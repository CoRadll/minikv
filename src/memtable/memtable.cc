#include "minikv/memtable/memtable.h"

#include <cstddef>
#include <functional>
#include <shared_mutex>
#include <string>
#include <string_view>

namespace minikv {
namespace {

// 每个条目的粗略固定开销（跳表节点头部 + 指针数组的平均摊派）。
// 只用于"是否该刷盘"的判断，不要求精确。
constexpr std::size_t kEntryOverhead = 32;

}  // namespace

MemTable::MemTable() : table_(&arena_) {}

void MemTable::Put(std::string_view key, std::string_view value) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  const SkipList::InsertResult result = table_.Insert(key, value, false);
  if (!result.existed) {
    approx_size_ += key.size() + value.size() + kEntryOverhead;
    return;
  }

  if (!result.was_deleted) {
    approx_size_ -= result.old_value_size;
  }
  approx_size_ += value.size();
}

void MemTable::Delete(std::string_view key) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  const SkipList::InsertResult result = table_.Insert(key, std::string_view(), true);
  if (!result.existed) {
    approx_size_ += key.size() + kEntryOverhead;
    return;
  }
  if (!result.was_deleted) {
    approx_size_ -= result.old_value_size;
  }
}

Status MemTable::Get(std::string_view key, std::string* value) const {
  if (value == nullptr) {
    return Status::InvalidArgument("memtable get: value is null");
  }
  return (Lookup(key, value) == LookupResult::kFound) ? Status::Ok() : Status::NotFound();
}

MemTable::LookupResult MemTable::Lookup(std::string_view key, std::string* value) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);

  switch (table_.Lookup(key, value)) {
    case SkipList::LookupResult::kFound:
      return LookupResult::kFound;
    case SkipList::LookupResult::kDeleted:
      return LookupResult::kDeleted;
    case SkipList::LookupResult::kNotFound:
      break;
  }
  return LookupResult::kNotFound;
}

void MemTable::ForEachInOrder(
    const std::function<bool(std::string_view key, std::string_view value, bool deleted)>& fn) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  table_.ForEach(fn);
}

bool MemTable::Empty() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return table_.EntryCount() == 0;
}

std::size_t MemTable::EntryCount() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return table_.EntryCount();
}

std::size_t MemTable::ApproximateSize() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return approx_size_;
}

}  // namespace minikv
