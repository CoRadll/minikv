#include "minikv/table/table_builder.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "minikv/table/bloom.h"
#include "minikv/table/format.h"
#include "minikv/util/coding.h"

namespace minikv {
namespace {

// 索引条目 value：数据块偏移 + 数据块长度
std::string EncodeBlockHandle(std::uint64_t offset, std::uint64_t size) {
  std::string out;
  PutFixed64(&out, offset);
  PutFixed32(&out, static_cast<std::uint32_t>(size));
  return out;
}

}  // namespace

TableBuilder::TableBuilder(std::filesystem::path path, std::size_t block_size)
    : path_(std::move(path)), block_size_(block_size == 0 ? 4096 : block_size) {}

TableBuilder::~TableBuilder() = default;

Status TableBuilder::EnsureOpen() {
  if (file_.is_open()) {
    return Status::Ok();
  }

  Status status = file_.Open(path_, FileOpenMode::kReadWrite);
  if (!status.ok()) {
    return status;
  }
  // 新建文件：保证从 0 开始写，避免残留旧内容
  return file_.Truncate(0);
}

Status TableBuilder::Add(std::string_view key, std::string_view value, bool deleted) {
  if (finished_) {
    return Status::InvalidArgument("table builder already finished");
  }
  if (num_entries_ > 0 && key <= last_key_) {
    return Status::InvalidArgument("keys must be added in strictly increasing order");
  }

  if (num_entries_ == 0) {
    smallest_key_.assign(key.data(), key.size());
  }
  largest_key_.assign(key.data(), key.size());
  last_key_.assign(key.data(), key.size());

  data_builder_.Add(key, value, deleted);
  keys_.push_back(std::string(key));
  ++num_entries_;

  if (data_builder_.CurrentSizeEstimate() >= block_size_) {
    return FlushBlock();
  }
  return Status::Ok();
}

Status TableBuilder::FlushBlock() {
  if (data_builder_.empty()) {
    return Status::Ok();
  }

  Status status = EnsureOpen();
  if (!status.ok()) {
    return status;
  }

  const std::string raw = data_builder_.Finish();
  data_builder_.Reset();

  const std::uint64_t offset = file_size_;
  status = file_.Append(raw);
  if (!status.ok()) {
    return status;
  }
  file_size_ += raw.size();

  // 索引 key 取该数据块的最后一个 key（边界语义：key <= 索引 key 时查这个块）
  index_builder_.Add(last_key_, EncodeBlockHandle(offset, raw.size()));
  ++num_data_blocks_;
  return Status::Ok();
}

Status TableBuilder::Finish() {
  if (finished_) {
    return Status::Ok();
  }
  if (num_entries_ == 0) {
    return Status::InvalidArgument("cannot finish an empty table");
  }

  Status status = FlushBlock();
  if (!status.ok()) {
    return status;
  }

  // 先写 Bloom filter，再写索引：读路径可以用 filter 跳过整个文件
  std::uint64_t filter_offset = 0;
  std::uint64_t filter_size = 0;
  if (!keys_.empty()) {
    BloomFilter bloom(keys_.size());
    for (const std::string& key : keys_) {
      bloom.Add(key);
    }
    const std::string filter_raw = bloom.Finish();
    filter_offset = file_size_;
    status = file_.Append(filter_raw);
    if (!status.ok()) {
      return status;
    }
    file_size_ += filter_raw.size();
    filter_size = filter_raw.size();
  }

  const std::string index_raw = index_builder_.Finish();
  const std::uint64_t index_offset = file_size_;
  status = file_.Append(index_raw);
  if (!status.ok()) {
    return status;
  }
  file_size_ += index_raw.size();

  TableFooter footer;
  footer.index_offset = index_offset;
  footer.index_size = index_raw.size();
  footer.filter_offset = filter_offset;
  footer.filter_size = filter_size;

  const std::string footer_raw = EncodeFooter(footer);
  status = file_.Append(footer_raw);
  if (!status.ok()) {
    return status;
  }
  file_size_ += footer_raw.size();

  status = file_.Sync();
  if (!status.ok()) {
    return status;
  }
  file_.Close();

  finished_ = true;
  return Status::Ok();
}

}  // namespace minikv
