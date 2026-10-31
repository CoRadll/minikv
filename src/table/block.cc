#include "minikv/table/block.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "minikv/util/coding.h"
#include "minikv/util/crc32c.h"

namespace minikv {
namespace {

// 块尾：[restart_offsets][restart_count: u32][crc32: u32]
constexpr std::size_t kTrailerSize = 8;

std::uint32_t ReadFixed32At(const std::string& data, std::size_t offset) {
  std::string_view view(data);
  view.remove_prefix(offset);
  std::uint32_t value = 0;
  GetFixed32(&view, &value);
  return value;
}

// 解析一条条目的头部：三个 varint + flags 字节
bool DecodeEntryHeader(std::string_view data, std::size_t* offset, std::uint32_t* shared,
                       std::uint32_t* non_shared, std::uint32_t* value_len, std::uint8_t* flags) {
  if (*offset >= data.size()) {
    return false;
  }
  std::string_view view = data.substr(*offset);
  const std::size_t before = view.size();

  if (!GetVarint32(&view, shared) || !GetVarint32(&view, non_shared) ||
      !GetVarint32(&view, value_len)) {
    return false;
  }
  if (view.empty()) {
    return false;
  }

  *flags = static_cast<std::uint8_t>(view.front());
  view.remove_prefix(1);

  *offset += before - view.size();
  return true;
}

}  // namespace

Block::Block(std::string contents) : contents_(std::move(contents)) { valid_ = Parse(); }

bool Block::Parse() {
  restarts_.clear();

  const std::size_t size = contents_.size();
  if (size < kTrailerSize) {
    return false;
  }

  const std::uint32_t stored_crc = ReadFixed32At(contents_, size - 4);
  const std::string_view body(contents_.data(), size - 4);
  if (Crc32c(body) != stored_crc) {
    return false;
  }

  const std::uint32_t restart_count = ReadFixed32At(contents_, size - kTrailerSize);
  if (restart_count == 0) {
    return false;
  }

  const std::size_t restart_bytes = static_cast<std::size_t>(restart_count) * 4;
  if (size < kTrailerSize + restart_bytes) {
    return false;
  }

  const std::size_t entries_end = size - kTrailerSize - restart_bytes;

  restarts_.reserve(restart_count);
  for (std::uint32_t i = 0; i < restart_count; ++i) {
    const std::uint32_t offset =
        ReadFixed32At(contents_, entries_end + static_cast<std::size_t>(i) * 4);
    if (offset > entries_end) {
      return false;
    }
    if (!restarts_.empty() && offset <= restarts_.back()) {
      return false;  // 必须严格递增
    }
    restarts_.push_back(offset);
  }

  if (restarts_.front() != 0) {
    return false;
  }

  entries_end_ = entries_end;
  return true;
}

Status Block::Get(std::string_view key, std::string* value, bool* deleted) const {
  if (!valid_) {
    return Status::Corruption("block is invalid");
  }
  if (restarts_.empty()) {
    return Status::NotFound();
  }

  const std::string_view data = contents_;

  // 1) 在重启点之间二分，找到最后一个 restart_key <= target
  auto restart_key_at = [&](std::size_t index, std::string* out) -> bool {
    std::size_t offset = restarts_[index];
    std::uint32_t shared = 0;
    std::uint32_t non_shared = 0;
    std::uint32_t value_len = 0;
    std::uint8_t flags = 0;
    if (!DecodeEntryHeader(data, &offset, &shared, &non_shared, &value_len, &flags)) {
      return false;
    }
    if (shared != 0) {
      return false;  // 重启点必须存完整 key
    }
    if (offset + non_shared + value_len > entries_end_) {
      return false;
    }
    out->assign(data.data() + offset, non_shared);
    return true;
  };

  std::size_t left = 0;
  std::size_t right = restarts_.size() - 1;
  while (left < right) {
    const std::size_t mid = left + (right - left + 1) / 2;
    std::string restart_key;
    if (!restart_key_at(mid, &restart_key)) {
      return Status::Corruption("block restart entry is malformed");
    }
    if (restart_key <= key) {
      left = mid;
    } else {
      right = mid - 1;
    }
  }

  // 2) 从该重启点开始顺序扫描
  std::string current_key;
  std::size_t offset = restarts_[left];
  while (offset < entries_end_) {
    std::uint32_t shared = 0;
    std::uint32_t non_shared = 0;
    std::uint32_t value_len = 0;
    std::uint8_t flags = 0;
    if (!DecodeEntryHeader(data, &offset, &shared, &non_shared, &value_len, &flags)) {
      return Status::Corruption("block entry header is malformed");
    }
    if (shared > current_key.size()) {
      return Status::Corruption("block shared prefix exceeds previous key");
    }
    if (offset + non_shared + value_len > entries_end_) {
      return Status::Corruption("block entry overruns block boundary");
    }

    current_key.resize(shared);
    current_key.append(data.data() + offset, non_shared);
    offset += non_shared;

    const std::string_view entry_value(data.data() + offset, value_len);
    offset += value_len;

    if (current_key == key) {
      if (value != nullptr) {
        value->assign(entry_value.data(), entry_value.size());
      }
      if (deleted != nullptr) {
        *deleted = (flags & 0x01u) != 0;
      }
      return Status::Ok();
    }
    if (current_key > key) {
      return Status::NotFound();
    }
  }

  return Status::NotFound();
}

}  // namespace minikv
