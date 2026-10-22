#pragma once

#include <cstddef>
#include <filesystem>

namespace minikv {

// 引擎配置。每个字段都有安全默认值，使用者只需改关心的项。
struct Options {
  // 数据目录（WAL、SSTable、MANIFEST 都放这里）
  std::filesystem::path db_path = "data";

  // true 时每次写入都 fsync，最安全但最慢。
  // 崩溃测试必须开启；性能测试建议关闭。
  bool sync_on_write = false;

  // MemTable 达到该字节数就冻结并刷成 SSTable
  std::size_t memtable_size = 4 * 1024 * 1024;

  // 单个 WAL 段文件的上限，超过后轮转到新段
  std::size_t wal_segment_size = 64 * 1024 * 1024;

  // 最大层数（L0 之外，每层容量按倍数增长）
  int max_levels = 7;

  // L0 文件数达到该值时触发 Compaction
  std::size_t level0_compaction_trigger = 4;
};

}  // namespace minikv
