#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace minikv {

class Arena;

// TODO(S2): 用 Arena 分配节点的跳表，替换 MemTable 内部的 std::map。
// 目标：有序插入、无锁读、层高由 Random::Skewed 决定。
// 草案：S2 实现时允许调整接口签名（迭代器与版本号支持待定）。
class SkipList {
 public:
  enum class LookupResult {
    kFound,
    kDeleted,
    kNotFound,
  };

  explicit SkipList(const Arena* arena);

  void Insert(std::string_view key, std::string_view value, bool deleted);
  LookupResult Lookup(std::string_view key, std::string* value) const;
  std::size_t ApproximateSize() const;

 private:
  const Arena* arena_;
  std::size_t size_ = 0;
};

}  // namespace minikv
