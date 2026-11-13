#include "minikv/cache/lru_cache.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

namespace minikv {

LruCache::LruCache(std::size_t capacity_bytes) : capacity_bytes_(capacity_bytes) {}

std::string LruCache::MakeKey(uint64_t file_number, uint64_t offset) {
  // 16 字节定长二进制 key：无需哈希冲突处理，也不依赖字符串语义
  std::string key;
  key.resize(16);
  for (int i = 0; i < 8; ++i) {
    key[i] = static_cast<char>((file_number >> (8 * i)) & 0xFF);
    key[8 + i] = static_cast<char>((offset >> (8 * i)) & 0xFF);
  }
  return key;
}

bool LruCache::Lookup(uint64_t file_number, uint64_t offset, std::string* out) {
  const std::string key = MakeKey(file_number, offset);

  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = index_.find(key);
  if (it == index_.end()) {
    ++misses_;
    return false;
  }

  // 命中的块移到链表前端（最近使用）
  lru_.splice(lru_.begin(), lru_, it->second);
  if (out != nullptr) {
    *out = it->second->data;
  }
  ++hits_;
  return true;
}

void LruCache::Insert(uint64_t file_number, uint64_t offset, std::string data) {
  if (data.empty() || data.size() > capacity_bytes_) {
    return;  // 单块比总容量还大，缓存它只会把别的块全挤掉
  }

  std::string key = MakeKey(file_number, offset);

  std::lock_guard<std::mutex> lock(mutex_);
  const auto existing = index_.find(key);
  if (existing != index_.end()) {
    used_bytes_ -= existing->second->data.size();
    existing->second->data = std::move(data);
    used_bytes_ += existing->second->data.size();
    lru_.splice(lru_.begin(), lru_, existing->second);
    return;
  }

  lru_.push_front(Entry{std::move(key), std::move(data)});
  used_bytes_ += lru_.front().data.size();

  while (used_bytes_ > capacity_bytes_ && !lru_.empty()) {
    used_bytes_ -= lru_.back().data.size();
    index_.erase(lru_.back().key);
    lru_.pop_back();
  }

  index_[lru_.front().key] = lru_.begin();
}

std::size_t LruCache::UsedBytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return used_bytes_;
}

std::size_t LruCache::Count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lru_.size();
}

std::uint64_t LruCache::hits() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return hits_;
}

std::uint64_t LruCache::misses() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return misses_;
}

void LruCache::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  lru_.clear();
  index_.clear();
  used_bytes_ = 0;
}

}  // namespace minikv
