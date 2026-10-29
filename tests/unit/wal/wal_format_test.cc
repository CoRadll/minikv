#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "minikv/util/coding.h"
#include "minikv/util/crc32c.h"
#include "minikv/wal/wal_format.h"

#include "support/test_framework.h"

namespace {

// 手工拼一条记录，用于构造编码器不会产生的坏数据
std::string CraftRecord(std::uint8_t type, std::uint32_t key_length, std::uint32_t value_length,
                        const std::string& key, const std::string& value) {
  std::string body;
  body.push_back(static_cast<char>(type));
  minikv::PutFixed32(&body, key_length);
  minikv::PutFixed32(&body, value_length);
  body.append(key);
  body.append(value);

  std::string encoded;
  minikv::PutFixed32(&encoded, minikv::Crc32c(body));
  encoded.append(body);
  return encoded;
}

minikv::WalRecord MakePut(const std::string& key, const std::string& value) {
  minikv::WalRecord record;
  record.type = minikv::RecordType::kPut;
  record.key = key;
  record.value = value;
  return record;
}

}  // namespace

TEST(WalFormat, RoundTripPut) {
  const minikv::WalRecord source = MakePut("user:1", "zhangsan");

  std::string encoded;
  minikv::EncodeRecord(source, &encoded);
  CHECK_EQ(encoded.size(),
           minikv::kRecordHeaderSize + source.key.size() + source.value.size());

  minikv::WalRecord decoded;
  std::size_t consumed = 0;
  CHECK_TRUE(minikv::DecodeRecord(encoded, &decoded, &consumed));
  CHECK_EQ(consumed, encoded.size());
  CHECK_EQ(decoded.type, minikv::RecordType::kPut);
  CHECK_STREQ(decoded.key, "user:1");
  CHECK_STREQ(decoded.value, "zhangsan");
}

TEST(WalFormat, RoundTripDeleteWithEmptyValue) {
  minikv::WalRecord source;
  source.type = minikv::RecordType::kDelete;
  source.key = "user:2";

  std::string encoded;
  minikv::EncodeRecord(source, &encoded);
  CHECK_EQ(encoded.size(), minikv::kRecordHeaderSize + source.key.size());

  minikv::WalRecord decoded;
  std::size_t consumed = 0;
  CHECK_TRUE(minikv::DecodeRecord(encoded, &decoded, &consumed));
  CHECK_EQ(decoded.type, minikv::RecordType::kDelete);
  CHECK_STREQ(decoded.key, "user:2");
  CHECK_TRUE(decoded.value.empty());
}

TEST(WalFormat, DetectsCorruptedValueByte) {
  std::string encoded;
  minikv::EncodeRecord(MakePut("k", "value"), &encoded);

  encoded.back() = static_cast<char>(encoded.back() ^ 0x01);

  minikv::WalRecord decoded;
  std::size_t consumed = 0;
  CHECK_FALSE(minikv::DecodeRecord(encoded, &decoded, &consumed));
}

TEST(WalFormat, DetectsCorruptedKeyByte) {
  std::string encoded;
  minikv::EncodeRecord(MakePut("abc", "v"), &encoded);

  // 翻转 key 的第一个字节（偏移 = 头部长度）
  encoded[minikv::kRecordHeaderSize] =
      static_cast<char>(encoded[minikv::kRecordHeaderSize] ^ 0x40);

  minikv::WalRecord decoded;
  std::size_t consumed = 0;
  CHECK_FALSE(minikv::DecodeRecord(encoded, &decoded, &consumed));
}

TEST(WalFormat, DetectsCorruptedLength) {
  std::string encoded;
  minikv::EncodeRecord(MakePut("abc", "v"), &encoded);

  // 改动长度字段，使头部自洽但 CRC 对不上
  encoded[minikv::kRecordHeaderSize - 8] = static_cast<char>(0x01);

  minikv::WalRecord decoded;
  std::size_t consumed = 0;
  CHECK_FALSE(minikv::DecodeRecord(encoded, &decoded, &consumed));
}

TEST(WalFormat, RejectsTruncatedRecord) {
  std::string encoded;
  minikv::EncodeRecord(MakePut("abc", "value"), &encoded);

  minikv::WalRecord decoded;
  std::size_t consumed = 0;

  // 头部都不完整
  CHECK_FALSE(minikv::DecodeRecord(encoded.substr(0, minikv::kRecordHeaderSize - 1), &decoded,
                                   &consumed));
  // 少最后一个字节
  CHECK_FALSE(minikv::DecodeRecord(encoded.substr(0, encoded.size() - 1), &decoded, &consumed));
}

TEST(WalFormat, RejectsUnknownRecordType) {
  const std::string crafted = CraftRecord(7, 1, 1, "k", "v");

  minikv::WalRecord decoded;
  std::size_t consumed = 0;
  CHECK_FALSE(minikv::DecodeRecord(crafted, &decoded, &consumed));
}

TEST(WalFormat, RejectsOversizedKeyLength) {
  // 声明的 key 长度超过 1KB 上限
  const std::string crafted = CraftRecord(static_cast<std::uint8_t>(minikv::RecordType::kPut),
                                          2000, 1, "k", "v");

  minikv::WalRecord decoded;
  std::size_t consumed = 0;
  CHECK_FALSE(minikv::DecodeRecord(crafted, &decoded, &consumed));
}

TEST(WalFormat, DoesNotTouchOutputOnFailure) {
  std::string encoded;
  minikv::EncodeRecord(MakePut("k", "v"), &encoded);
  encoded.back() = static_cast<char>(encoded.back() ^ 0xFF);

  minikv::WalRecord decoded = MakePut("keep-key", "keep-value");
  std::size_t consumed = 0;
  CHECK_FALSE(minikv::DecodeRecord(encoded, &decoded, &consumed));
  CHECK_STREQ(decoded.key, "keep-key");
  CHECK_STREQ(decoded.value, "keep-value");
  CHECK_EQ(consumed, static_cast<std::size_t>(0));
}

TEST(WalFormat, DecodesFirstOfTwoBackToBackRecords) {
  std::string buffer;
  minikv::EncodeRecord(MakePut("a", "1"), &buffer);
  const std::size_t first_size = buffer.size();
  minikv::EncodeRecord(MakePut("b", "2"), &buffer);

  minikv::WalRecord decoded;
  std::size_t consumed = 0;
  CHECK_TRUE(minikv::DecodeRecord(buffer, &decoded, &consumed));
  CHECK_EQ(consumed, first_size);
  CHECK_STREQ(decoded.key, "a");

  const std::string_view remainder(buffer.data() + consumed, buffer.size() - consumed);
  std::size_t second_consumed = 0;
  CHECK_TRUE(minikv::DecodeRecord(remainder, &decoded, &second_consumed));
  CHECK_STREQ(decoded.key, "b");
  CHECK_EQ(second_consumed, remainder.size());
}

TEST(WalFormat, HandlesBinaryKeysAndValues) {
  const std::string key("\0\1\2", 3);
  const std::string value("\xFF\xFE\x00", 3);

  std::string encoded;
  minikv::EncodeRecord(MakePut(key, value), &encoded);

  minikv::WalRecord decoded;
  std::size_t consumed = 0;
  CHECK_TRUE(minikv::DecodeRecord(encoded, &decoded, &consumed));
  CHECK_TRUE(decoded.key == key);
  CHECK_TRUE(decoded.value == value);
}

TEST(WalFormat, HandlesLargeValue) {
  const std::string value(1024 * 1024, 'x');  // 1MB
  minikv::WalRecord source = MakePut("big", value);

  std::string encoded;
  minikv::EncodeRecord(source, &encoded);
  CHECK_EQ(encoded.size(), minikv::kRecordHeaderSize + source.key.size() + value.size());

  minikv::WalRecord decoded;
  std::size_t consumed = 0;
  CHECK_TRUE(minikv::DecodeRecord(encoded, &decoded, &consumed));
  CHECK_EQ(decoded.value.size(), value.size());
  CHECK_TRUE(decoded.value == value);
}
