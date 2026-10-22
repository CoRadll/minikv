#pragma once

#include <cstddef>
#include <vector>

namespace minikv {

// 内存池：一次性申请大块，按序分配小对象，整体析构时统一释放。
// 跳表每个 key 一个节点，逐次 new 既慢又碎片化；用 Arena 让分配变成指针加法。
class Arena {
 public:
  Arena();
  ~Arena();

  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;

  char* Allocate(std::size_t bytes);
  char* AllocateAligned(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t));
  std::size_t MemoryUsage() const { return allocated_bytes_; }
  void Reset();

 private:
  struct Block;

  std::vector<Block*> blocks_;
  std::size_t allocated_bytes_ = 0;
  std::size_t remaining_ = 0;
  char* cursor_ = nullptr;
};

}  // namespace minikv
