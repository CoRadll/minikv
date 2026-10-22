#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "minikv/util/file.h"
#include "minikv/util/status.h"
#include "minikv/wal/wal_format.h"

namespace minikv {

// WAL 写入端：只追加，不修改已有内容。
class WalWriter {
 public:
  WalWriter() = default;
  ~WalWriter();

  // start_offset 用于「截断坏尾之后继续写」的场景。
  Status Open(const std::filesystem::path& path, bool sync_on_write, std::uint64_t start_offset);
  Status Append(const WalRecord& record);
  Status Sync();
  uint64_t size() const { return size_; }
  void Close();

 private:
  File file_;
  bool sync_on_write_ = false;
  std::uint64_t size_ = 0;
};

}  // namespace minikv
