# MiniKV

从零实现的 LSM-Tree 持久化键值存储引擎，C++20，**运行时零第三方依赖**。

> 目标不是再造一个 RocksDB，而是把一个存储引擎该有的部件完整走一遍：
> WAL 崩溃恢复、MemTable、分层 SSTable、布隆过滤器、Compaction、压测与量化。

<!-- 徽章指向实际仓库 -->
[![CI](https://github.com/CoRadll/minikv/actions/workflows/ci.yml/badge.svg)](https://github.com/CoRadll/minikv/actions/workflows/ci.yml)

## 状态

每个阶段结束时提供一个可执行的现场演示：`minikv.exe demo sN`，输出确定性 transcript 并落盘。
规格见 [docs/demo-spec.md](docs/demo-spec.md)。

| 阶段 | 内容 | 演示 | 状态 |
|---|---|---|---|
| S0 | 工程骨架、构建系统、目录同构检查 | `minikv --version` + 布局检查 | ✅ 已完成 |
| S1 | WAL + MemTable + 崩溃恢复 | `demo s1`：写入→读→删→重启回放→坏尾截断 | 🚧 进行中 |
| S2 | SSTable 落盘、Manifest、启动加载 | `demo s2`：阈值触发刷盘→重启从 SSTable 读 | ⬜ 待开始 |
| S3 | Compaction 与版本切换 | `demo s3`：合并前后文件数与大小对比 | ⬜ 待开始 |
| S4 | 布隆过滤器、块缓存、Scan 归并 | `demo s4`：布隆过滤器挡掉的读盘次数 | ⬜ 待开始 |
| S5 | 对外接口、压测工具、崩溃测试 | `demo s5`：真实 kill -9 恢复 + bench 数字 | ⬜ 待开始 |

## 特性（目标）

- [x] 工程骨架与 CI
- [ ] 崩溃安全：进程被强杀后已确认的写入不丢
- [ ] 数据落盘：超过内存阈值自动写成 SSTable
- [ ] 后台合并：多路归并 + 墓碑清理
- [ ] 读路径优化：布隆过滤器剪枝、块缓存
- [ ] 范围扫描：跨 MemTable 与多层文件的有序遍历
- [ ] 量化指标：吞吐与 P50 / P99 延迟

## 架构

```
            ┌─────────────────────────┐
            │   tools/ (CLI, bench)   │
            └────────────┬────────────┘
                         │
            ┌────────────▼────────────┐
            │   api/   Engine 门面    │
            └────────────┬────────────┘
                         │
            ┌────────────▼────────────┐
            │   db/  版本与合并编排   │
            └───────┬──────────┬──────┘
                    │          │
        ┌───────────▼──┐   ┌───▼────────┐
        │  table/      │   │  cache/    │
        │  SSTable     │   │  块缓存    │
        └───────┬──────┘   └────────────┘
                │
        ┌───────▼──────┐
        │  memtable/   │
        │  有序内存表  │
        └───────┬──────┘
                │
        ┌───────▼──────┐
        │    wal/      │
        │  崩溃恢复    │
        └───────┬──────┘
                │
        ┌───────▼──────┐
        │    util/     │
        │ 基础设施     │
        └──────────────┘
```

写路径：`Engine::Put` → WAL 追加 → MemTable 写入 → 达到阈值后刷成 SSTable。
读路径：MemTable → 从新到旧的 SSTable，布隆过滤器剪枝。

细节见 [架构说明](docs/architecture.md)。

## 构建

环境要求：CMake ≥ 3.20（使用预设文件需 ≥ 3.21）、支持 C++20 的编译器
（Windows 上已验证 MSVC 19.34 / VS 2022）。

### 推荐：用预设

```powershell
cmake --preset msvc
cmake --build --preset debug
ctest --preset debug
.\build\bin\Debug\minikv.exe
```

### 或不依赖预设（任何平台通用）

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
.\build\bin\Debug\minikv.exe
```

### 命令行

```powershell
.\build\bin\Debug\minikv.exe --dir .\data
```

```
minikv> put user:1 zhangsan
OK
minikv> get user:1
"zhangsan"
minikv> del user:1
OK
minikv> get user:1
NOT FOUND
minikv> stats
db path        : .\data
wal bytes      : 91
memtable       : 2 entries, 80 bytes (approx)
minikv> exit
```

| 命令 | 作用 |
|---|---|
| `put <key> <value>` | 写入或覆盖（value 可含空格） |
| `get <key>` | 读取；不存在时打印 `NOT FOUND` |
| `del <key>` | 删除（写墓碑） |
| `stats` | 打印 WAL 字节数与内存表条目 |
| `help` / `exit` | 帮助 / 退出 |

启动参数：`--dir <path>` 指定数据目录（默认 `data`），`--verbose` 打开 INFO 级日志，
`--version` 打印版本。

数据在进程退出后由 WAL 保留，重新启动会自动回放。

### 可选项
| 选项 | 默认 | 作用 |
|---|---|---|
| `MINIKV_WARNINGS_AS_ERRORS` | OFF | 把警告当错误，适合 CI 与提交前自检 |
| `MINIKV_ENABLE_ASAN` | OFF | 启用 AddressSanitizer；MSVC 需先在 VS Installer 安装「C++ AddressSanitizer」组件 |

所有构建选项都以 `PRIVATE` 方式施加在本项目 target 上，链接 `minikv_core` 的下游项目
不会被迫继承警告等级或 sanitizer 运行时。

### 脚本（Windows）

| 脚本 | 作用 |
|---|---|
| `scripts\build.cmd [-Config Debug\|Release\|RelWithDebInfo] [-Clean] [-Asan]` | 配置 + 构建 |
| `scripts\test.cmd [-Config ...] [-Build] [-SkipLayout] [-Filter <regex>]` | 同构检查 + ctest |
| `scripts\check_layout.cmd` | 只跑 `include/` 与 `src/` 的同构检查 |

`.cmd` 是 PowerShell 脚本的包装器：`.cmd` 不受 ExecutionPolicy 限制，在默认
`Restricted` 策略的机器上可直接调用。脚本本体为**纯 ASCII**——Windows PowerShell 5.1
会把无 BOM 的脚本按 ANSI 解码，中文会变成语法错误（见 ADR D-006）。

测试的临时目录按 `MINIKV_TEST_TMPDIR` → 系统临时目录 → 当前工作目录 的顺序回退。
若 `%TEMP%` 对可执行文件不可写（受限沙箱、安全软件拦截等），用
`setx MINIKV_TEST_TMPDIR "<项目目录>\.minikv-test-tmp"` 指定一个**确认可写**的目录；
在受限环境里该目录通常需要位于项目工作区之内。

## 目录

| 目录 | 职责 |
|---|---|
| `include/minikv/` | 公开头文件，与 `src/` 逐文件同构 |
| `include/minikv/api/` | 对外门面（`Engine` / `Options` / `Iterator`） |
| `src/util/` | 可复用底层设施：编码、校验、文件 IO、日志 |
| `src/wal/` | 预写日志与崩溃恢复 |
| `src/memtable/` | 有序内存表 |
| `src/table/` | SSTable、布隆过滤器、归并迭代器 |
| `src/db/` | 版本管理、Compaction、扫描迭代器 |
| `src/cache/` | 块缓存 |
| `src/tools/` | 可执行程序入口（CLI、压测），不提供对外接口 |
| `tests/` | 单元测试与集成测试，目录镜像 `src/` |
| `docs/` | 架构说明与设计决策记录 |

**命名约定**：`util/` 是被链接的库代码，`tools/` 是链接这些库的独立程序入口，两者不重复。

## 性能

S5 完成后填入实测数据（顺序写吞吐、点查 P99、1GB 数据恢复耗时）。

## 文档

- [架构说明](docs/architecture.md)
- [设计决策（ADR）](docs/decisions.md)

## 开发说明

本项目采用 AI 辅助开发（AI-assisted development）：由 AI 承担重复性编码与骨架生成，
所有架构选择、模块边界与实现细节均由作者逐层把关，设计权衡记录在
[docs/decisions.md](docs/decisions.md)。

## License

[MIT](LICENSE)
