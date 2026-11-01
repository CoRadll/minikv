#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace minikv {

// SSTable 文件尾部的固定布局（48 字节），见 docs/architecture.md 8.3。
//
//   [magic: 8][version: 4][reserved: 4]
//   [index_offset: 8][index_size: 8][filter_offset: 8][filter_size: 8]
inline constexpr std::string_view kTableMagic = "MINIKVS2";
inline constexpr std::uint32_t kTableVersion = 1;
inline constexpr std::size_t kTableFooterSize = 48;

struct TableFooter {
  std::uint64_t index_offset = 0;
  std::uint64_t index_size = 0;
  std::uint64_t filter_offset = 0;  // S4 才填，S2/S3 为 0
  std::uint64_t filter_size = 0;
};

std::string EncodeFooter(const TableFooter& footer);

// tail 为文件末尾 kTableFooterSize 字节。magic/version 不符返回 false。
bool DecodeFooter(std::string_view tail, TableFooter* footer);

}  // namespace minikv
