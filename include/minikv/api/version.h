#pragma once

#include <string_view>

namespace minikv {

// 版本信息以 header-only 常量提供，没有对应的 .cc 文件。
// 属于「include 与 src 同构」规则里允许的「纯定义」例外，
// 例外名单登记在 scripts/check_layout.ps1。
inline constexpr std::string_view kProjectName = "minikv";
inline constexpr std::string_view kVersion = "0.1.1";

}  // namespace minikv
