#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace minikv {

// TODO(S5): 位数组 + 双哈希（Kirsch-Mitzenmacher）。
// 语义：MayContain 返回 false 时该 key 一定不存在；返回 true 时可能误判。
// 草案：S5 实现时允许调整接口签名。
class BloomFilter {
 public:
  BloomFilter(std::size_t key_count, std::size_t bits_per_key = 10);

  void Add(std::string_view key);
  bool MayContain(std::string_view key) const;

  // 序列化后的位数组，随 SSTable 一起落盘。
  std::string Finish() const;

 private:
  std::string bits_;
  std::size_t hash_count_ = 7;
};

}  // namespace minikv
