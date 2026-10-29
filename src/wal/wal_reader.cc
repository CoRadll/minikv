#include "minikv/wal/wal_reader.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "minikv/util/file.h"
#include "minikv/util/logging.h"

namespace minikv {

WalReader::WalReader(std::filesystem::path path) : path_(std::move(path)) {}

Status WalReader::Replay(const std::function<void(const WalRecord&)>& on_record,
                         std::uint64_t start_offset) {
  valid_bytes_ = 0;
  dropped_bytes_ = 0;

  std::error_code ec;
  if (!std::filesystem::exists(path_, ec)) {
    if (ec) {
      return Status::IOError("wal exists " + path_.string() + ": " + ec.message());
    }
    // 空库：没有日志可回放
    return Status::Ok();
  }

  File file;
  Status status = file.Open(path_, FileOpenMode::kReadWrite);
  if (!status.ok()) {
    return status;
  }

  const std::uint64_t file_size = file.Size();
  if (start_offset > file_size) {
    return Status::InvalidArgument("wal start offset " + std::to_string(start_offset) +
                                   " exceeds file size " + std::to_string(file_size));
  }

  std::string content;
  status = file.Read(start_offset, static_cast<std::size_t>(file_size - start_offset), &content);
  if (!status.ok()) {
    return status;
  }

  std::size_t offset = 0;
  while (offset < content.size()) {
    WalRecord record;
    std::size_t consumed = 0;
    const std::string_view remaining(content.data() + offset, content.size() - offset);
    if (!DecodeRecord(remaining, &record, &consumed)) {
      break;
    }
    if (on_record) {
      on_record(record);
    }
    offset += consumed;
  }

  const std::uint64_t valid = start_offset + static_cast<std::uint64_t>(offset);
  valid_bytes_ = valid;

  if (valid < file_size) {
    dropped_bytes_ = file_size - valid;
    LOG_WARN("wal truncated at offset " + std::to_string(valid) + ": dropped " +
             std::to_string(dropped_bytes_) + " bytes");

    // 只保留有效前缀并落盘，否则下次启动会再次看到同一段坏数据
    status = file.Truncate(valid);
    if (!status.ok()) {
      return status;
    }
    status = file.Sync();
    if (!status.ok()) {
      return status;
    }
  }

  return Status::Ok();
}

}  // namespace minikv
