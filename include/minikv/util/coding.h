#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace minikv {

// 变长整数与定长整数的编解码。
// 磁盘格式里大量出现长度与偏移量，用 varint 存小值可以显著省空间。

void PutFixed32(std::string* dst, uint32_t value);
void PutFixed64(std::string* dst, uint64_t value);
void PutVarint32(std::string* dst, uint32_t value);
void PutVarint64(std::string* dst, uint64_t value);

// 解析成功返回 true 并推进 input；解析失败返回 false，input 不变。
bool GetFixed32(std::string_view* input, uint32_t* value);
bool GetFixed64(std::string_view* input, uint64_t* value);
bool GetVarint32(std::string_view* input, uint32_t* value);
bool GetVarint64(std::string_view* input, uint64_t* value);

}  // namespace minikv
