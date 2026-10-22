#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>

#include "minikv/util/status.h"
#include "minikv/wal/wal_format.h"

namespace minikv {

// WAL 读取端：顺序回放，遇到写了一半的尾部记录时安全截断。
class WalReader {
 public:
  explicit WalReader(std::filesystem::path path);

  // 逐条解析并回调。返回 OK 表示回放完成（可能已截断坏尾）；
  // 若坏记录不在文件尾部，返回 Corruption。
  Status Replay(const std::function<void(const WalRecord&)>& on_record,
                std::uint64_t start_offset = 0);

  // 回放后有效数据的字节数，用于决定后续写入位置。
  uint64_t valid_bytes() const { return valid_bytes_; }

 private:
  std::filesystem::path path_;
  std::uint64_t valid_bytes_ = 0;
};

}  // namespace minikv
