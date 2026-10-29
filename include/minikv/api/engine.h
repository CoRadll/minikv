#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "minikv/api/iterator.h"
#include "minikv/api/options.h"
#include "minikv/util/status.h"

namespace minikv {

// 引擎运行状态快照。只用于观测，不参与读写决策。
struct EngineStats {
  std::uint64_t wal_bytes = 0;        // WAL 当前有效长度
  std::size_t memtable_entries = 0;   // 内存表条目数（含墓碑）
  std::size_t memtable_bytes = 0;     // 内存表近似占用
};

// 对外唯一门面。调用方不需要知道 WAL、SSTable、Compaction 的存在。
class Engine {
 public:
  // 打开（或创建）数据库。成功后 *engine 持有实例。
  static Status Open(const Options& options, std::unique_ptr<Engine>* engine);

  virtual ~Engine() = default;

  virtual Status Put(std::string_view key, std::string_view value) = 0;
  virtual Status Get(std::string_view key, std::string* value) = 0;
  virtual Status Delete(std::string_view key) = 0;

  // TODO(S5): 范围扫描，返回 [begin, end) 的有序视图。
  virtual std::unique_ptr<Iterator> NewIterator() = 0;

  virtual EngineStats GetStats() const = 0;

  virtual Status Close() = 0;
};

}  // namespace minikv
