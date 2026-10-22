#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

namespace minikv {

// TODO(S5): 分片 LRU，按 (file_number, offset) 缓存已读过的数据块。
// 草案：S5 实现时允许调整接口签名（引用计数、分片数、淘汰回调待定）。
class LruCache {
 public:
  explicit LruCache(std::size_t capacity_bytes);

  bool Lookup(uint64_t file_number, uint64_t offset, std::string* out);
  void Insert(uint64_t file_number, uint64_t offset, std::string data);
  std::size_t UsedBytes() const;

 private:
  std::size_t capacity_bytes_;
  std::size_t used_bytes_ = 0;
  mutable std::mutex mutex_;
  std::list<std::string> lru_;
  std::unordered_map<uint64_t, std::list<std::string>::iterator> index_;
};

}  // namespace minikv
