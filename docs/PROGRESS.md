# 进度交接（供新会话快速接手）

> 更新于 S1 完成、S2 待开始。

## 项目

MiniKV：C++20 从零实现的 LSM-Tree KV 存储引擎。位置 `D:\Users\72673\Documents\deepseek-harness\default-workspace\minikv`。
远程 https://github.com/CoRadll/minikv ，CI 在 windows-latest 跑 98 个测试。

## 已完成

| 阶段 | 内容 |
|---|---|
| S0 | CMake/presets、警告与 ASan 模块、脚本（build/test/check_layout，PS5.1 兼容）、CI、目录同构 |
| S1 | Status、日志、CRC32C、varint/fixed、File/Env、WAL 格式/写/回放、MemTable、Engine、CLI、集成测试、`demo s1` |
| S2 | 块格式（前缀压缩+重启点）、SSTable 写读、Manifest、刷盘链路、崩溃边界、`demo s2`、Arena + 跳表替换 `std::map` |
| S3 | 顺序迭代器、多路归并、L0+L1 全量合并、墓碑清理、MANIFEST 原子安装、`demo s3` |
| S4 | Bloom filter、LRU 块缓存、跨层有序扫描、`scan` 命令、`demo s4` |
| S5 | 压测工具（吞吐 + P50/P95/P99）、真实强杀恢复脚本、`demo s5`、README 实测数字 |

当前测试：**187 个全绿**，0 警告（Debug + RelWithDebInfo）。

## 关键约定（不要推翻）

- **不用异常**，错误一律 `Status` 返回值（ADR D-002）
- `include/minikv/` 与 `src/` 逐文件同构；例外 `api/options.h`、`api/iterator.h`、`api/version.h`
- 头文件分「契约级」与「草案级」，草案标注可改（ADR D-005）
- 构建脚本纯 ASCII；`.cmd` 包装器绕过 ExecutionPolicy（ADR D-006）
- 测试临时目录：`MINIKV_TEST_TMPDIR` → `%TEMP%` → cwd（ADR D-008）
- WAL 坏尾：第一条坏记录处停止并截断，`dropped_bytes()` 暴露（ADR D-010）
- 写入顺序：**先 WAL 成功，再改 MemTable**
- 每个阶段交付 `minikv demo sN`（ADR D-007）

## 尚未实现（S2–S5）

| 阶段 | 任务 |
|---|---|
| S4 | 布隆过滤器、块缓存、MergeIterator / `scan` |
| S5 | 压测工具、真实 kill -9、吞吐与 P99 指标、`demo s5` |

## 常用命令

```powershell
cd D:\Users\72673\Documents\deepseek-harness\default-workspace\minikv
.\scripts\build.cmd                 # 构建
.\scripts\test.cmd -Build           # 同构检查 + 构建 + 全部测试
.\build\bin\Debug\minikv.exe demo s1 --dir .\demo-data\s1
.\build\bin\Debug\minikv.exe --dir .\data      # REPL: put/get/del/stats
```

## 环境注意

- 只有 Windows PowerShell 5.1（无 pwsh）；脚本纯 ASCII
- exe 被限制为只能写工作区内路径（沙箱），数据目录用相对路径
- 系统 `%TEMP%` 对 exe 不可写，测试用 `MINIKV_TEST_TMPDIR` 指向项目内 `.minikv-test-tmp`
- push 需用户自己执行（agent 推送要多一次提权）
