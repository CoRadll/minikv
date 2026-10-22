#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

#include "minikv/util/status.h"

namespace minikv {

// TODO(S3): 写出一个 SSTable：数据块 + 索引块 + 布隆过滤器 + Footer。
// 草案：S3 实现时允许调整接口签名（预计要补 Options 与布隆过滤器参数）。
class TableBuilder {
 public:
  explicit TableBuilder(std::filesystem::path path);

  Status Add(std::string_view key, std::string_view value);
  Status Finish();

  uint64_t FileSize() const { return file_size_; }

 private:
  std::filesystem::path path_;
  uint64_t file_size_ = 0;
};

}  // namespace minikv
