#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "support/test_framework.h"

namespace minikv {
namespace test {
namespace {

struct HarnessState {
  bool quiet = false;
  bool case_failed = false;
  bool header_printed = false;
  int failures = 0;
  int cases_run = 0;
  int cases_passed = 0;
  int cases_skipped = 0;
  std::string current_name;
};

HarnessState& State() {
  static HarnessState state;
  return state;
}

std::string ToLower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

bool MatchesFilter(std::string_view filter, std::string_view full_name) {
  if (filter.empty()) {
    return true;
  }
  return ToLower(full_name).find(ToLower(filter)) != std::string::npos;
}

void PrintIndented(const std::string& message) {
  std::istringstream stream(message);
  std::string line;
  while (std::getline(stream, line)) {
    std::printf("         %s\n", line.c_str());
  }
}

bool g_reached_after_require = false;

void RequireProbe() {
  g_reached_after_require = false;
  REQUIRE_TRUE(false);
  g_reached_after_require = true;
}

}  // namespace

std::vector<TestCase>& Registry() {
  static std::vector<TestCase> registry;
  return registry;
}

void Fail(const char* file, int line, const std::string& message) {
  HarnessState& s = State();
  s.case_failed = true;
  ++s.failures;
  if (s.quiet) {
    return;
  }
  if (!s.header_printed) {
    std::printf("[ FAIL ] %s\n", s.current_name.c_str());
    s.header_printed = true;
  }
  std::printf("         %s:%d\n", file, line);
  PrintIndented(message);
  std::fflush(stdout);
}

bool CaseFailed() { return State().case_failed; }

void BeginCase(std::string_view full_name) {
  HarnessState& s = State();
  s.current_name = std::string(full_name);
  s.case_failed = false;
  s.header_printed = false;
  if (!s.quiet) {
    std::printf("[ RUN  ] %s\n", s.current_name.c_str());
    std::fflush(stdout);
  }
}

void EndCase() {
  HarnessState& s = State();
  ++s.cases_run;
  if (!s.case_failed) {
    ++s.cases_passed;
  }
}

int RunAll(int argc, char** argv) {
  std::string filter;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--help" || arg == "-h") {
      std::printf("usage: minikv_tests [--list] [--filter=<substr>] [--verify-harness]\n");
      return 0;
    }
    if (arg == "--list") {
      for (const TestCase& test_case : Registry()) {
        std::printf("%s.%s\n", std::string(test_case.suite).c_str(),
                    std::string(test_case.name).c_str());
      }
      return 0;
    }
    if (arg == "--verify-harness") {
      return VerifyHarness();
    }
    constexpr std::string_view kFilterPrefix = "--filter=";
    if (arg.substr(0, kFilterPrefix.size()) == kFilterPrefix) {
      filter = std::string(arg.substr(kFilterPrefix.size()));
      continue;
    }
    std::printf("unknown argument: %s\n", std::string(arg).c_str());
    return 2;
  }

  const auto start = std::chrono::steady_clock::now();
  for (const TestCase& test_case : Registry()) {
    const std::string full_name =
        std::string(test_case.suite) + "." + std::string(test_case.name);
    if (!MatchesFilter(filter, full_name)) {
      ++State().cases_skipped;
      continue;
    }

    BeginCase(full_name);
    const auto case_start = std::chrono::steady_clock::now();
    test_case.fn();
    const auto case_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - case_start)
                             .count();
    if (!State().case_failed) {
      std::printf("[  OK  ] %s (%lld ms)\n", full_name.c_str(),
                  static_cast<long long>(case_ms));
      std::fflush(stdout);
    }
    EndCase();
  }

  const auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count();
  std::printf("[ SUMMARY ] %d passed, %d failed, %d skipped (%lld ms)\n", State().cases_passed,
              State().failures, State().cases_skipped, static_cast<long long>(total_ms));
  return State().failures == 0 ? 0 : 1;
}

int VerifyHarness() {
  const HarnessState saved = State();
  int total = 0;
  int passed = 0;
  std::string report;

  auto record = [&](bool ok, const char* what) {
    ++total;
    if (ok) {
      ++passed;
    }
    report += std::string(ok ? "  [ok]   " : "  [BAD]  ") + what + "\n";
  };

  State() = HarnessState{};
  State().quiet = true;

  // 1. 不相等的值必须被记录为失败
  BeginCase("harness/CheckEqMismatch");
  CheckEq(1, 2, "CheckEq(1, 2)", __FILE__, __LINE__);
  record(State().case_failed, "CHECK_EQ records a mismatch");
  EndCase();

  // 2. 相等的值不得误报
  BeginCase("harness/CheckEqEqual");
  CheckEq(7, 7, "CheckEq(7, 7)", __FILE__, __LINE__);
  record(!State().case_failed, "CHECK_EQ accepts equal values");
  EndCase();

  // 3. REQUIRE 必须中断当前用例
  BeginCase("harness/RequireStops");
  RequireProbe();
  record(!g_reached_after_require, "REQUIRE stops execution");
  EndCase();

  // 4. 过滤器必须大小写不敏感且只匹配子串
  record(MatchesFilter("wal", "Wal.RoundTrip") && !MatchesFilter("wal", "MemTable.Put"),
         "filter matches case-insensitively");

  State() = saved;

  std::printf("harness self-check:\n%s", report.c_str());
  std::printf("harness %s: %d/%d\n", passed == total ? "OK" : "FAILED", passed, total);
  return passed == total ? 0 : 1;
}

}  // namespace test
}  // namespace minikv

int main(int argc, char** argv) { return ::minikv::test::RunAll(argc, argv); }
