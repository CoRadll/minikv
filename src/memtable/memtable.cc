#include "minikv/memtable/memtable.h"

#include <cstddef>
#include <shared_mutex>
#include <string>
#include <string_view>

namespace minikv {
namespace {

// 每个条目的粗略开销：map 节点、指针、Entry 自身的固定部分。
// 只用于「是否该刷盘」的判断，不要求精确。
constexpr std::size_t kEntryOverhead = 32;

}  // namespace

void MemTable::Put(std::string_view key, std::string_view value) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  const auto it = table_.find(key);
  if (it == table_.end()) {
    Entry entry;
    entry.value.assign(value.data(), value.size());
    table_.emplace(std::string(key), std::move(entry));
    approx_size_ += key.size() + value.size() + kEntryOverhead;
    return;
  }

  // 覆盖写：先扣掉旧值的体积（墓碑没有值可扣）
  if (!it->second.deleted) {
    approx_size_ -= it->second.value.size();
  }
  it->second.value.assign(value.data(), value.size());
  it->second.deleted = false;
  approx_size_ += value.size();
}

void MemTable::Delete(std::string_view key) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  const auto it = table_.find(key);
  if (it == table_.end()) {
    Entry entry;
    entry.deleted = true;
    table_.emplace(std::string(key), std::move(entry));
    approx_size_ += key.size() + kEntryOverhead;
    return;
  }

  if (!it->second.deleted) {
    approx_size_ -= it->second.value.size();
  }
  it->second.value.clear();
  it->second.deleted = true;
}

Status MemTable::Get(std::string_view key, std::string* value) const {
  if (value == nullptr) {
    return Status::InvalidArgument("memtable get: value is null");
  }

  std::shared_lock<std::shared_mutex> lock(mutex_);
  const auto it = table_.find(key);
  if (it == table_.end() || it->second.deleted) {
    return Status::NotFound();
  }

  *value = it->second.value;
  return Status::Ok();
}

bool MemTable::Empty() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return table_.empty();
}

std::size_t MemTable::EntryCount() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return table_.size();
}

std::size_t MemTable::ApproximateSize() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return approx_size_;
}

}  // namespace minikv
