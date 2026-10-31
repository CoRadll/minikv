#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace minikv {

// 每隔多少条记录插入一个重启点（完整 key，不参与前缀压缩）。
// 间隔越小，查得快但压缩率差；越大反之。
inline constexpr std::size_t kBlockRestartInterval = 16;

// 数据块构建器：前缀压缩 + 重启点。
//
// 调用约定：Add 的 key 必须严格递增（SSTable 的上层保证）。
// Finish() 会消费内部缓冲并返回完整块字节（含重启点数组、计数、CRC）。
class BlockBuilder {
 public:
  void Add(std::string_view key, std::string_view value, bool deleted = false);
  std::string Finish();
  void Reset();

  std::size_t CurrentSizeEstimate() const {
    return buffer_.size() + restarts_.size() * 4 + 8;
  }
  bool empty() const { return buffer_.empty(); }

 private:
  std::string buffer_;
  std::vector<std::uint32_t> restarts_{0};  // 第一条永远是重启点
  std::string last_key_;
  std::size_t counter_ = 0;
};

}  // namespace minikv
