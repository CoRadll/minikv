#include "minikv/wal/wal_writer.h"

#include <cstdint>
#include <string>
#include <utility>

namespace minikv {

WalWriter::~WalWriter() { Close(); }

Status WalWriter::Open(const std::filesystem::path& path, bool sync_on_write,
                       std::uint64_t start_offset) {
  Close();

  Status status = file_.Open(path, FileOpenMode::kReadWrite);
  if (!status.ok()) {
    return status;
  }

  const std::uint64_t current_size = file_.Size();
  if (start_offset > current_size) {
    file_.Close();
    return Status::InvalidArgument("wal start offset " + std::to_string(start_offset) +
                                   " exceeds file size " + std::to_string(current_size));
  }

  if (start_offset < current_size) {
    // 只保留有效前缀：坏尾（写了一半的记录）在这里被真正丢弃。
    status = file_.Truncate(start_offset);
    if (!status.ok()) {
      file_.Close();
      return status;
    }
    // 截断本身也要落盘，否则崩溃后坏尾可能重新出现
    status = file_.Sync();
    if (!status.ok()) {
      file_.Close();
      return status;
    }
  }

  sync_on_write_ = sync_on_write;
  size_ = start_offset;
  return Status::Ok();
}

Status WalWriter::Append(const WalRecord& record) {
  if (!file_.is_open()) {
    return Status::Closed("wal writer is not open");
  }

  std::string buffer;
  EncodeRecord(record, &buffer);

  Status status = file_.Append(buffer);
  if (!status.ok()) {
    return status;
  }

  size_ += static_cast<std::uint64_t>(buffer.size());

  if (sync_on_write_) {
    return file_.Sync();
  }
  return Status::Ok();
}

Status WalWriter::Sync() {
  if (!file_.is_open()) {
    return Status::Closed("wal writer is not open");
  }
  return file_.Sync();
}

void WalWriter::Close() {
  file_.Close();
  sync_on_write_ = false;
  size_ = 0;
}

}  // namespace minikv
