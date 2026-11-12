#include "minikv/api/engine.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "minikv/db/compaction.h"
#include "minikv/db/manifest.h"
#include "minikv/memtable/memtable.h"
#include "minikv/table/merging_iterator.h"
#include "minikv/table/table_builder.h"
#include "minikv/table/table_reader.h"
#include "minikv/util/env.h"
#include "minikv/util/file.h"
#include "minikv/util/logging.h"
#include "minikv/wal/wal_format.h"
#include "minikv/wal/wal_reader.h"
#include "minikv/wal/wal_writer.h"

namespace minikv {
namespace {

constexpr const char* kWalFileName = "wal.log";
constexpr const char* kManifestFileName = "MANIFEST";

Status ValidateKeyValue(std::string_view key, std::string_view value) {
  if (key.empty()) {
    return Status::InvalidArgument("empty key");
  }
  if (key.size() > kMaxKeySize) {
    return Status::InvalidArgument("key too large: " + std::to_string(key.size()) + " bytes");
  }
  if (value.size() > kMaxValueSize) {
    return Status::InvalidArgument("value too large: " + std::to_string(value.size()) + " bytes");
  }
  return Status::Ok();
}

struct OpenTable {
  std::uint64_t file_number = 0;
  int level = 0;
  std::shared_ptr<TableReader> reader;
};

std::string SstableFileName(std::uint64_t file_number) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%06llu.sst",
                static_cast<unsigned long long>(file_number));
  return std::string(buffer);
}

// 只认 6 位数字 + ".sst"，其余文件一律忽略（避免误删用户文件）
bool ParseSstableFileName(const std::string& name, std::uint64_t* number) {  if (name.size() != 10 || name.compare(6, 4, ".sst") != 0) {
    return false;
  }
  std::uint64_t value = 0;
  for (int i = 0; i < 6; ++i) {
    const char c = name[static_cast<std::size_t>(i)];
    if (c < '0' || c > '9') {
      return false;
    }
    value = value * 10 + static_cast<std::uint64_t>(c - '0');
  }
  *number = value;
  return true;
}

// MANIFEST.tmp 是"写到一半"的中间产物，永远不是权威数据
Status RemoveStaleTempFiles(const std::filesystem::path& dir) {
  const std::filesystem::path tmp = (dir / kManifestFileName).string() + ".tmp";
  if (!FileExists(tmp)) {
    return Status::Ok();
  }
  LOG_WARN("removing stale " + tmp.string());
  return RemoveFile(tmp);
}

// 删除未被 MANIFEST 引用的 .sst：崩溃发生在"写完文件但还没登记"时会产生这种孤儿
Status RemoveOrphanSstables(const std::filesystem::path& dir, const Manifest& manifest,
                            std::size_t* removed) {
  std::set<std::uint64_t> referenced;
  for (const Manifest::FileMeta& meta : manifest.files()) {
    referenced.insert(meta.file_number);
  }

  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (ec) {
      return Status::IOError("list " + dir.string() + ": " + ec.message());
    }
    if (!entry.is_regular_file()) {
      continue;
    }

    std::uint64_t number = 0;
    if (!ParseSstableFileName(entry.path().filename().string(), &number)) {
      continue;
    }
    if (referenced.count(number) != 0) {
      continue;
    }

    Status status = RemoveFile(entry.path());
    if (!status.ok()) {
      return status;
    }
    ++(*removed);
  }
  return Status::Ok();
}

// ---------------------------------------------------------------------------
// 扫描相关：内存表的快照迭代器 + 去重/去墓碑的包装迭代器
// ---------------------------------------------------------------------------

class VectorIterator final : public Iterator {
 public:
  struct Entry {
    std::string key;
    std::string value;
    bool deleted = false;
  };

  explicit VectorIterator(std::vector<Entry> entries) : entries_(std::move(entries)) {}

  bool Valid() const override { return index_ < entries_.size(); }
  void SeekToFirst() override { index_ = 0; }

  void Seek(std::string_view target) override {
    index_ = 0;
    while (index_ < entries_.size() && entries_[index_].key < target) {
      ++index_;
    }
  }

  void Next() override {
    if (index_ < entries_.size()) {
      ++index_;
    }
  }

  std::string_view key() const override {
    return Valid() ? std::string_view(entries_[index_].key) : std::string_view();
  }

  std::string_view value() const override {
    return Valid() ? std::string_view(entries_[index_].value) : std::string_view();
  }

  bool IsDeleted() const override { return Valid() && entries_[index_].deleted; }

  Status status() const override { return Status::Ok(); }

 private:
  std::vector<Entry> entries_;
  std::size_t index_ = 0;
};

// 内存表是可变结构，扫描时先做一份有序快照，避免遍历期间被写入打断
std::unique_ptr<Iterator> MakeMemTableIterator(const MemTable* table) {
  std::vector<VectorIterator::Entry> entries;
  table->ForEachInOrder([&entries](std::string_view key, std::string_view value, bool deleted) {
    entries.push_back(VectorIterator::Entry{std::string(key), std::string(value), deleted});
    return true;
  });
  return std::make_unique<VectorIterator>(std::move(entries));
}

// 归并结果再去重、去墓碑：
//   - 同一个 key 只输出最新版本（归并器保证新的在前）
//   - 墓碑不输出（对调用方来说 key 就是不存在）
// keepalive 持有 TableReader，防止扫描期间合并把文件关掉
class ScanIterator final : public Iterator {
 public:
  ScanIterator(std::unique_ptr<Iterator> merged,
               std::vector<std::shared_ptr<TableReader>> keepalive)
      : merged_(std::move(merged)), keepalive_(std::move(keepalive)) {}

  bool Valid() const override { return valid_; }

  void SeekToFirst() override {
    merged_->SeekToFirst();
    last_emitted_.clear();
    Refresh();
  }

  void Seek(std::string_view target) override {
    merged_->Seek(target);
    last_emitted_.clear();
    Refresh();
  }

  void Next() override {
    if (!valid_) {
      return;
    }
    last_emitted_.assign(merged_->key());
    merged_->Next();
    Refresh();
  }

  std::string_view key() const override {
    return valid_ ? merged_->key() : std::string_view();
  }

  std::string_view value() const override {
    return valid_ ? merged_->value() : std::string_view();
  }

  bool IsDeleted() const override { return false; }  // 墓碑已被跳过

  Status status() const override { return merged_->status(); }

 private:
  void Refresh() {
    while (merged_->Valid()) {
      const std::string_view current = merged_->key();

      // 比已输出 key 更小或相等：都是同一个 key 的旧版本
      if (!last_emitted_.empty() && current <= last_emitted_) {
        merged_->Next();
        continue;
      }

      if (merged_->IsDeleted()) {
        // 墓碑是该 key 的最新状态：记下来，后续旧版本一律跳过
        last_emitted_.assign(merged_->key());
        merged_->Next();
        continue;
      }

      valid_ = true;
      return;
    }
    valid_ = false;
  }

  std::unique_ptr<Iterator> merged_;
  std::vector<std::shared_ptr<TableReader>> keepalive_;
  std::string last_emitted_;
  bool valid_ = false;
};

}  // namespace

class EngineImpl : public Engine {
 public:
  explicit EngineImpl(Options options) : options_(std::move(options)) {}

  ~EngineImpl() override {
    if (!closed_) {
      Close();
    }
  }

  Status Init() {
    Status status = CreateDirIfMissing(options_.db_path);
    if (!status.ok()) {
      return status;
    }

    wal_path_ = options_.db_path / kWalFileName;
    manifest_path_ = options_.db_path / kManifestFileName;

    // 1) 先按 MANIFEST 打开所有 SSTable（倒序入队：新文件在前，查询从新到旧）
    status = Manifest::Load(manifest_path_, &manifest_);
    if (!status.ok()) {
      return status;
    }

    // 清理上一次崩溃可能留下的半成品：残留临时文件与未被 MANIFEST 引用的 .sst
    status = RemoveStaleTempFiles(options_.db_path);
    if (!status.ok()) {
      return status;
    }
    std::size_t removed_orphans = 0;
    status = RemoveOrphanSstables(options_.db_path, manifest_, &removed_orphans);
    if (!status.ok()) {
      return status;
    }
    if (removed_orphans > 0) {
      LOG_WARN("removed " + std::to_string(removed_orphans) + " orphan sstable(s)");
    }

    for (auto it = manifest_.files().rbegin(); it != manifest_.files().rend(); ++it) {
      const std::filesystem::path table_path = options_.db_path / SstableFileName(it->file_number);
      std::unique_ptr<TableReader> reader;
      status = TableReader::Open(table_path, &reader);
      if (!status.ok()) {
        return Status::Corruption("cannot open sstable " + table_path.string() + ": " +
                                  status.ToString());
      }
      OpenTable table;
      table.file_number = it->file_number;
      table.level = it->level;
      table.reader = std::shared_ptr<TableReader>(std::move(reader));
      tables_.push_back(std::move(table));
    }

    // 2) 再回放 WAL（只包含最后一次刷盘之后的写入）
    mem_ = std::make_unique<MemTable>();
    std::uint64_t replayed = 0;
    WalReader reader(wal_path_);
    status = reader.Replay([this, &replayed](const WalRecord& record) {
      if (record.type == RecordType::kPut) {
        mem_->Put(record.key, record.value);
      } else {
        mem_->Delete(record.key);
      }
      ++replayed;
    });
    if (!status.ok()) {
      return status;
    }
    if (reader.dropped_bytes() > 0) {
      LOG_WARN("recovered with truncated wal: dropped " + std::to_string(reader.dropped_bytes()) +
               " bytes");
    }

    wal_ = std::make_unique<WalWriter>();
    status = wal_->Open(wal_path_, options_.sync_on_write, reader.valid_bytes());
    if (!status.ok()) {
      return status;
    }

    LOG_INFO("engine opened: sstables = " + std::to_string(tables_.size()) +
             ", wal records replayed = " + std::to_string(replayed));
    return Status::Ok();
  }

  Status Put(std::string_view key, std::string_view value) override {
    Status status = ValidateKeyValue(key, value);
    if (!status.ok()) {
      return status;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      return Status::Closed("engine is closed");
    }

    WalRecord record;
    record.type = RecordType::kPut;
    record.key.assign(key.data(), key.size());
    record.value.assign(value.data(), value.size());

    status = wal_->Append(record);
    if (!status.ok()) {
      return status;
    }
    mem_->Put(key, value);

    return MaybeFlushLocked();
  }

  Status Delete(std::string_view key) override {
    Status status = ValidateKeyValue(key, std::string_view{});
    if (!status.ok()) {
      return status;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      return Status::Closed("engine is closed");
    }

    WalRecord record;
    record.type = RecordType::kDelete;
    record.key.assign(key.data(), key.size());

    status = wal_->Append(record);
    if (!status.ok()) {
      return status;
    }
    mem_->Delete(key);

    return MaybeFlushLocked();
  }

  Status Get(std::string_view key, std::string* value) override {
    if (value == nullptr) {
      return Status::InvalidArgument("get: value is null");
    }

    // 刷盘会修改 tables_，这里与写共用一把锁，保证不会读到半成品状态
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      return Status::Closed("engine is closed");
    }

    // 1) 当前内存表
    switch (mem_->Lookup(key, value)) {
      case MemTable::LookupResult::kFound:
        return Status::Ok();
      case MemTable::LookupResult::kDeleted:
        return Status::NotFound();  // 墓碑：不再往下找
      case MemTable::LookupResult::kNotFound:
        break;
    }

    // 2) 正在刷盘的内存表
    if (imm_ != nullptr) {
      switch (imm_->Lookup(key, value)) {
        case MemTable::LookupResult::kFound:
          return Status::Ok();
        case MemTable::LookupResult::kDeleted:
          return Status::NotFound();
        case MemTable::LookupResult::kNotFound:
          break;
      }
    }

    // 3) 从新到旧查 SSTable
    for (const auto& table : tables_) {
      bool deleted = false;
      const Status status = table.reader->Get(key, value, &deleted);
      if (status.ok()) {
        return deleted ? Status::NotFound() : Status::Ok();
      }
      if (!status.IsNotFound()) {
        return status;
      }
    }

    return Status::NotFound();
  }

  Status Flush() override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      return Status::Closed("engine is closed");
    }
    return FlushLocked();
  }

  Status Compact() override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      return Status::Closed("engine is closed");
    }
    return CompactLocked();
  }

  std::unique_ptr<Iterator> NewIterator() override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      return nullptr;
    }

    std::vector<std::unique_ptr<Iterator>> children;
    std::vector<std::shared_ptr<TableReader>> keepalive;

    // 顺序即优先级：当前内存表 > 刷盘中的内存表 > 新文件 > 旧文件
    if (mem_ != nullptr) {
      children.push_back(MakeMemTableIterator(mem_.get()));
    }
    if (imm_ != nullptr) {
      children.push_back(MakeMemTableIterator(imm_.get()));
    }
    for (const OpenTable& table : tables_) {
      children.push_back(table.reader->NewIterator());
      keepalive.push_back(table.reader);
    }

    return std::make_unique<ScanIterator>(
        std::make_unique<MergingIterator>(std::move(children)), std::move(keepalive));
  }

  EngineStats GetStats() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    EngineStats stats;
    if (wal_) {
      stats.wal_bytes = wal_->size();
    }
    if (mem_) {
      stats.memtable_entries = mem_->EntryCount();
      stats.memtable_bytes = mem_->ApproximateSize();
    }
    stats.sstable_count = tables_.size();
    return stats;
  }

  Status Close() override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      return Status::Ok();
    }

    // 关闭前把内存表刷盘：重启更快，也方便演示观察 .sst
    Status status = FlushLocked();
    if (status.ok() && wal_) {
      status = wal_->Sync();
      if (status.ok()) {
        wal_->Close();
      }
    }
    closed_ = true;
    return status;
  }

 private:
  Status MaybeFlushLocked() {
    if (mem_->ApproximateSize() < options_.memtable_size) {
      return Status::Ok();
    }
    return FlushLocked();
  }

  Status FlushLocked() {
    if (mem_ == nullptr || mem_->Empty()) {
      return Status::Ok();
    }

    // 1) 冻结：新写入进新的内存表，旧表等待落盘
    imm_ = std::move(mem_);
    mem_ = std::make_unique<MemTable>();

    // 2) 写 SSTable
    const std::uint64_t file_number = manifest_.next_file_number();
    const std::filesystem::path table_path = options_.db_path / SstableFileName(file_number);

    TableBuilder builder(table_path);
    Status status = Status::Ok();
    imm_->ForEachInOrder(
        [&builder, &status](std::string_view key, std::string_view value, bool deleted) {
          status = builder.Add(key, value, deleted);
          return status.ok();
        });
    if (status.ok()) {
      status = builder.Finish();
    }
    if (!status.ok()) {
      LOG_ERROR("flush failed while writing " + table_path.string() + ": " + status.ToString());
      return status;  // imm_ 保留：数据仍可读，下次刷盘会重试
    }

    std::unique_ptr<TableReader> reader;
    status = TableReader::Open(table_path, &reader);
    if (!status.ok()) {
      return status;
    }

    // 3) 先登记到 MANIFEST 并落盘，再清 WAL
    status = manifest_.AddFile(0, file_number, builder.smallest_key(), builder.largest_key());
    if (status.ok()) {
      status = manifest_.Save();
    }
    if (!status.ok()) {
      return status;
    }

    OpenTable table;
    table.file_number = file_number;
    table.level = 0;
    table.reader = std::shared_ptr<TableReader>(std::move(reader));
    tables_.insert(tables_.begin(), std::move(table));

    // 4) 数据已在 SSTable + MANIFEST 中，WAL 可以清空。
    //    若在清空 WAL 之前崩溃，重放这些记录是幂等的，不会产生错误数据。
    status = ResetWalLocked();
    if (!status.ok()) {
      return status;
    }

    imm_.reset();
    LOG_INFO("flushed memtable to " + table_path.string());

    // 5) L0 文件数达到阈值就合并
    return MaybeCompactLocked();
  }

  Status MaybeCompactLocked() {
    std::size_t level0_files = 0;
    for (const OpenTable& table : tables_) {
      if (table.level == 0) {
        ++level0_files;
      }
    }
    if (level0_files < options_.level0_compaction_trigger) {
      return Status::Ok();
    }
    return CompactLocked();
  }

  // 两层简化模型：把所有 L0 + L1 全量合并成一个 L1 文件。
  //
  // 事务顺序：写新文件 → 更新 MANIFEST（原子替换）→ 换内存视图 → 删旧文件。
  // 任何一步失败都保留旧版本可用；崩溃最多留下一个孤儿文件（启动时清理）。
  Status CompactLocked() {
    std::vector<const TableReader*> inputs;
    std::vector<std::uint64_t> old_file_numbers;

    for (const OpenTable& table : tables_) {
      if (table.level <= 1) {
        inputs.push_back(table.reader.get());
        old_file_numbers.push_back(table.file_number);
      }
    }
    if (inputs.size() < 2) {
      return Status::Ok();  // 没有可合并的对象
    }

    const std::uint64_t output_number = manifest_.next_file_number();
    const std::filesystem::path output_path =
        options_.db_path / SstableFileName(output_number);

    CompactionResult result;
    Status status = Compaction::Run(inputs, output_number, output_path, &result);
    if (!status.ok()) {
      LOG_ERROR("compaction failed: " + status.ToString());
      return status;
    }

    // 安装新 MANIFEST：清空后只写一个 L1 文件（若没有有效数据则不写）
    manifest_.ClearFiles();
    if (result.has_output) {
      status = manifest_.AddFile(1, result.file_number, result.smallest_key, result.largest_key);
      if (status.ok()) {
        status = manifest_.Save();
      }
      if (!status.ok()) {
        return status;
      }
    } else {
      status = manifest_.Save();
      if (!status.ok()) {
        return status;
      }
    }

    // 换内存视图：参与合并的文件全部移除，换成新文件（新文件在最前）
    std::vector<OpenTable> updated;
    updated.reserve(1);
    if (result.has_output) {
      std::unique_ptr<TableReader> reader;
      status = TableReader::Open(output_path, &reader);
      if (!status.ok()) {
        return status;
      }
      OpenTable table;
      table.file_number = result.file_number;
      table.level = 1;
      table.reader = std::shared_ptr<TableReader>(std::move(reader));
      updated.push_back(std::move(table));
    }
    tables_ = std::move(updated);

    // 旧文件已不在 MANIFEST 中，可以安全删除
    for (const std::uint64_t number : old_file_numbers) {
      Status remove_status = RemoveFile(options_.db_path / SstableFileName(number));
      if (!remove_status.ok()) {
        LOG_WARN("failed to remove old sstable: " + remove_status.ToString());
      }
    }

    LOG_INFO("compacted " + std::to_string(inputs.size()) + " files into " +
             (result.has_output ? output_path.string() : std::string("<empty>")) + ", entries = " +
             std::to_string(result.entries));
    return Status::Ok();
  }

  Status ResetWalLocked() {
    if (!wal_) {
      return Status::Ok();
    }
    wal_->Close();

    File file;
    Status status = file.Open(wal_path_, FileOpenMode::kReadWrite);
    if (!status.ok()) {
      return status;
    }
    status = file.Truncate(0);
    if (status.ok()) {
      status = file.Sync();
    }
    file.Close();
    if (!status.ok()) {
      return status;
    }
    return wal_->Open(wal_path_, options_.sync_on_write, 0);
  }

  Options options_;
  std::filesystem::path wal_path_;
  std::filesystem::path manifest_path_;
  Manifest manifest_;
  std::unique_ptr<MemTable> mem_;
  std::unique_ptr<MemTable> imm_;
  std::vector<OpenTable> tables_;  // 新文件在前
  std::unique_ptr<WalWriter> wal_;
  mutable std::mutex mutex_;
  bool closed_ = false;
};

Status Engine::Open(const Options& options, std::unique_ptr<Engine>* engine) {
  if (engine == nullptr) {
    return Status::InvalidArgument("engine out pointer is null");
  }
  if (options.db_path.empty()) {
    return Status::InvalidArgument("db_path is empty");
  }

  auto impl = std::make_unique<EngineImpl>(options);
  Status status = impl->Init();
  if (!status.ok()) {
    return status;
  }

  *engine = std::move(impl);
  return Status::Ok();
}

}  // namespace minikv
