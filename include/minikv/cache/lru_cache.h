#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

namespace minikv {

// 按 (file_number, offset) 缓存已读过的数据块。
// 容量按字节计；淘汰策略是 LRU（最近最少使用）。
class LruCache {
 public:
  explicit LruCache(std::size_t capacity_bytes);

  // 命中返回 true 并填充 out；未命中返回 false
  bool Lookup(uint64_t file_number, uint64_t offset, std::string* out);

  // 插入或更新；单块超过总容量时直接丢弃
  void Insert(uint64_t file_number, uint64_t offset, std::string data);

  std::size_t UsedBytes() const;
  std::size_t Count() const;
  std::size_t capacity_bytes() const { return capacity_bytes_; }

  std::uint64_t hits() const;
  std::uint64_t misses() const;
  void Clear();

 private:
  static std::string MakeKey(uint64_t file_number, uint64_t offset);

  struct Entry {
    std::string key;
    std::string data;
  };

  std::size_t capacity_bytes_;
  std::size_t used_bytes_ = 0;
  std::uint64_t hits_ = 0;
  std::uint64_t misses_ = 0;
  mutable std::mutex mutex_;
  std::list<Entry> lru_;  // 前端 = 最近使用
  std::unordered_map<std::string, std::list<Entry>::iterator> index_;
};

}  // namespace minikv
