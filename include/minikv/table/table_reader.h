#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "minikv/api/iterator.h"
#include "minikv/util/status.h"

namespace minikv {

// TODO(S3): 打开 SSTable，通过索引块定位数据块，只读命中的块。
// 草案：S3 实现时允许调整接口签名（块缓存注入方式待定）。
class TableReader {
 public:
  static Status Open(const std::filesystem::path& path, std::unique_ptr<TableReader>* reader);

  Status Get(std::string_view key, std::string* value) const;
  std::unique_ptr<Iterator> NewIterator() const;
  uint64_t file_size() const { return file_size_; }

 private:
  std::filesystem::path path_;
  uint64_t file_size_ = 0;
};

}  // namespace minikv
