#pragma once

#include <cstdint>
#include <string_view>

namespace minikv {

// CRC32C（Castagnoli 多项式），表驱动实现。
// 用于 WAL 记录与 SSTable 块的完整性校验：能发现数据被截断或写坏。
uint32_t Crc32c(std::string_view data);

// 在已有校验值上继续累加，便于分块计算。
uint32_t Crc32cExtend(uint32_t crc, std::string_view data);

}  // namespace minikv
