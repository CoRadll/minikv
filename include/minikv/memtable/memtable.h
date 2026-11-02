#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <shared_mutex>
#include <string>
#include <string_view>

#include "minikv/util/status.h"

namespace minikv {

// 有序内存表。删除写墓碑而不是真删，因为磁盘上的旧版本可能还在。
//
// S1 内部用 std::map 实现；S2 末尾换成跳表，但这里的方法签名保持不变。
class MemTable {
 public:
  // 三态查找结果：值还在 / 已被删除（墓碑）/ 根本没写过。
  // 墓碑必须与"不存在"区分，否则查询会穿透到更旧的文件，让已删数据复活。
  enum class LookupResult {
    kFound,
    kDeleted,
    kNotFound,
  };

  void Put(std::string_view key, std::string_view value);
  void Delete(std::string_view key);

  // 命中返回值并返回 OK；未命中或已删除返回 NotFound（便捷接口）。
  Status Get(std::string_view key, std::string* value) const;

  LookupResult Lookup(std::string_view key, std::string* value) const;

  // 按 key 升序回调，供刷盘使用；回调返回 false 时提前终止。
  void ForEachInOrder(
      const std::function<bool(std::string_view key, std::string_view value, bool deleted)>& fn)
      const;

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
