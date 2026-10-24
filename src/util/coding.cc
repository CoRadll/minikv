#include "minikv/util/coding.h"

#include <cstddef>
#include <cstdint>

namespace minikv {
namespace {

constexpr int kBitsPerByte = 8;
constexpr std::uint32_t kByteMask = 0xFFu;
constexpr std::uint32_t kContinueBit = 0x80u;
constexpr std::uint32_t kValueMask = 0x7Fu;

}  // namespace

void PutFixed32(std::string* dst, std::uint32_t value) {
  char buffer[4];
  for (int i = 0; i < 4; ++i) {
    buffer[i] = static_cast<char>((value >> (kBitsPerByte * i)) & kByteMask);
  }
  dst->append(buffer, sizeof(buffer));
}

void PutFixed64(std::string* dst, std::uint64_t value) {
  char buffer[8];
  for (int i = 0; i < 8; ++i) {
    buffer[i] = static_cast<char>((value >> (kBitsPerByte * i)) & kByteMask);
  }
  dst->append(buffer, sizeof(buffer));
}

void PutVarint32(std::string* dst, std::uint32_t value) {
  while (value >= kContinueBit) {
    dst->push_back(static_cast<char>((value & kValueMask) | kContinueBit));
    value >>= 7;
  }
  dst->push_back(static_cast<char>(value));
}

void PutVarint64(std::string* dst, std::uint64_t value) {
  while (value >= kContinueBit) {
    dst->push_back(static_cast<char>((value & kValueMask) | kContinueBit));
    value >>= 7;
  }
  dst->push_back(static_cast<char>(value));
}

bool GetFixed32(std::string_view* input, std::uint32_t* value) {
  if (input->size() < 4) {
    return false;
  }
  std::uint32_t result = 0;
  for (int i = 0; i < 4; ++i) {
    const auto byte = static_cast<std::uint32_t>(
        static_cast<unsigned char>((*input)[static_cast<std::size_t>(i)]));
    result |= byte << (kBitsPerByte * i);
  }
  input->remove_prefix(4);
  *value = result;
  return true;
}

bool GetFixed64(std::string_view* input, std::uint64_t* value) {
  if (input->size() < 8) {
    return false;
  }
  std::uint64_t result = 0;
  for (int i = 0; i < 8; ++i) {
    const auto byte = static_cast<std::uint64_t>(
        static_cast<unsigned char>((*input)[static_cast<std::size_t>(i)]));
    result |= byte << (kBitsPerByte * i);
  }
  input->remove_prefix(8);
  *value = result;
  return true;
}

bool GetVarint32(std::string_view* input, std::uint32_t* value) {
  const std::string_view data = *input;
  std::uint32_t result = 0;

  for (std::size_t i = 0; i < 5 && i < data.size(); ++i) {
    const auto byte = static_cast<std::uint32_t>(static_cast<unsigned char>(data[i]));
    // 第 5 个字节只允许低 4 位，否则超过 32 位
    if (i == 4 && (byte & kValueMask) > 0x0Fu) {
      return false;
    }
    result |= (byte & kValueMask) << (7 * i);
    if ((byte & kContinueBit) == 0) {
      *value = result;
      input->remove_prefix(i + 1);
      return true;
    }
  }
  return false;
}

bool GetVarint64(std::string_view* input, std::uint64_t* value) {
  const std::string_view data = *input;
  std::uint64_t result = 0;

  for (std::size_t i = 0; i < 10 && i < data.size(); ++i) {
    const auto byte = static_cast<std::uint64_t>(static_cast<unsigned char>(data[i]));
    // 第 10 个字节只允许最低 1 位，否则超过 64 位
    if (i == 9 && (byte & kValueMask) > 0x01u) {
      return false;
    }
    result |= (byte & kValueMask) << (7 * i);
    if ((byte & kContinueBit) == 0) {
      *value = result;
      input->remove_prefix(i + 1);
      return true;
    }
  }
  return false;
}

}  // namespace minikv
