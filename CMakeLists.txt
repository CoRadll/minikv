cmake_minimum_required(VERSION 3.20)

project(minikv
  VERSION 0.1.0
  DESCRIPTION "A from-scratch LSM-Tree key-value storage engine"
  LANGUAGES CXX)

# ---------------------------------------------------------------------------
# 语言标准
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 源文件统一 UTF-8 编码，且含中文注释。
# MSVC 默认按系统代码页（简体中文环境是 936/GBK）解析，会报 C4819
# 并可能把后续声明一起误解码，因此必须显式指定 /utf-8。
if (MSVC)
  add_compile_options(/utf-8)
endif()

# ---------------------------------------------------------------------------
# 产物统一输出到 build/bin 与 build/lib
# 多配置生成器（VS）会在其下再追加 /Debug 或 /Release
# ---------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---------------------------------------------------------------------------
# 核心库
#
# S0-T1 阶段先用 INTERFACE 占位：它只传递编译选项、不产生二进制，
# 因此允许没有任何源文件，用来先验证构建链路。
# S0-T2 会把它换成 STATIC 静态库，并挂上真实的 .cc 源文件。
# ---------------------------------------------------------------------------
add_library(minikv_core INTERFACE)
target_include_directories(minikv_core
  INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(minikv_core INTERFACE cxx_std_20)

# ---------------------------------------------------------------------------
# 命令行程序
# ---------------------------------------------------------------------------
add_executable(minikv src/tools/minikv_cli.cc)
target_link_libraries(minikv PRIVATE minikv_core)

# ---------------------------------------------------------------------------
# 测试
# ---------------------------------------------------------------------------
enable_testing()

add_executable(minikv_tests tests/unit/smoke_test.cc)
target_link_libraries(minikv_tests PRIVATE minikv_core)

add_test(NAME smoke COMMAND minikv_tests)
