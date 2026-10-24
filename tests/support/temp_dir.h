#pragma once

// 每个用例独占的临时目录，析构时自动递归删除。
// 目的：测试不污染工作区，用例之间互不干扰。
//
// 容错策略：系统临时目录不可用（不存在、无权限、盘符无效）时，
// 依次回退到「当前工作目录/.minikv-test-tmp」，并通过日志说明原因。
// 调用方应检查 ok()，失败时把 error() 打进断言信息。

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include "minikv/util/logging.h"

namespace minikv {
namespace test {

class TempDir {
 public:
  TempDir() { Initialize(); }

  ~TempDir() {
    if (!ok_) {
      return;
    }
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  bool ok() const { return ok_; }
  const std::string& error() const { return error_; }
  const std::filesystem::path& path() const { return path_; }

  std::filesystem::path operator/(std::string_view child) const {
    return path_ / std::filesystem::path(child);
  }

 private:
  static std::string MakeName() {
    static std::atomic<unsigned> sequence{0};
    const auto stamp = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::ostringstream name;
    name << "minikv-test-" << stamp << "-" << sequence.fetch_add(1);
    return name.str();
  }

  // 读取环境变量。MSVC 的 getenv 会触发 C4996，这里用 _dupenv_s 替代。
  static std::string GetEnv(const char* name) {
#ifdef _WIN32
    char* buffer = nullptr;
    std::size_t length = 0;
    std::string value;
    if (_dupenv_s(&buffer, &length, name) == 0 && buffer != nullptr) {
      value.assign(buffer);
      std::free(buffer);
    }
    return value;
#else
    const char* raw = std::getenv(name);
    return (raw != nullptr) ? std::string(raw) : std::string();
#endif
  }

  void Initialize() {
    std::error_code ec;

    // 优先级：MINIKV_TEST_TMPDIR > 系统临时目录 > 当前工作目录
    std::filesystem::path base;
    const std::string override_dir = GetEnv("MINIKV_TEST_TMPDIR");
    if (!override_dir.empty()) {
      base = std::filesystem::path(override_dir);
      std::filesystem::create_directories(base, ec);
      if (ec) {
        LOG_WARN(std::string("MINIKV_TEST_TMPDIR unusable (") + ec.message() +
                 "), falling back to system temp");
        ec.clear();
        base.clear();
      }
    }

    if (base.empty()) {
      base = std::filesystem::temp_directory_path(ec);
      if (ec || base.empty()) {
        LOG_WARN("temp_directory_path unavailable (" + ec.message() + "), falling back to cwd");
        ec.clear();
        base = std::filesystem::current_path();
      }
    }

    path_ = base / MakeName();
    std::filesystem::create_directories(path_, ec);
    if (!ec) {
      ok_ = std::filesystem::is_directory(path_);
      if (!ok_) {
        error_ = "created path is not a directory: " + path_.string();
      }
      return;
    }

    const std::string first_error = ec.message();
    LOG_WARN("cannot create " + path_.string() + " (" + first_error + "), retrying under cwd");

    std::error_code cwd_ec;
    const std::filesystem::path cwd = std::filesystem::current_path(cwd_ec);
    if (!cwd_ec) {
      std::error_code retry_ec;
      path_ = cwd / ".minikv-test-tmp" / MakeName();
      std::filesystem::create_directories(path_, retry_ec);
      if (!retry_ec) {
        ok_ = std::filesystem::is_directory(path_);
        if (ok_) {
          return;
        }
        error_ = "fallback path is not a directory: " + path_.string();
        return;
      }
      error_ = "temp dir failed: " + first_error + "; fallback failed: " + retry_ec.message();
      return;
    }
    error_ = "temp dir failed: " + first_error + "; cwd unavailable: " + cwd_ec.message();
  }

  std::filesystem::path path_;
  bool ok_ = false;
  std::string error_;
};

}  // namespace test
}  // namespace minikv
