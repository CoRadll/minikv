#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace minikv {

class Arena;

// 用 Arena 分配节点的跳表，替代 MemTable 内部的 std::map。
//
// 设计要点：
//   - 节点、key、value 都从 Arena 分配，整表释放时一次性归还
//   - 自身不做并发控制；并发由 MemTable 的读写锁保证
//   - 插入同一 key 视为覆盖，EntryCount 不变
class SkipList {
 public:
  static constexpr int kMaxHeight = 12;

  // 插入结果，供上层做容量统计
  struct InsertResult {
    bool existed = false;
    bool was_deleted = false;
    std::size_t old_value_size = 0;
  };

  enum class LookupResult {
    kFound,
    kDeleted,
    kNotFound,
  };

  explicit SkipList(Arena* arena, std::uint64_t seed = 0x9E3779B97F4A7C15ull);

  InsertResult Insert(std::string_view key, std::string_view value, bool deleted);
  LookupResult Lookup(std::string_view key, std::string* value) const;
  bool Contains(std::string_view key) const;

  // 按 key 升序回调；回调返回 false 时提前终止
  void ForEach(const std::function<bool(std::string_view key, std::string_view value, bool deleted)>&
                   fn) const;

  std::size_t EntryCount() const { return size_; }
  int max_height() const { return max_height_; }

 private:
  struct Node;

  static std::size_t NodeBytes(int height);
  static std::string_view KeyOf(const Node* node);
  static std::string_view ValueOf(const Node* node);
  Node* FindGreaterOrEqual(std::string_view key, Node** prev) const;
  int RandomHeight();
  Node* NewNode(std::string_view key, std::string_view value, bool deleted, int height);

  Arena* arena_;
  Node* head_;
  int max_height_ = 1;
  std::size_t size_ = 0;
  std::uint64_t rng_state_;
};

}  // namespace minikv
