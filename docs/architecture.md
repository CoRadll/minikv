# 架构说明

## 1. 分层

依赖方向严格向下，下层不知道上层的存在。

```
tools/          可执行入口：CLI、压测工具
   │
api/            Engine 门面：参数校验、请求编排、生命周期
   │
db/             版本管理（Manifest）、Compaction、扫描迭代器
   │
table/          SSTable 读写、布隆过滤器、多路归并
   │
memtable/       有序内存表（有序结构 + 墓碑）
   │
wal/            预写日志：记录格式、追加、回放、坏尾截断
   │
util/           编码、CRC、文件 IO、目录操作、日志、内存池
```

规则：

- 任何模块只能依赖它下方的模块
- `util/` 不依赖任何业务模块
- `tools/` 不对外提供头文件，只有 `main()`

## 2. 写路径

```
Put(k, v)
  → 编码为 WAL 记录（类型 / 长度 / 载荷 / CRC32）
  → 追加到 WAL（可选 fsync）
  → 写入 MemTable
  → 返回

MemTable 达到阈值时：
  → 冻结当前 MemTable
  → 顺序写出 SSTable
  → 更新 Manifest，登记新文件
  → 换一个空 MemTable
```

关键约束：**先写 WAL，再改内存**。WAL 写失败时，内存不能变。

## 3. 读路径

```
Get(k)
  → 查 MemTable，命中返回
  → 从新到旧扫描各层 SSTable：
      → 布隆过滤器判定「肯定不存在」则跳过（不读盘）
      → 读索引块定位数据块
      → 只读命中的块
      → 命中墓碑则返回 NotFound
  → 全部未命中，返回 NotFound
```

同一个 key 可能存在多个历史版本，**必须按新到旧的顺序读取**。

## 4. 崩溃恢复

```
Open()
  → 打开 WAL
  → 逐条解析记录并校验 CRC
  → 校验通过：重放到 MemTable
  → 校验失败：判定为「写了一半」，截断到最后一条完整记录
  → 坏记录若不在尾部，视为真实损坏，返回 Corruption
```

保证语义：`Put` 返回成功之后，重启必定能读到该数据。

## 5. 目录语义

| 目录 | 说明 |
|---|---|
| `util/` | 被链接进核心库的底层设施，不含 `main()` |
| `tools/` | 链接核心库后生成的独立可执行程序，每个文件必有一个 `main()` |

两者不是重复，是「零件」与「用零件组装出的成品」的关系。

## 6. 头文件组织

`include/minikv/` 与 `src/` **逐文件同构**：

```
src/util/crc32c.cc  ↔  include/minikv/util/crc32c.h
src/wal/wal_writer.cc  ↔  include/minikv/wal/wal_writer.h
```

例外（允许只有头文件，无 `.cc`）：

- `include/minikv/api/options.h` —— 纯数据结构定义
- `include/minikv/api/iterator.h` —— 纯虚接口
- `include/minikv/api/version.h` —— 纯常量定义

该规则由 `scripts/check_layout.ps1` 机械校验，避免靠自觉。

## 7. S1 落地细节

### WAL 记录布局（小端）

```
+--------+--------+----------+------------+-----+-------+
| crc32  | type   | key_len  | value_len  | key | value |
| 4 字节 | 1 字节 | 4 字节   | 4 字节     |     |       |
+--------+--------+----------+------------+-----+-------+
```

- `crc32`（Castagnoli）覆盖 `type` 到 `value` 末尾的全部字节
- `type`：`1 = Put`、`2 = Delete`；未知值直接判为坏数据
- 上限：key ≤ 1 KB、value ≤ 64 MB，超限在格式层拒绝

### 写入时序（Engine::Put）

```
校验 key/value
  → 加写锁（串行化写入）
  → 编码记录
  → WAL 追加（sync_on_write 时 fsync）
      ↑ 失败则直接返回，内存不变
  → MemTable 写入
  → 返回
```

### 启动时序（Engine::Open）

```
创建数据目录
  → WalReader::Replay 读取 wal.log
      ├─ 逐条 DecodeRecord
      ├─ 成功：callback 应用到 MemTable
      └─ 失败：停止，记录 dropped_bytes
  → 若 dropped_bytes > 0：截断文件并 fsync
  → 以 valid_bytes 为起点打开 WalWriter（续写）
```

### 坏尾处理语义

见 ADR D-010：无法从字节层面区分「尾部写了一半」与「中段损坏」，
因此统一在第一条坏记录处停止并截断，丢弃字节数通过
`WalReader::dropped_bytes()` 与 WARN 日志暴露。

### 并发模型

| 操作 | 同步方式 |
|---|---|
| Put / Delete / Close | Engine 级 `std::mutex`（WAL 顺序与内存一致） |
| Get | MemTable 内 `std::shared_mutex` 共享锁，可与写并发 |
| WalReader::Replay | 单线程，启动期一次性执行 |

## 8. 待补充

- S2：SSTable 文件格式（数据块 / 索引块 / Footer）
- S3：Compaction 的文件选取策略与写放大实测
- S4：布隆过滤器参数推导与缓存命中率
