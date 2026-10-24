#include "minikv/util/crc32c.h"

#include <cstddef>
#include <cstdint>

namespace minikv {
namespace {

// CRC32C（Castagnoli）用反射多项式 0x82F63B78。
// 表在编译期生成，运行时只做查表 + 移位。
constexpr std::uint32_t kPolynomial = 0x82F63B78u;

struct Crc32cTable {
  std::uint32_t entries[256]{};

  constexpr Crc32cTable() {
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t crc = i;
      for (int bit = 0; bit < 8; ++bit) {
        crc = ((crc & 1U) != 0U) ? ((crc >> 1) ^ kPolynomial) : (crc >> 1);
      }
      entries[i] = crc;
    }
  }
};

constexpr Crc32cTable kTable{};

}  // namespace

std::uint32_t Crc32cExtend(std::uint32_t crc, std::string_view data) {
  // 反射输入/输出：内部状态与传入值都做按位取反
  std::uint32_t state = crc ^ 0xFFFFFFFFu;
  for (const char ch : data) {
    const auto index = static_cast<std::uint32_t>(static_cast<unsigned char>(ch)) ^ (state & 0xFFu);
    state = kTable.entries[index] ^ (state >> 8);
  }
  return state ^ 0xFFFFFFFFu;
}

std::uint32_t Crc32c(std::string_view data) { return Crc32cExtend(0, data); }

}  // namespace minikv
