#include "minikv/table/table_reader.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "minikv/cache/lru_cache.h"
#include "minikv/table/bloom.h"
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

  // 读 Bloom filter（旧文件可能没有，filter_size == 0）
  if (footer.filter_size > 0) {
    if (footer.filter_offset > footer_start ||
        footer.filter_size > footer_start - footer.filter_offset) {
      return Status::Corruption("filter out of range: " + path.string());
    }
    status = table->file_.Read(footer.filter_offset, static_cast<std::size_t>(footer.filter_size),
                               &table->filter_);
    if (!status.ok()) {
      return status;
    }
  }

  *reader = std::move(table);
  return Status::Ok();
}

Status TableReader::Get(std::string_view key, std::string* value, bool* deleted) const {
  if (!index_) {
    return Status::Corruption("table reader is not open");
  }

  // 0) Bloom filter 快速否定：返回 false 时 key 一定不在这个文件里
  if (!filter_.empty() && !BloomFilter::MayContainIn(filter_, key)) {
    return Status::NotFound();
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

  // 2) 只读命中的那一个数据块（优先走缓存）
  std::string block_raw;
  const bool cached =
      block_cache_ != nullptr && block_cache_->Lookup(file_number_, block_offset, &block_raw);
  if (!cached) {
    const Status read_status =
        file_.Read(block_offset, static_cast<std::size_t>(block_size), &block_raw);
    if (!read_status.ok()) {
      return read_status;
    }
    if (block_cache_ != nullptr) {
      block_cache_->Insert(file_number_, block_offset, block_raw);
    }
  }

  const Block block(std::move(block_raw));
  if (!block.valid()) {
    return Status::Corruption("data block is invalid");
  }

  // 3) 块内二分
  return block.Get(key, value, deleted);
}

// 顺序迭代器：按索引块的顺序逐块读取，每块解出条目后逐条前进。
//
// 只缓存"当前块"的条目（4KB），不会把整个文件读进内存。
class TableReader::TableIterator final : public Iterator {
 public:
  explicit TableIterator(const TableReader* owner) : owner_(owner) {
    owner_->index_->ForEach([this](std::string_view key, std::string_view value, bool) {
      BlockHandle handle;
      handle.upper.assign(key.data(), key.size());
      if (!DecodeBlockHandle(value, &handle.offset, &handle.size)) {
        status_ = Status::Corruption("table iterator: bad block handle");
        return false;
      }
      blocks_.push_back(std::move(handle));
      return true;
    });
  }

  bool Valid() const override { return valid_; }

  void SeekToFirst() override {
    status_ = Status::Ok();
    LoadBlock(0);
  }

  void Seek(std::string_view target) override {
    status_ = Status::Ok();

    // 第一个 upper >= target 的块
    std::size_t left = 0;
    std::size_t right = blocks_.size();
    while (left < right) {
      const std::size_t mid = left + (right - left) / 2;
      if (blocks_[mid].upper < target) {
        left = mid + 1;
      } else {
        right = mid;
      }
    }

    if (left >= blocks_.size()) {
      valid_ = false;
      return;
    }

    LoadBlock(left);
    while (valid_ && entries_[entry_index_].key < target) {
      Next();
    }
  }

  void Next() override {
    if (!valid_) {
      return;
    }
    ++entry_index_;
    if (entry_index_ < entries_.size()) {
      return;
    }
    LoadBlock(block_index_ + 1);
  }

  std::string_view key() const override {
    if (!valid_) {
      return std::string_view();
    }
    return entries_[entry_index_].key;
  }

  std::string_view value() const override {
    if (!valid_) {
      return std::string_view();
    }
    return entries_[entry_index_].value;
  }

  bool IsDeleted() const override {
    if (!valid_) {
      return false;
    }
    return entries_[entry_index_].deleted;
  }

  Status status() const override { return status_; }

 private:
  struct BlockHandle {
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::string upper;  // 该块最后一条 key
  };

  struct Entry {
    std::string key;
    std::string value;
    bool deleted = false;
  };

  void LoadBlock(std::size_t index) {
    entries_.clear();
    entry_index_ = 0;
    valid_ = false;

    if (index >= blocks_.size()) {
      return;
    }
    block_index_ = index;

    std::string raw;
    const bool cached = owner_->block_cache_ != nullptr &&
                        owner_->block_cache_->Lookup(owner_->file_number_, blocks_[index].offset,
                                                     &raw);
    if (!cached) {
      const Status read_status =
          owner_->file_.Read(blocks_[index].offset, static_cast<std::size_t>(blocks_[index].size),
                             &raw);
      if (!read_status.ok()) {
        status_ = read_status;
        return;
      }
      if (owner_->block_cache_ != nullptr) {
        owner_->block_cache_->Insert(owner_->file_number_, blocks_[index].offset, raw);
      }
    }

    Block block(std::move(raw));
    if (!block.valid()) {
      status_ = Status::Corruption("table iterator: invalid data block");
      return;
    }

    const Status walk_status =
        block.ForEach([this](std::string_view key, std::string_view value, bool deleted) {
          Entry entry;
          entry.key.assign(key.data(), key.size());
          entry.value.assign(value.data(), value.size());
          entry.deleted = deleted;
          entries_.push_back(std::move(entry));
          return true;
        });
    if (!walk_status.ok()) {
      status_ = walk_status;
      return;
    }

    if (entries_.empty()) {
      LoadBlock(index + 1);  // 极端情况：空块，跳过
      return;
    }
    valid_ = true;
  }

  const TableReader* owner_;
  std::vector<BlockHandle> blocks_;
  std::vector<Entry> entries_;
  std::size_t block_index_ = 0;
  std::size_t entry_index_ = 0;
  bool valid_ = false;
  Status status_;
};

void TableReader::SetBlockCache(LruCache* cache, std::uint64_t file_number) {
  block_cache_ = cache;
  file_number_ = file_number;
}

std::unique_ptr<Iterator> TableReader::NewIterator() const {
  return std::make_unique<TableIterator>(this);
}

}  // namespace minikv
