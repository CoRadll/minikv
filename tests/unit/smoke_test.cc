// 框架与 TempDir 的最小可用示例。真正的用例在后续任务里逐层加入。
//
// 运行方式：
//   minikv_tests.exe                  跑全部用例
//   minikv_tests.exe --list           列出用例
//   minikv_tests.exe --filter=Smoke   只跑名字含 Smoke 的用例
//   minikv_tests.exe --verify-harness 框架自检

#include <filesystem>
#include <fstream>

#include "minikv/api/version.h"

#include "support/temp_dir.h"
#include "support/test_framework.h"

TEST(Smoke, VersionNotEmpty) {
  CHECK_STREQ(minikv::kProjectName, "minikv");
  CHECK_FALSE(minikv::kVersion.empty());

  // 只校验版本号形状（主.次.修订），不锁死具体数字：
  // 否则每次升版本都要顺手改测试，反而掩盖真实失败。
  int dots = 0;
  bool digits_only = true;
  for (const char c : minikv::kVersion) {
    if (c == '.') {
      ++dots;
    } else if (c < '0' || c > '9') {
      digits_only = false;
    }
  }
  CHECK_EQ(dots, 2);
  CHECK_TRUE(digits_only);
}

TEST(Smoke, TempDirIsolated) {
  minikv::test::TempDir dir;

  // 目录建不出来时把原因带进断言，而不是只报 "exists() 为 false"。
  if (!dir.ok()) {
    ::minikv::test::Fail(__FILE__, __LINE__, "TempDir unavailable: " + dir.error());
    return;
  }

  CHECK_TRUE(std::filesystem::exists(dir.path()));

  const auto probe = dir.path() / "probe.txt";
  {
    std::ofstream out(probe);
    REQUIRE_TRUE(out.good());
    out << "hello";
  }
  CHECK_TRUE(std::filesystem::exists(probe));
  CHECK_TRUE(std::filesystem::is_directory(dir.path()));
}
