#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "minikv/util/status.h"

namespace minikv {

// TODO(S3): 解析已序列化的数据块，支持按 key 查找与顺序遍历。
// 草案：S3 实现时允许调整接口签名。
class Block {
 public:
  explicit Block(std::string contents);

  Status Get(std::string_view key, std::string* value) const;
  std::size_t size() const { return contents_.size(); }

 private:
  std::string contents_;
};

}  // namespace minikv
