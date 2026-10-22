#pragma once

#include <cstddef>
#include <map>
#include <shared_mutex>
#include <string>
#include <string_view>

#include "minikv/util/status.h"

namespace minikv {

// 有序内存表。删除写墓碑而不是真删，因为磁盘上的旧版本可能还在。
//
// S1 内部用 std::map 实现；S2 换成跳表，但这里的方法签名保持不变。
class MemTable {
 public:
  void Put(std::string_view key, std::string_view value);
  void Delete(std::string_view key);

  // 命中返回值并返回 OK；未命中或已删除返回 NotFound。
  Status Get(std::string_view key, std::string* value) const;

  bool Empty() const;
  std::size_t EntryCount() const;
  std::size_t ApproximateSize() const;

 private:
  struct Entry {
    std::string value;
    bool deleted = false;
  };

  mutable std::shared_mutex mutex_;
  std::map<std::string, Entry, std::less<>> table_;
  std::size_t approx_size_ = 0;
};

}  // namespace minikv
