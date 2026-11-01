#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "minikv/api/iterator.h"
#include "minikv/table/block.h"
#include "minikv/util/file.h"
#include "minikv/util/status.h"

namespace minikv {

// 打开一个 SSTable 并按 key 查找。
//
// 读取路径：文件尾 Footer → 索引块（常驻内存）→ 二分定位数据块 → 只读那一块。
// 草案：S4 会加入块缓存与布隆过滤器。
class TableReader {
 public:
  static Status Open(const std::filesystem::path& path, std::unique_ptr<TableReader>* reader);

  // 命中返回 OK；不存在返回 NotFound；文件/块损坏返回 Corruption。
  Status Get(std::string_view key, std::string* value, bool* deleted) const;

  // TODO(S4): 顺序迭代（Scan / Compaction 用）
  std::unique_ptr<Iterator> NewIterator() const;

  std::uint64_t file_size() const { return file_size_; }

 private:
  TableReader() = default;

  std::filesystem::path path_;
  File file_;
  std::unique_ptr<Block> index_;
  std::uint64_t file_size_ = 0;
};

}  // namespace minikv
