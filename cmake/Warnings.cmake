# 统一编译警告配置。
#
# 设计要点：一律 PRIVATE。
# 如果写成 PUBLIC/INTERFACE，链接 minikv_core 的下游项目会被迫继承这些警告选项，
# 这才是「影响用户」的地方。构建期开关必须留在自己家里。

option(MINIKV_WARNINGS_AS_ERRORS "把编译警告当作错误处理（CI 与本地严格模式可开）" OFF)

function(minikv_set_warnings target)
  if (MSVC)
    target_compile_options(${target} PRIVATE
      /W4                 # 接近 -Wall -Wextra 的等级
      /permissive-        # 关闭 MSVC 的宽松模式，贴近标准
      /utf-8              # 源文件是 UTF-8；默认按 936/GBK 解析会报 C4819
      /Zc:__cplusplus     # 让 __cplusplus 反映真实标准版本
      /Zc:preprocessor    # 使用符合标准的新预处理器
    )
    if (MINIKV_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Wshadow
      -Wnon-virtual-dtor
      -Wold-style-cast
      -Woverloaded-virtual
      -Wformat=2
      -Wunused
    )
    if (MINIKV_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()
