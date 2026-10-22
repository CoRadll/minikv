#pragma once

#include <string>

namespace minikv {

enum class LogLevel {
  kDebug = 0,
  kInfo,
  kWarn,
  kError,
};

void SetLogLevel(LogLevel level);
void LogMessage(LogLevel level, const char* file, int line, const std::string& message);

}  // namespace minikv

#define MINIKV_LOG(level, msg) ::minikv::LogMessage((level), __FILE__, __LINE__, (msg))
#define LOG_DEBUG(msg) MINIKV_LOG(::minikv::LogLevel::kDebug, msg)
#define LOG_INFO(msg) MINIKV_LOG(::minikv::LogLevel::kInfo, msg)
#define LOG_WARN(msg) MINIKV_LOG(::minikv::LogLevel::kWarn, msg)
#define LOG_ERROR(msg) MINIKV_LOG(::minikv::LogLevel::kError, msg)
