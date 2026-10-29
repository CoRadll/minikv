#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>

#include "minikv/util/status.h"
#include "minikv/wal/wal_format.h"

namespace minikv {

// WAL 读取端：顺序回放。
//
// 坏记录处理策略（见 ADR：WAL 恢复语义）：
// 遇到第一条无法完整解码的记录就停止，并把文件截断到最后一个完整记录的
// 结束位置。没有校验链时无法可靠区分「尾部写了一半」与「中段损坏」，
// 因此统一按尾部处理，丢弃的字节数通过 dropped_bytes() 暴露。
class WalReader {
 public:
  explicit WalReader(std::filesystem::path path);

  // 逐条解析并回调。文件不存在时视为空日志，返回 OK。
  Status Replay(const std::function<void(const WalRecord&)>& on_record,
                std::uint64_t start_offset = 0);

  // 回放后有效数据的字节数，用于决定后续写入位置。
  uint64_t valid_bytes() const { return valid_bytes_; }

  // 本次回放被截断丢弃的字节数（0 表示日志完整）。
  uint64_t dropped_bytes() const { return dropped_bytes_; }

 private:
  std::filesystem::path path_;
  std::uint64_t valid_bytes_ = 0;
  std::uint64_t dropped_bytes_ = 0;
};

}  // namespace minikv
