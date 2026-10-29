#include "minikv/api/engine.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include "minikv/memtable/memtable.h"
#include "minikv/util/env.h"
#include "minikv/util/logging.h"
#include "minikv/wal/wal_format.h"
#include "minikv/wal/wal_reader.h"
#include "minikv/wal/wal_writer.h"

namespace minikv {
namespace {

constexpr const char* kWalFileName = "wal.log";

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

    const std::filesystem::path wal_path = options_.db_path / kWalFileName;

    // 1) 回放：把日志里的变更重新应用到内存表
    memtable_ = std::make_unique<MemTable>();
    std::uint64_t replay_count = 0;
    WalReader reader(wal_path);
    status = reader.Replay([this, &replay_count](const WalRecord& record) {
      if (record.type == RecordType::kPut) {
        memtable_->Put(record.key, record.value);
      } else {
        memtable_->Delete(record.key);
      }
      ++replay_count;
    });
    if (!status.ok()) {
      return status;
    }

    if (reader.dropped_bytes() > 0) {
      LOG_WARN("recovered with truncated wal: dropped " + std::to_string(reader.dropped_bytes()) +
               " bytes");
    }

    // 2) 以「有效长度」继续追加，坏尾不会复活
    wal_ = std::make_unique<WalWriter>();
    status = wal_->Open(wal_path, options_.sync_on_write, reader.valid_bytes());
    if (!status.ok()) {
      return status;
    }

    LOG_INFO("engine opened: records replayed = " + std::to_string(replay_count) + ", wal bytes = " +
             std::to_string(reader.valid_bytes()));
    return Status::Ok();
  }

  Status Put(std::string_view key, std::string_view value) override {
    Status status = ValidateKeyValue(key, value);
    if (!status.ok()) {
      return status;
    }

    std::lock_guard<std::mutex> lock(write_mutex_);
    if (closed_) {
      return Status::Closed("engine is closed");
    }

    WalRecord record;
    record.type = RecordType::kPut;
    record.key.assign(key.data(), key.size());
    record.value.assign(value.data(), value.size());

    // 先写 WAL：写成功才允许改内存，否则会出现「内存有、磁盘没有」
    status = wal_->Append(record);
    if (!status.ok()) {
      return status;
    }
    memtable_->Put(key, value);
    return Status::Ok();
  }

  Status Get(std::string_view key, std::string* value) override {
    if (value == nullptr) {
      return Status::InvalidArgument("get: value is null");
    }
    if (closed_) {
      return Status::Closed("engine is closed");
    }
    return memtable_->Get(key, value);
  }

  Status Delete(std::string_view key) override {
    Status status = ValidateKeyValue(key, std::string_view{});
    if (!status.ok()) {
      return status;
    }

    std::lock_guard<std::mutex> lock(write_mutex_);
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
    memtable_->Delete(key);
    return Status::Ok();
  }

  std::unique_ptr<Iterator> NewIterator() override {
    // TODO(S5): 合并 MemTable 与各层 SSTable 的迭代器
    return nullptr;
  }

  Status Close() override {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (closed_) {
      return Status::Ok();
    }
    closed_ = true;

    Status status = Status::Ok();
    if (wal_) {
      status = wal_->Sync();
      if (status.ok()) {
        wal_->Close();
      }
    }
    return status;
  }

 private:
  Options options_;
  std::unique_ptr<MemTable> memtable_;
  std::unique_ptr<WalWriter> wal_;
  std::mutex write_mutex_;
  bool closed_ = false;
};

}  // namespace

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
