# MiniKV

从零实现的 LSM-Tree 持久化键值存储引擎，C++20，**运行时零第三方依赖**。

> 目标不是再造一个 RocksDB，而是把一个存储引擎该有的部件完整走一遍：
> WAL 崩溃恢复、MemTable、分层 SSTable、布隆过滤器、Compaction、压测与量化。

<!-- 徽章指向实际仓库 -->
[![CI](https://github.com/CoRadll/minikv/actions/workflows/ci.yml/badge.svg)](https://github.com/CoRadll/minikv/actions/workflows/ci.yml)

## 状态

| 阶段 | 内容 | 状态 |
|---|---|---|
| S0 | 工程骨架、构建系统、目录同构检查 | ✅ 已完成 |
| S1 | WAL + MemTable + 崩溃恢复 | 🚧 进行中 |
| S2 | SSTable 落盘、Manifest、启动加载 | ⬜ 待开始 |
| S3 | Compaction 与版本切换 | ⬜ 待开始 |
| S4 | 布隆过滤器、块缓存、Scan 归并 | ⬜ 待开始 |
| S5 | 对外接口、压测工具、崩溃测试 | ⬜ 待开始 |

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

### 可选项

| 选项 | 默认 | 作用 |
|---|---|---|
| `MINIKV_WARNINGS_AS_ERRORS` | OFF | 把警告当错误，适合 CI 与提交前自检 |
| `MINIKV_ENABLE_ASAN` | OFF | 启用 AddressSanitizer；MSVC 需先在 VS Installer 安装「C++ AddressSanitizer」组件 |

所有构建选项都以 `PRIVATE` 方式施加在本项目 target 上，链接 `minikv_core` 的下游项目
不会被迫继承警告等级或 sanitizer 运行时。

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
