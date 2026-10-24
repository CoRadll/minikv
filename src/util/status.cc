#include "minikv/util/status.h"

#include <utility>

namespace minikv {
namespace {

const char* CodeName(StatusCode code) {
  switch (code) {
    case StatusCode::kOk:
      return "OK";
    case StatusCode::kNotFound:
      return "NotFound";
    case StatusCode::kIOError:
      return "IOError";
    case StatusCode::kCorruption:
      return "Corruption";
    case StatusCode::kInvalidArgument:
      return "InvalidArgument";
    case StatusCode::kNotSupported:
      return "NotSupported";
    case StatusCode::kClosed:
      return "Closed";
  }
  return "Unknown";
}

}  // namespace

std::string Status::ToString() const {
  if (code_ == StatusCode::kOk) {
    return "OK";
  }
  std::string result(CodeName(code_));
  if (!message_.empty()) {
    result += ": ";
    result += message_;
  }
  return result;
}

Status Status::NotFound(std::string message) {
  return Status(StatusCode::kNotFound, std::move(message));
}

Status Status::IOError(std::string message) {
  return Status(StatusCode::kIOError, std::move(message));
}

Status Status::Corruption(std::string message) {
  return Status(StatusCode::kCorruption, std::move(message));
}

Status Status::InvalidArgument(std::string message) {
  return Status(StatusCode::kInvalidArgument, std::move(message));
}

Status Status::NotSupported(std::string message) {
  return Status(StatusCode::kNotSupported, std::move(message));
}

Status Status::Closed(std::string message) {
  return Status(StatusCode::kClosed, std::move(message));
}

}  // namespace minikv
