#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "minikv/util/status.h"

namespace minikv {

// 已序列化的数据块。构造时校验 CRC 与布局，失败则 valid() 为 false。
//
// 块布局（见 docs/architecture.md 8.1）：
//   [entries...][restart_offsets: u32×N][restart_count: u32][crc32: u32]
class Block {
 public:
  explicit Block(std::string contents);

  bool valid() const { return valid_; }

  // 命中返回 OK；key 不在块内返回 NotFound；块损坏返回 Corruption。
  Status Get(std::string_view key, std::string* value, bool* deleted) const;

  std::size_t size() const { return contents_.size(); }
  std::size_t NumRestarts() const { return restarts_.size(); }

 private:
  bool Parse();

  std::string contents_;
  std::vector<std::uint32_t> restarts_;
  std::size_t entries_end_ = 0;
  bool valid_ = false;
};

}  // namespace minikv
