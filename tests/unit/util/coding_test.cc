#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "minikv/util/coding.h"

#include "support/test_framework.h"

namespace {

void ExpectVarint32(const std::uint32_t value, const std::size_t expected_size) {
  std::string buffer;
  minikv::PutVarint32(&buffer, value);
  CHECK_EQ(buffer.size(), expected_size);

  std::string_view input(buffer);
  std::uint32_t decoded = 0;
  CHECK_TRUE(minikv::GetVarint32(&input, &decoded));
  CHECK_EQ(decoded, value);
  CHECK_TRUE(input.empty());
}

void ExpectVarint64(const std::uint64_t value, const std::size_t expected_size) {
  std::string buffer;
  minikv::PutVarint64(&buffer, value);
  CHECK_EQ(buffer.size(), expected_size);

  std::string_view input(buffer);
  std::uint64_t decoded = 0;
  CHECK_TRUE(minikv::GetVarint64(&input, &decoded));
  CHECK_EQ(decoded, value);
  CHECK_TRUE(input.empty());
}

}  // namespace

TEST(Coding, Varint32Boundaries) {
  ExpectVarint32(0u, 1);
  ExpectVarint32(1u, 1);
  ExpectVarint32(127u, 1);
  ExpectVarint32(128u, 2);
  ExpectVarint32(16383u, 2);
  ExpectVarint32(16384u, 3);
  ExpectVarint32(2097151u, 3);    // 2^21 - 1
  ExpectVarint32(2097152u, 4);    // 2^21
  ExpectVarint32(268435455u, 4);  // 2^28 - 1
  ExpectVarint32(268435456u, 5);  // 2^28
  ExpectVarint32(0xFFFFFFFFu, 5);
}

TEST(Coding, Varint64Boundaries) {
  ExpectVarint64(0u, 1);
  ExpectVarint64(127u, 1);
  ExpectVarint64(128u, 2);
  ExpectVarint64(0xFFFFFFFFull, 5);
  ExpectVarint64(0x100000000ull, 5);  // 2^32
  ExpectVarint64(1ull << 35, 6);
  ExpectVarint64(1ull << 56, 9);
  ExpectVarint64(0xFFFFFFFFFFFFFFFFull, 10);
}

TEST(Coding, Varint32RejectsTruncatedInput) {
  std::string buffer;
  minikv::PutVarint32(&buffer, 16384u);  // 3 字节

  // 必须先落成一个具名 string 再取 view：直接 view(buffer.substr(...))
  // 会指向临时对象，语句结束即悬空。
  const std::string truncated = buffer.substr(0, 2);
  std::string_view input(truncated);
  const char* before = input.data();
  std::uint32_t decoded = 12345;
  CHECK_FALSE(minikv::GetVarint32(&input, &decoded));
  CHECK_EQ(decoded, 12345u);
  CHECK_TRUE(input.data() == before);
  CHECK_EQ(input.size(), static_cast<std::size_t>(2));
}

TEST(Coding, Varint32RejectsOverflow) {
  // 6 个字节的连续位，远超 32 位可表示范围
  const char bytes[] = {static_cast<char>(0xFF), static_cast<char>(0xFF), static_cast<char>(0xFF),
                        static_cast<char>(0xFF), static_cast<char>(0x1F)};
  std::string_view input(bytes, sizeof(bytes));
  std::uint32_t decoded = 0;
  CHECK_FALSE(minikv::GetVarint32(&input, &decoded));
}

TEST(Coding, Varint32LeavesTrailingBytes) {
  std::string buffer;
  minikv::PutVarint32(&buffer, 300u);
  buffer.append("tail");

  std::string_view input(buffer);
  std::uint32_t decoded = 0;
  CHECK_TRUE(minikv::GetVarint32(&input, &decoded));
  CHECK_EQ(decoded, 300u);
  CHECK_STREQ(input, "tail");
}

TEST(Coding, Fixed32IsLittleEndian) {
  std::string buffer;
  minikv::PutFixed32(&buffer, 0x01020304u);
  CHECK_EQ(buffer.size(), static_cast<std::size_t>(4));
  CHECK_EQ(static_cast<unsigned>(static_cast<unsigned char>(buffer[0])), 0x04u);
  CHECK_EQ(static_cast<unsigned>(static_cast<unsigned char>(buffer[1])), 0x03u);
  CHECK_EQ(static_cast<unsigned>(static_cast<unsigned char>(buffer[2])), 0x02u);
  CHECK_EQ(static_cast<unsigned>(static_cast<unsigned char>(buffer[3])), 0x01u);

  std::string_view input(buffer);
  std::uint32_t decoded = 0;
  CHECK_TRUE(minikv::GetFixed32(&input, &decoded));
  CHECK_EQ(decoded, 0x01020304u);
  CHECK_TRUE(input.empty());
}

TEST(Coding, Fixed64RoundTrip) {
  std::string buffer;
  minikv::PutFixed64(&buffer, 0x0102030405060708ull);
  CHECK_EQ(buffer.size(), static_cast<std::size_t>(8));

  std::string_view input(buffer);
  std::uint64_t decoded = 0;
  CHECK_TRUE(minikv::GetFixed64(&input, &decoded));
  CHECK_EQ(decoded, 0x0102030405060708ull);
  CHECK_TRUE(input.empty());
}

TEST(Coding, FixedRejectsShortInput) {
  std::string_view input("abc");
  std::uint32_t value32 = 0;
  std::uint64_t value64 = 0;
  CHECK_FALSE(minikv::GetFixed32(&input, &value32));
  CHECK_FALSE(minikv::GetFixed64(&input, &value64));
  CHECK_EQ(input.size(), static_cast<std::size_t>(3));
}
