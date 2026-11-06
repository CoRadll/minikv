#include "minikv/util/arena.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace minikv {
namespace {

constexpr std::size_t kBlockSize = 4096;
constexpr std::size_t kAlignment = alignof(std::max_align_t);

std::size_t RoundUp(std::size_t bytes, std::size_t alignment) {
  return (bytes + alignment - 1) / alignment * alignment;
}

}  // namespace

struct Arena::Block {
  char* data = nullptr;
  std::size_t size = 0;
};

Arena::Arena() = default;

Arena::~Arena() { Reset(); }

char* Arena::Allocate(std::size_t bytes) { return AllocateAligned(bytes, kAlignment); }

char* Arena::AllocateAligned(std::size_t bytes, std::size_t alignment) {
  if (bytes == 0) {
    bytes = 1;
  }
  if (alignment == 0) {
    alignment = kAlignment;
  }

  // 块内对齐最多浪费 alignment-1 字节，按这个上界判断是否需要新块
  const std::size_t needed = bytes + alignment - 1;
  if (cursor_ == nullptr || remaining_ < needed) {
    const std::size_t block_size = std::max(kBlockSize, needed);
    auto* block = new Block();
    block->data = static_cast<char*>(std::malloc(block_size));
    block->size = block_size;
    if (block->data == nullptr) {
      delete block;
      return nullptr;
    }
    blocks_.push_back(block);
    allocated_bytes_ += block_size;
    cursor_ = block->data;
    remaining_ = block_size;
  }

  const auto address = reinterpret_cast<std::uintptr_t>(cursor_);
  const std::size_t padding = (alignment - (address % alignment)) % alignment;
  char* result = cursor_ + padding;

  cursor_ = result + bytes;
  remaining_ -= padding + bytes;
  return result;
}

void Arena::Reset() {
  for (Block* block : blocks_) {
    std::free(block->data);
    delete block;
  }
  blocks_.clear();
  allocated_bytes_ = 0;
  remaining_ = 0;
  cursor_ = nullptr;
}

}  // namespace minikv
