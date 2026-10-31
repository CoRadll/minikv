#include "minikv/table/block_builder.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "minikv/util/coding.h"
#include "minikv/util/crc32c.h"

namespace minikv {
namespace {

std::size_t CommonPrefixLength(std::string_view a, std::string_view b) {
  const std::size_t max = (a.size() < b.size()) ? a.size() : b.size();
  std::size_t i = 0;
  while (i < max && a[i] == b[i]) {
    ++i;
  }
  return i;
}

}  // namespace

void BlockBuilder::Add(std::string_view key, std::string_view value, bool deleted) {
  std::size_t shared = 0;
  if (counter_ < kBlockRestartInterval) {
    shared = CommonPrefixLength(last_key_, key);
  } else {
    // 到达间隔：下一条成为重启点，必须存完整 key
    restarts_.push_back(static_cast<std::uint32_t>(buffer_.size()));
    counter_ = 0;
  }

  const std::size_t non_shared = key.size() - shared;
  // 墓碑不携带 value：即使调用方传了值也丢弃，保证"删除"不占空间
  const std::size_t stored_value_size = deleted ? 0 : value.size();

  PutVarint32(&buffer_, static_cast<std::uint32_t>(shared));
  PutVarint32(&buffer_, static_cast<std::uint32_t>(non_shared));
  PutVarint32(&buffer_, static_cast<std::uint32_t>(stored_value_size));
  buffer_.push_back(deleted ? static_cast<char>(1) : static_cast<char>(0));
  buffer_.append(key.data() + shared, non_shared);
  if (!deleted) {
    buffer_.append(value.data(), value.size());
  }

  last_key_.assign(key.data(), key.size());
  ++counter_;
}

std::string BlockBuilder::Finish() {
  for (const std::uint32_t restart : restarts_) {
    PutFixed32(&buffer_, restart);
  }
  PutFixed32(&buffer_, static_cast<std::uint32_t>(restarts_.size()));
  PutFixed32(&buffer_, Crc32c(buffer_));

  std::string result;
  result.swap(buffer_);
  restarts_.assign(1, 0);
  last_key_.clear();
  counter_ = 0;
  return result;
}

void BlockBuilder::Reset() {
  buffer_.clear();
  restarts_.assign(1, 0);
  last_key_.clear();
  counter_ = 0;
}

}  // namespace minikv
