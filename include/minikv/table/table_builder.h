#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "minikv/table/block_builder.h"
#include "minikv/util/file.h"
#include "minikv/util/status.h"

namespace minikv {

// 写出一个 SSTable：数据块 + 索引块 + Footer。
//
// 调用约定：Add 的 key 必须严格递增，否则返回 InvalidArgument。
// 空表（没有任何 Add 就 Finish）同样返回 InvalidArgument。
// 草案：S4 会增加布隆过滤器参数。
class TableBuilder {
 public:
  explicit TableBuilder(std::filesystem::path path, std::size_t block_size = 4096);
  ~TableBuilder();

  TableBuilder(const TableBuilder&) = delete;
  TableBuilder& operator=(const TableBuilder&) = delete;

  Status Add(std::string_view key, std::string_view value, bool deleted = false);
  Status Finish();

  bool finished() const { return finished_; }
  std::uint64_t FileSize() const { return file_size_; }
  std::uint64_t NumEntries() const { return num_entries_; }
  std::uint64_t NumDataBlocks() const { return num_data_blocks_; }

  // Finish 之后可用；空表时为空串。
  const std::string& smallest_key() const { return smallest_key_; }
  const std::string& largest_key() const { return largest_key_; }

 private:
  Status EnsureOpen();
  Status FlushBlock();

  std::filesystem::path path_;
  std::size_t block_size_;
  File file_;
  BlockBuilder data_builder_;
  BlockBuilder index_builder_;
  std::uint64_t file_size_ = 0;
  std::uint64_t num_entries_ = 0;
  std::uint64_t num_data_blocks_ = 0;
  std::string last_key_;
  std::string smallest_key_;
  std::string largest_key_;
  bool finished_ = false;
};

}  // namespace minikv
