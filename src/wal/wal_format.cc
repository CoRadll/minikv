#include "minikv/wal/wal_format.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "minikv/util/coding.h"
#include "minikv/util/crc32c.h"

namespace minikv {
namespace {

constexpr std::size_t kCrcSize = 4;
constexpr std::size_t kTypeOffset = 4;
constexpr std::size_t kKeyLengthOffset = 5;
constexpr std::size_t kValueLengthOffset = 9;

// 与 Options 层的限制保持一致：key 1KB、value 64MB。
// 在格式层先挡住，避免坏数据把上层拖进巨大的内存分配。
constexpr std::size_t kMaxKeySize = 1u << 10;
constexpr std::size_t kMaxValueSize = 64u << 20;

bool IsValidType(std::uint8_t type) {
  return type == static_cast<std::uint8_t>(RecordType::kPut) ||
         type == static_cast<std::uint8_t>(RecordType::kDelete);
}

}  // namespace

void EncodeRecord(const WalRecord& record, std::string* dst) {
  if (dst == nullptr) {
    return;
  }

  // body = [type][key_len][value_len][key][value]，CRC 覆盖整个 body
  std::string body;
  body.reserve(kRecordHeaderSize - kCrcSize + record.key.size() + record.value.size());
  body.push_back(static_cast<char>(record.type));
  PutFixed32(&body, static_cast<std::uint32_t>(record.key.size()));
  PutFixed32(&body, static_cast<std::uint32_t>(record.value.size()));
  body.append(record.key);
  body.append(record.value);

  PutFixed32(dst, Crc32c(body));
  dst->append(body);
}

bool DecodeRecord(std::string_view input, WalRecord* record, std::size_t* consumed) {
  if (record == nullptr || consumed == nullptr) {
    return false;
  }
  if (input.size() < kRecordHeaderSize) {
    return false;
  }

  std::string_view header = input;
  std::uint32_t stored_crc = 0;
  if (!GetFixed32(&header, &stored_crc)) {
    return false;
  }

  const auto raw_type = static_cast<std::uint8_t>(input[kTypeOffset]);
  if (!IsValidType(raw_type)) {
    return false;
  }

  std::string_view lengths = input.substr(kKeyLengthOffset, 8);
  std::uint32_t key_length = 0;
  std::uint32_t value_length = 0;
  if (!GetFixed32(&lengths, &key_length) || !GetFixed32(&lengths, &value_length)) {
    return false;
  }

  if (key_length > kMaxKeySize || value_length > kMaxValueSize) {
    return false;
  }

  const std::size_t total = kRecordHeaderSize + static_cast<std::size_t>(key_length) +
                            static_cast<std::size_t>(value_length);
  if (total > input.size()) {
    return false;
  }

  const std::string_view body = input.substr(kTypeOffset, total - kCrcSize);
  if (Crc32c(body) != stored_crc) {
    return false;
  }

  const std::string_view key = input.substr(kRecordHeaderSize, key_length);
  const std::string_view value =
      input.substr(kRecordHeaderSize + static_cast<std::size_t>(key_length), value_length);

  // 全部校验通过后才写入输出，保证失败时调用方的数据不变
  WalRecord decoded;
  decoded.type = static_cast<RecordType>(raw_type);
  decoded.key.assign(key.data(), key.size());
  decoded.value.assign(value.data(), value.size());

  *record = std::move(decoded);
  *consumed = total;
  return true;
}

}  // namespace minikv
