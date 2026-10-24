#pragma once

// 极简测试框架：零第三方依赖，不使用异常（与项目 D-002 一致）。
//
// 用法：
//   TEST(Suite, Name) {
//     CHECK_EQ(1 + 1, 2);
//   }
//
// 断言分两级：
//   CHECK_*   失败后继续执行本用例剩余检查
//   REQUIRE_* 失败后立即结束本用例（用于前置条件）

#include <chrono>
#include <cstdio>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "minikv/util/status.h"

namespace minikv {
namespace test {

namespace detail {

template <typename T, typename = void>
struct IsStreamable : std::false_type {};

template <typename T>
struct IsStreamable<T,
                    std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>>
    : std::true_type {};

// 枚举无法直接输出，打印其底层整数值；其余不可流化的类型给占位符，
// 保证 CHECK_EQ 对任意可比较类型都能编译。
template <typename T>
void PrintValue(std::ostringstream& out, const T& value) {
  if constexpr (std::is_enum<std::remove_cv_t<std::remove_reference_t<T>>>::value) {
    out << static_cast<long long>(value);
  } else if constexpr (IsStreamable<T>::value) {
    out << value;
  } else {
    out << "<unprintable>";
  }
}

}  // namespace detail

struct TestCase {
  std::string_view suite;
  std::string_view name;
  void (*fn)();
};

std::vector<TestCase>& Registry();

struct Registrar {
  Registrar(std::string_view suite, std::string_view name, void (*fn)()) {
    Registry().push_back(TestCase{suite, name, fn});
  }
};

// 记录一次失败。由断言宏调用，不要在测试里直接调用。
void Fail(const char* file, int line, const std::string& message);

bool CaseFailed();
void BeginCase(std::string_view full_name);
void EndCase();

template <typename Expected, typename Actual>
void CheckEq(const Expected& expected, const Actual& actual, const char* expr, const char* file,
             int line) {
  if (!(expected == actual)) {
    std::ostringstream oss;
    oss << expr << " failed\n  expected: ";
    detail::PrintValue(oss, expected);
    oss << "\n  actual:   ";
    detail::PrintValue(oss, actual);
    Fail(file, line, oss.str());
  }
}

inline void CheckStrEq(std::string_view expected, std::string_view actual, const char* expr,
                       const char* file, int line) {
  if (expected != actual) {
    std::ostringstream oss;
    oss << expr << " failed\n  expected: \"" << expected << "\"\n  actual:   \"" << actual << "\"";
    Fail(file, line, oss.str());
  }
}

inline void CheckOk(const Status& status, const char* expr, const char* file, int line) {
  if (!status.ok()) {
    std::ostringstream oss;
    oss << expr << " failed: " << status.ToString();
    Fail(file, line, oss.str());
  }
}

// 运行器入口：处理 --list / --filter= / --verify-harness / --help。
int RunAll(int argc, char** argv);

// 框架自检：验证断言、REQUIRE 中断、过滤器三项确实按预期工作。
int VerifyHarness();

}  // namespace test
}  // namespace minikv

#define MINIKV_TEST_CASE(suite, name)                                              \
  static void minikv_test_##suite##_##name();                                      \
  static ::minikv::test::Registrar minikv_test_registrar_##suite##_##name(         \
      #suite, #name, &minikv_test_##suite##_##name);                               \
  static void minikv_test_##suite##_##name()

#define TEST(suite, name) MINIKV_TEST_CASE(suite, name)

#define CHECK_TRUE(cond)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      ::minikv::test::Fail(__FILE__, __LINE__, "CHECK_TRUE(" #cond ") failed"); \
    }                                                                          \
  } while (0)

#define CHECK_FALSE(cond)                                                       \
  do {                                                                          \
    if ((cond)) {                                                               \
      ::minikv::test::Fail(__FILE__, __LINE__, "CHECK_FALSE(" #cond ") failed"); \
    }                                                                           \
  } while (0)

#define CHECK_EQ(expected, actual)                                                 \
  ::minikv::test::CheckEq((expected), (actual), "CHECK_EQ(" #expected ", " #actual ")", \
                          __FILE__, __LINE__)

#define CHECK_STREQ(expected, actual)                                                  \
  ::minikv::test::CheckStrEq((expected), (actual), "CHECK_STREQ(" #expected ", " #actual ")", \
                             __FILE__, __LINE__)

#define CHECK_OK(expr) ::minikv::test::CheckOk((expr), "CHECK_OK(" #expr ")", __FILE__, __LINE__)

#define REQUIRE_TRUE(cond)                                                      \
  do {                                                                          \
    if (!(cond)) {                                                              \
      ::minikv::test::Fail(__FILE__, __LINE__, "REQUIRE_TRUE(" #cond ") failed"); \
      return;                                                                   \
    }                                                                           \
  } while (0)

#define REQUIRE_OK(expr)                                                               \
  do {                                                                                 \
    const ::minikv::Status& minikv_status_ = (expr);                                   \
    if (!minikv_status_.ok()) {                                                        \
      ::minikv::test::Fail(__FILE__, __LINE__,                                         \
                           std::string("REQUIRE_OK(" #expr ") failed: ") +             \
                               minikv_status_.ToString());                             \
      return;                                                                          \
    }                                                                                  \
  } while (0)
