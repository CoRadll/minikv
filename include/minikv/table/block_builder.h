#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace minikv {

// TODO(S3): 前缀压缩 + 重启点的块构建器。
// 重启点让块在压缩后仍能二分查找。
// 草案：S3 实现时允许调整接口签名。
class BlockBuilder {
 public:
  void Add(std::string_view key, std::string_view value);
  std::string Finish();
  void Reset();

  std::size_t CurrentSizeEstimate() const { return buffer_.size(); }
  bool empty() const { return buffer_.empty(); }

 private:
  std::string buffer_;
  std::size_t counter_ = 0;
  bool finished_ = false;
};

}  // namespace minikv
