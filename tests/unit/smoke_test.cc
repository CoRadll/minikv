// S0-T1 占位测试：验证「可执行测试 -> ctest」这条链路是通的。
// S0-T2 会换成 tests/support/test_framework.h 驱动的正式用例。

#include <cstdio>

#include "minikv/api/version.h"

int main() {
  if (minikv::kVersion.empty()) {
    std::printf("FAIL: version string is empty\n");
    return 1;
  }
  std::printf("smoke ok (%.*s)\n", static_cast<int>(minikv::kVersion.size()),
              minikv::kVersion.data());
  return 0;
}
