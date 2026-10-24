#include "minikv/util/logging.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <utility>

namespace minikv {
namespace {

std::atomic<LogLevel> g_level{LogLevel::kInfo};
std::mutex g_mutex;
LogSink g_sink;
bool g_has_sink = false;

const char* LevelName(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug:
      return "DEBUG";
    case LogLevel::kInfo:
      return "INFO";
    case LogLevel::kWarn:
      return "WARN";
    case LogLevel::kError:
      return "ERROR";
  }
  return "UNKNOWN";
}

std::string FormatTimestamp() {
  using Clock = std::chrono::system_clock;
  const auto now = Clock::now();
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
  const std::time_t seconds = Clock::to_time_t(now);

  std::tm local_time{};
#ifdef _WIN32
  localtime_s(&local_time, &seconds);
#else
  localtime_r(&seconds, &local_time);
#endif

  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                local_time.tm_year + 1900, local_time.tm_mon + 1, local_time.tm_mday,
                local_time.tm_hour, local_time.tm_min, local_time.tm_sec,
                static_cast<int>(millis.count()));
  return std::string(buffer);
}

}  // namespace

void SetLogLevel(LogLevel level) { g_level.store(level, std::memory_order_relaxed); }

LogLevel GetLogLevel() { return g_level.load(std::memory_order_relaxed); }

void SetLogSink(LogSink sink) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_has_sink = static_cast<bool>(sink);
  g_sink = std::move(sink);
}

void LogMessage(LogLevel level, const char* file, int line, const std::string& message) {
  if (static_cast<int>(level) < static_cast<int>(GetLogLevel())) {
    return;
  }

  std::string text;
  text.reserve(message.size() + 64);
  text += '[';
  text += FormatTimestamp();
  text += "] [";
  text += LevelName(level);
  text += "] ";
  text += (file != nullptr) ? file : "?";
  text += ':';
  text += std::to_string(line);
  text += ' ';
  text += message;
  text += '\n';

  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_has_sink) {
    g_sink(text);
    return;
  }
  std::fputs(text.c_str(), stderr);
  std::fflush(stderr);
}

}  // namespace minikv
