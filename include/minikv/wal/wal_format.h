#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace minikv {

enum class RecordType : std::uint8_t {
  kPut = 1,
  kDelete = 2,
};

// 一条日志记录。删除也写成一条记录（墓碑），不做原地删除。
struct WalRecord {
  RecordType type = RecordType::kPut;
  std::string key;
  std::string value;
};

// 记录布局（小端）：
//   [crc32: 4][type: 1][key_len: 4][value_len: 4][key][value]
// crc32 覆盖 type 及其之后的全部字节。
inline constexpr std::size_t kRecordHeaderSize = 13;

// 单条记录的字段上限（格式层与 Engine 层共用）。
// key 要进内存索引，value 不能无限大以免一次写入吃掉全部内存。
inline constexpr std::size_t kMaxKeySize = 1u << 10;      // 1 KB
inline constexpr std::size_t kMaxValueSize = 64u << 20;   // 64 MB

void EncodeRecord(const WalRecord& record, std::string* dst);

// 解析一条记录。成功返回 true 并写入 *consumed（本条记录占用的字节数）；
// 失败返回 false，通常意味着写入被截断或 CRC 不匹配。
bool DecodeRecord(std::string_view input, WalRecord* record, std::size_t* consumed);

}  // namespace minikv
