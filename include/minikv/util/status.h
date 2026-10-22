#pragma once

#include <string>
#include <utility>

namespace minikv {

// 全项目统一错误模型。不使用异常，所有可失败操作都返回 Status。
enum class StatusCode {
  kOk = 0,
  kNotFound,
  kIOError,
  kCorruption,
  kInvalidArgument,
  kNotSupported,
  kClosed,
};

class Status {
 public:
  Status() = default;

  static Status Ok() { return Status(); }
  static Status NotFound(std::string message = {});
  static Status IOError(std::string message);
  static Status Corruption(std::string message);
  static Status InvalidArgument(std::string message);
  static Status NotSupported(std::string message);
  static Status Closed(std::string message);

  bool ok() const { return code_ == StatusCode::kOk; }
  bool IsNotFound() const { return code_ == StatusCode::kNotFound; }
  StatusCode code() const { return code_; }
  const std::string& message() const { return message_; }

  // 形如 "NotFound: xxx"，用于日志与错误输出。
  std::string ToString() const;

 private:
  Status(StatusCode code, std::string message) : code_(code), message_(std::move(message)) {}

  StatusCode code_ = StatusCode::kOk;
  std::string message_;
};

}  // namespace minikv
