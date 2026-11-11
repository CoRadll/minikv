#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "minikv/util/status.h"

namespace minikv {

class TableReader;

// 合并结果。没有有效数据时不产出文件（例如所有 key 都被删除）。
struct CompactionResult {
  bool has_output = false;
  std::uint64_t file_number = 0;
  std::uint64_t entries = 0;
  std::string smallest_key;
  std::string largest_key;
};

// 把多个 SSTable 合并成一个。
//
// 约定：
//   - inputs 必须按「新 → 旧」排列，同一个 key 取第一个来源（最新版本）
//   - 合并结果写入 output_path；墓碑直接丢弃（本实现的两层模型里它就是最底层）
//   - 不修改 MANIFEST，也不删除旧文件；那些属于调用方（Engine）的事务
class Compaction {
 public:
  static Status Run(const std::vector<const TableReader*>& inputs, std::uint64_t output_number,
                    const std::filesystem::path& output_path, CompactionResult* result);
};

}  // namespace minikv
