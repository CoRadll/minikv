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
# MSVC 默认按系统代码页解析（简体中文环境是 936/GBK），会报 C4819
# 并可能把后续声明一起误解码。该选项由 cmake/Warnings.cmake 统一管理。
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(Warnings)
include(Sanitizers)

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
# 源文件按层排列：util -> wal -> memtable -> table -> db -> cache -> api
# 头文件全部位于 include/minikv/，与 src/ 逐文件同构。
# ---------------------------------------------------------------------------
add_library(minikv_core STATIC
  # util：基础设施
  src/util/status.cc
  src/util/crc32c.cc
  src/util/coding.cc
  src/util/file.cc
  src/util/env.cc
  src/util/logging.cc
  src/util/random.cc
  src/util/arena.cc
  # wal：预写日志
  src/wal/wal_format.cc
  src/wal/wal_writer.cc
  src/wal/wal_reader.cc
  # memtable：有序内存表
  src/memtable/memtable.cc
  src/memtable/skiplist.cc
  # table：SSTable 与过滤器
  src/table/block.cc
  src/table/block_builder.cc
  src/table/format.cc
  src/table/table_builder.cc
  src/table/table_reader.cc
  src/table/bloom.cc
  src/table/merging_iterator.cc
  # db：版本、合并、扫描
  src/db/manifest.cc
  src/db/compaction.cc
  src/db/db_iterator.cc
  # cache：块缓存
  src/cache/lru_cache.cc
  # api：对外门面
  src/api/engine.cc)

target_include_directories(minikv_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(minikv_core PUBLIC cxx_std_20)

# 警告与 sanitizer 均以 PRIVATE 方式施加：只影响本项目构建，
# 不会通过 PUBLIC/INTERFACE 传播给链接 minikv_core 的下游项目。
minikv_set_warnings(minikv_core)
minikv_set_sanitizers(minikv_core)

# S0-T2 阶段所有实现文件都还是空壳，归档时每个空翻译单元会产生一条
# LNK4221（无公开符号）。这是临时豁免，等各模块落地后删除。
if (MSVC)
  set_target_properties(minikv_core PROPERTIES STATIC_LIBRARY_FLAGS "/ignore:4221")
endif()

# ---------------------------------------------------------------------------
# 头文件自包含检查
#
# 每个公开头文件单独生成一个翻译单元并编译。任何"依赖其他头间接包含"的
# 情况都会在这里暴露（例如 skiplist.h 用了 std::string 却忘了 #include <string>，
# 旧版 MSVC 的 <string_view> 恰好间接包含 <string>，CI 换了新版才炸）。
# ---------------------------------------------------------------------------
file(GLOB_RECURSE MINIKV_PUBLIC_HEADERS CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/include/minikv/*.h)

set(MINIKV_HEADER_CHECK_SOURCES "")
set(MINIKV_HEADER_CHECK_DIR ${CMAKE_CURRENT_BINARY_DIR}/header_check)
file(MAKE_DIRECTORY ${MINIKV_HEADER_CHECK_DIR})

foreach(header IN LISTS MINIKV_PUBLIC_HEADERS)
  file(RELATIVE_PATH header_rel ${CMAKE_CURRENT_SOURCE_DIR}/include ${header})
  string(REPLACE "/" "_" header_flat ${header_rel})
  string(REPLACE "." "_" header_flat ${header_flat})
  set(header_src ${MINIKV_HEADER_CHECK_DIR}/${header_flat}.cc)
  file(GENERATE OUTPUT ${header_src} CONTENT "#include \"${header_rel}\"\n")
  list(APPEND MINIKV_HEADER_CHECK_SOURCES ${header_src})
endforeach()

add_library(minikv_header_check OBJECT ${MINIKV_HEADER_CHECK_SOURCES})
target_include_directories(minikv_header_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
minikv_set_warnings(minikv_header_check)
minikv_set_sanitizers(minikv_header_check)

# ---------------------------------------------------------------------------
# 命令行程序
# ---------------------------------------------------------------------------
add_executable(minikv
  src/tools/minikv_cli.cc
  src/tools/minikv_bench.cc
  src/tools/minikv_demo.cc)
target_link_libraries(minikv PRIVATE minikv_core)

# 作为库使用的示例程序（不影响其他目标）
add_executable(minikv_example examples/embedded_demo.cc)
target_link_libraries(minikv_example PRIVATE minikv_core)
minikv_set_warnings(minikv_example)
minikv_set_warnings(minikv)
minikv_set_sanitizers(minikv)

# ---------------------------------------------------------------------------
# 测试
# ---------------------------------------------------------------------------
enable_testing()

add_executable(minikv_tests
  tests/support/test_main.cpp
  tests/unit/smoke_test.cc
  tests/unit/util/status_test.cc
  tests/unit/util/logging_test.cc
  tests/unit/util/crc32c_test.cc
  tests/unit/util/coding_test.cc
  tests/unit/util/file_test.cc
  tests/unit/util/arena_test.cc
  tests/unit/util/env_test.cc
  tests/unit/wal/wal_format_test.cc
  tests/unit/wal/wal_writer_test.cc
  tests/unit/wal/wal_reader_test.cc
  tests/unit/memtable/memtable_test.cc
  tests/unit/memtable/skiplist_test.cc
  tests/unit/db/engine_test.cc
  tests/unit/db/flush_test.cc
  tests/unit/db/compaction_test.cc
  tests/unit/db/scan_test.cc
  tests/unit/cache/lru_cache_test.cc
  tests/integration/recovery_test.cc
  tests/integration/crash_boundary_test.cc
  tests/integration/multi_file_test.cc
  tests/unit/table/block_test.cc
  tests/unit/table/table_builder_test.cc
  tests/unit/table/table_reader_test.cc
  tests/unit/table/bloom_test.cc
  tests/unit/table/table_iterator_test.cc
  tests/unit/table/merging_iterator_test.cc)
target_link_libraries(minikv_tests PRIVATE minikv_core)
# 让测试可以用 "support/test_framework.h" 这样的路径引用测试基建
target_include_directories(minikv_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
minikv_set_warnings(minikv_tests)
minikv_set_sanitizers(minikv_tests)

add_test(NAME smoke COMMAND minikv_tests)
