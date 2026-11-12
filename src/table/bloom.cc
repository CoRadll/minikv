#include "minikv/table/bloom.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "minikv/util/crc32c.h"

namespace minikv {
namespace {

// 每个 bit 的字节/掩码
inline void SetBit(std::string* bits, std::uint32_t index) {
  (*bits)[index / 8] = static_cast<char>(
      static_cast<unsigned char>((*bits)[index / 8]) | static_cast<unsigned char>(1U << (index % 8)));
}

inline bool TestBit(const std::string& bits, std::uint32_t index) {
  return (static_cast<unsigned char>(bits[index / 8]) & (1U << (index % 8))) != 0;
}

std::uint32_t Hash(std::string_view key) {
  return static_cast<std::uint32_t>(Crc32c(key));
}

std::uint32_t Rotate(std::uint32_t value) { return (value >> 17) | (value << 15); }

}  // namespace

BloomFilter::BloomFilter(std::size_t key_count, std::size_t bits_per_key) {
  // 位数组按 key 数量线性增长，最少 64 位（避免小表过度稀疏）
  const std::size_t bits = std::max<std::size_t>(64, key_count * bits_per_key);
  bits_.assign((bits + 7) / 8, '\0');

  // 最优哈希次数 k = m/n * ln2 ≈ bits_per_key * 0.69，限制在 [1, 30]
  const std::size_t k = static_cast<std::size_t>(static_cast<double>(bits_per_key) * 0.69);
  hash_count_ = std::clamp<std::size_t>(k, 1, 30);
}

void BloomFilter::Add(std::string_view key) {
  const std::uint32_t h = Hash(key);
  const std::uint32_t delta = Rotate(h);
  const auto num_bits = static_cast<std::uint32_t>(bits_.size() * 8);

  for (std::size_t i = 0; i < hash_count_; ++i) {
    SetBit(&bits_, (h + static_cast<std::uint32_t>(i) * delta) % num_bits);
  }
}

bool BloomFilter::MayContain(std::string_view key) const {
  if (bits_.empty()) {
    return false;
  }

  const std::uint32_t h = Hash(key);
  const std::uint32_t delta = Rotate(h);
  const auto num_bits = static_cast<std::uint32_t>(bits_.size() * 8);

  for (std::size_t i = 0; i < hash_count_; ++i) {
    if (!TestBit(bits_, (h + static_cast<std::uint32_t>(i) * delta) % num_bits)) {
      return false;
    }
  }
  return true;
}

std::string BloomFilter::Finish() const {
  // 落盘格式：[k: 1 字节][位数组]
  std::string encoded;
  encoded.reserve(bits_.size() + 1);
  encoded.push_back(static_cast<char>(hash_count_));
  encoded.append(bits_);
  return encoded;
}

bool BloomFilter::MayContainIn(std::string_view filter_data, std::string_view key) {
  if (filter_data.size() < 2) {
    return true;  // 数据缺失时保守放行，交给后续真正查找
  }

  const auto hash_count =
      static_cast<std::size_t>(static_cast<unsigned char>(filter_data[0]));
  const std::string bits(filter_data.substr(1));
  if (bits.empty() || hash_count == 0) {
    return true;
  }

  const std::uint32_t h = Hash(key);
  const std::uint32_t delta = Rotate(h);
  const auto num_bits = static_cast<std::uint32_t>(bits.size() * 8);

  for (std::size_t i = 0; i < hash_count; ++i) {
    if (!TestBit(bits, (h + static_cast<std::uint32_t>(i) * delta) % num_bits)) {
      return false;
    }
  }
  return true;
}

}  // namespace minikv
