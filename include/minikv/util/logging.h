#pragma once

#include <functional>
#include <string>

namespace minikv {

enum class LogLevel {
  kDebug = 0,
  kInfo,
  kWarn,
  kError,
};

// 日志落点。默认写 stderr；测试可注入自己的 sink 来断言输出内容。
using LogSink = std::function<void(const std::string& line)>;

void SetLogLevel(LogLevel level);
LogLevel GetLogLevel();
void SetLogSink(LogSink sink);  // 传空函数对象表示恢复默认的 stderr 输出

void LogMessage(LogLevel level, const char* file, int line, const std::string& message);

}  // namespace minikv

#define MINIKV_LOG(level, msg) ::minikv::LogMessage((level), __FILE__, __LINE__, (msg))
#define LOG_DEBUG(msg) MINIKV_LOG(::minikv::LogLevel::kDebug, msg)
#define LOG_INFO(msg) MINIKV_LOG(::minikv::LogLevel::kInfo, msg)
#define LOG_WARN(msg) MINIKV_LOG(::minikv::LogLevel::kWarn, msg)
#define LOG_ERROR(msg) MINIKV_LOG(::minikv::LogLevel::kError, msg)
