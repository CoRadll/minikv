// S0-T1 占位程序：只验证构建链路是否打通。
// S1-T8 会把它替换成真正的交互式 REPL（put / get / del / stats）。

#include <cstdio>
 
#include "minikv/api/version.h"

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  std::printf("%.*s %.*s\n", static_cast<int>(minikv::kProjectName.size()),
              minikv::kProjectName.data(), static_cast<int>(minikv::kVersion.size()),
              minikv::kVersion.data());
  std::printf("usage: minikv --dir <path>\n");
  return 0;
}
