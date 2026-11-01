#include "minikv/table/table_reader.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "minikv/table/format.h"
#include "minikv/util/coding.h"

namespace minikv {
namespace {

// 索引条目 value：[block_offset: u64][block_size: u32]
bool DecodeBlockHandle(std::string_view value, std::uint64_t* offset, std::uint64_t* size) {
  std::string_view view = value;
  std::uint32_t size32 = 0;
  if (!GetFixed64(&view, offset) || !GetFixed32(&view, &size32)) {
    return false;
  }
  *size = size32;
  return true;
}

}  // namespace

Status TableReader::Open(const std::filesystem::path& path,
                         std::unique_ptr<TableReader>* reader) {
  if (reader == nullptr) {
    return Status::InvalidArgument("table reader out pointer is null");
  }

  std::unique_ptr<TableReader> table(new TableReader());
  table->path_ = path;

  Status status = table->file_.Open(path, FileOpenMode::kReadOnly);
  if (!status.ok()) {
    return status;
  }

  const std::uint64_t size = table->file_.Size();
  table->file_size_ = size;
  if (size < kTableFooterSize) {
    return Status::Corruption("table too short: " + path.string());
  }

  std::string footer_raw;
  status = table->file_.Read(size - kTableFooterSize, kTableFooterSize, &footer_raw);
  if (!status.ok()) {
    return status;
  }

  TableFooter footer;
  if (!DecodeFooter(footer_raw, &footer)) {
    return Status::Corruption("bad footer (magic/version): " + path.string());
  }

  // 索引必须在 footer 之前，且完全落在文件内（小表的索引偏移可能小于 footer 长度）
  const std::uint64_t footer_start = size - kTableFooterSize;
  if (footer.index_offset > footer_start ||
      footer.index_size > footer_start - footer.index_offset) {
    return Status::Corruption("index out of range: " + path.string());
  }
  if (footer.index_size == 0) {
    return Status::Corruption("empty index block: " + path.string());
  }

  std::string index_raw;
  status = table->file_.Read(footer.index_offset, static_cast<std::size_t>(footer.index_size),
                             &index_raw);
  if (!status.ok()) {
    return status;
  }

  auto index = std::make_unique<Block>(std::move(index_raw));
  if (!index->valid()) {
    return Status::Corruption("index block is invalid: " + path.string());
  }
  table->index_ = std::move(index);

  *reader = std::move(table);
  return Status::Ok();
}

Status TableReader::Get(std::string_view key, std::string* value, bool* deleted) const {
  if (!index_) {
    return Status::Corruption("table reader is not open");
  }

  // 1) 索引块里找第一个上界 key >= target 的数据块
  std::string block_upper_bound;
  std::string handle;
  const Status seek_status = index_->Seek(key, &block_upper_bound, &handle, nullptr);
  if (seek_status.IsNotFound()) {
    return Status::NotFound();
  }
  if (!seek_status.ok()) {
    return seek_status;
  }

  std::uint64_t block_offset = 0;
  std::uint64_t block_size = 0;
  if (!DecodeBlockHandle(handle, &block_offset, &block_size)) {
    return Status::Corruption("malformed block handle");
  }

  if (block_offset + block_size > file_size_ - kTableFooterSize) {
    return Status::Corruption("data block out of range");
  }

  // 2) 只读命中的那一个数据块
  std::string block_raw;
  const Status read_status =
      file_.Read(block_offset, static_cast<std::size_t>(block_size), &block_raw);
  if (!read_status.ok()) {
    return read_status;
  }

  const Block block(std::move(block_raw));
  if (!block.valid()) {
    return Status::Corruption("data block is invalid");
  }

  // 3) 块内二分
  return block.Get(key, value, deleted);
}

std::unique_ptr<Iterator> TableReader::NewIterator() const {
  // TODO(S4): 顺序遍历（Scan 与 Compaction 使用）
  return nullptr;
}

}  // namespace minikv
