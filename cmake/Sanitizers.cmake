# AddressSanitizer 开关。
#
# 默认关闭。开启后只影响本项目 target，不会改变库接口，
# 也不会把 sanitizer 运行时塞给链接 minikv_core 的下游使用者。
#
# 前置条件（MSVC）：Visual Studio Installer 中勾选
#   「使用 C++ 的桌面开发」→ 单个组件 → C++ AddressSanitizer
# 未安装时链接阶段会报 LNK1104: 无法打开 clang_rt.asan_*.lib。

option(MINIKV_ENABLE_ASAN "启用 AddressSanitizer（Debug 排查内存问题时使用）" OFF)

function(minikv_set_sanitizers target)
  if (NOT MINIKV_ENABLE_ASAN)
    return()
  endif()

  if (MSVC)
    # MSVC 的 ASan 是纯编译期开关，运行时会通过编译单元里的指令自动参与链接。
    # 不能把 /fsanitize=address 加给链接器：link.exe 不识别该选项（LNK4044）。
    target_compile_options(${target} PRIVATE /fsanitize=address)
    message(STATUS "AddressSanitizer 已启用：需要 VS 的「C++ AddressSanitizer」组件")
  elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    # 静态库没有链接步骤，链接选项只加在可执行文件/动态库上。
    get_target_property(target_type ${target} TYPE)
    if (NOT target_type STREQUAL "STATIC_LIBRARY")
      target_link_options(${target} PRIVATE -fsanitize=address)
    endif()
  else()
    message(WARNING "MINIKV_ENABLE_ASAN: 当前编译器不支持，已忽略")
  endif()
endfunction()
