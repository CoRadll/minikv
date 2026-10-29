#include <string>
#include <vector>

#include "minikv/util/logging.h"

#include "support/test_framework.h"

namespace {

// 捕获日志输出，析构时恢复进入前的设置，避免用例之间互相污染。
class LogCapture {
 public:
  LogCapture() : previous_level_(minikv::GetLogLevel()) {
    minikv::SetLogSink([this](const std::string& line) { lines.push_back(line); });
  }

  ~LogCapture() {
    minikv::SetLogSink(minikv::LogSink{});
    minikv::SetLogLevel(previous_level_);
  }

  LogCapture(const LogCapture&) = delete;
  LogCapture& operator=(const LogCapture&) = delete;

  std::vector<std::string> lines;

 private:
  minikv::LogLevel previous_level_;
};

bool Contains(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

}  // namespace

TEST(Logging, LevelFilterSuppressesLowerLevels) {
  LogCapture capture;
  minikv::SetLogLevel(minikv::LogLevel::kWarn);

  LOG_DEBUG("dropped");
  LOG_INFO("dropped");
  LOG_WARN("kept");
  LOG_ERROR("kept");

  CHECK_EQ(capture.lines.size(), static_cast<std::size_t>(2));
  CHECK_TRUE(Contains(capture.lines[0], "WARN"));
  CHECK_TRUE(Contains(capture.lines[1], "ERROR"));
}

TEST(Logging, LineCarriesLevelLocationAndMessage) {
  LogCapture capture;
  minikv::SetLogLevel(minikv::LogLevel::kDebug);

  LOG_ERROR("disk is full");

  REQUIRE_TRUE(capture.lines.size() == 1);
  const std::string& line = capture.lines[0];
  CHECK_TRUE(Contains(line, "[ERROR]"));
  CHECK_TRUE(Contains(line, "logging_test.cc"));
  CHECK_TRUE(Contains(line, "disk is full\n"));
  CHECK_TRUE(line.front() == '[');
  CHECK_TRUE(line.find("] [") != std::string::npos);
}

TEST(Logging, LevelCanBeReadBack) {
  const minikv::LogLevel saved = minikv::GetLogLevel();

  minikv::SetLogLevel(minikv::LogLevel::kError);
  CHECK_EQ(minikv::GetLogLevel(), minikv::LogLevel::kError);

  minikv::SetLogLevel(minikv::LogLevel::kInfo);
  CHECK_EQ(minikv::GetLogLevel(), minikv::LogLevel::kInfo);

  // 恢复进入前的级别，避免影响后续用例（测试默认是 kError）
  minikv::SetLogLevel(saved);
}
