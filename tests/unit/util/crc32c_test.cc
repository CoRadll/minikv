#include <cstdint>
#include <string>
#include <string_view>

#include "minikv/util/crc32c.h"

#include "support/test_framework.h"

TEST(Crc32c, KnownVectors) {
  // CRC32C 的公开测试向量
  CHECK_EQ(minikv::Crc32c(""), 0x00000000u);
  CHECK_EQ(minikv::Crc32c("123456789"), 0xE3069283u);
}

TEST(Crc32c, SingleByteIncrementsMatchWhole) {
  std::uint32_t crc = 0;
  for (const char ch : std::string("123456789")) {
    crc = minikv::Crc32cExtend(crc, std::string_view(&ch, 1));
  }
  CHECK_EQ(crc, minikv::Crc32c("123456789"));
}

TEST(Crc32c, ExtendMatchesConcatenation) {
  CHECK_EQ(minikv::Crc32cExtend(minikv::Crc32c("1234"), "56789"), minikv::Crc32c("123456789"));
  CHECK_EQ(minikv::Crc32cExtend(minikv::Crc32c(""), "abc"), minikv::Crc32c("abc"));
  CHECK_EQ(minikv::Crc32cExtend(minikv::Crc32c("abc"), ""), minikv::Crc32c("abc"));
}

TEST(Crc32c, HandlesEmbeddedNul) {
  const std::string_view with_nuls("\0\0\0", 3);
  const std::uint32_t whole = minikv::Crc32c(with_nuls);

  std::uint32_t chunked = 0;
  for (std::size_t i = 0; i < with_nuls.size(); ++i) {
    chunked = minikv::Crc32cExtend(chunked, with_nuls.substr(i, 1));
  }
  CHECK_EQ(chunked, whole);
}

TEST(Crc32c, DetectsSingleBitFlip) {
  std::string data = "the quick brown fox";
  const std::uint32_t original = minikv::Crc32c(data);

  data[3] = static_cast<char>(data[3] ^ 0x01);
  CHECK_TRUE(minikv::Crc32c(data) != original);
}
