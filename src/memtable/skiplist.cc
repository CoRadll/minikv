#include "minikv/memtable/skiplist.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "minikv/util/arena.h"

namespace minikv {

struct SkipList::Node {
  std::uint32_t key_size = 0;
  std::uint32_t value_size = 0;
  const char* value_data = nullptr;  // 指向 Arena 中的 value 字节；墓碑为 nullptr
  bool deleted = false;
  int height = 1;
  Node* next[1];  // 变长：实际分配 height 个指针
};

namespace {

constexpr std::size_t kNodeHeaderOffset = offsetof(SkipList::Node, next);

}  // namespace

std::size_t SkipList::NodeBytes(int height) {
  return kNodeHeaderOffset + sizeof(Node*) * static_cast<std::size_t>(height);
}

std::string_view SkipList::KeyOf(const Node* node) {
  const char* payload = reinterpret_cast<const char*>(node) + NodeBytes(node->height);
  return std::string_view(payload, node->key_size);
}

std::string_view SkipList::ValueOf(const Node* node) {
  if (node->value_data == nullptr || node->value_size == 0) {
    return std::string_view();
  }
  return std::string_view(node->value_data, node->value_size);
}

SkipList::SkipList(Arena* arena, std::uint64_t seed)
    : arena_(arena), head_(nullptr), rng_state_(seed == 0 ? 1 : seed) {
  head_ = NewNode(std::string_view(), std::string_view(), false, kMaxHeight);
}

SkipList::Node* SkipList::NewNode(std::string_view key, std::string_view value, bool deleted,
                                  int height) {
  // key 内联在节点尾部（key 不可变）；value 单独分配，便于覆盖时只换指针
  char* raw = arena_->AllocateAligned(NodeBytes(height) + key.size(), alignof(Node*));
  auto* node = reinterpret_cast<Node*>(raw);
  node->key_size = static_cast<std::uint32_t>(key.size());
  node->height = height;
  node->deleted = deleted;
  node->value_data = nullptr;
  node->value_size = 0;

  char* payload = raw + NodeBytes(height);
  if (!key.empty()) {
    std::memcpy(payload, key.data(), key.size());
  }

  if (!deleted && !value.empty()) {
    char* value_bytes = arena_->AllocateAligned(value.size(), alignof(char));
    std::memcpy(value_bytes, value.data(), value.size());
    node->value_data = value_bytes;
    node->value_size = static_cast<std::uint32_t>(value.size());
  }

  for (int i = 0; i < height; ++i) {
    node->next[i] = nullptr;
  }
  return node;
}

int SkipList::RandomHeight() {
  int height = 1;
  while (height < kMaxHeight) {
    // xorshift64*：便宜且分布够用
    rng_state_ ^= rng_state_ >> 12;
    rng_state_ ^= rng_state_ << 25;
    rng_state_ ^= rng_state_ >> 27;
    const std::uint64_t value = rng_state_ * 0x2545F4914F6CDD1Dull;
    if ((value & 3u) != 0u) {
      break;  // 1/4 概率继续升层
    }
    ++height;
  }
  return height;
}

SkipList::Node* SkipList::FindGreaterOrEqual(std::string_view key, Node** prev) const {
  Node* node = head_;
  for (int level = max_height_ - 1; level >= 0; --level) {
    Node* next = node->next[level];
    while (next != nullptr && KeyOf(next) < key) {
      node = next;
      next = node->next[level];
    }
    if (prev != nullptr) {
      prev[level] = node;
    }
  }
  return node->next[0];
}

SkipList::InsertResult SkipList::Insert(std::string_view key, std::string_view value, bool deleted) {
  Node* prev[kMaxHeight];
  for (int i = 0; i < kMaxHeight; ++i) {
    prev[i] = head_;
  }

  Node* found = FindGreaterOrEqual(key, prev);

  InsertResult result;
  if (found != nullptr && KeyOf(found) == key) {
    result.existed = true;
    result.was_deleted = found->deleted;
    result.old_value_size = found->value_size;

    if (!deleted && !value.empty()) {
      char* value_bytes = arena_->AllocateAligned(value.size(), alignof(char));
      std::memcpy(value_bytes, value.data(), value.size());
      found->value_data = value_bytes;
      found->value_size = static_cast<std::uint32_t>(value.size());
    } else {
      found->value_data = nullptr;
      found->value_size = 0;
    }
    found->deleted = deleted;
    return result;
  }

  const int height = RandomHeight();
  if (height > max_height_) {
    max_height_ = height;
  }

  Node* node = NewNode(key, value, deleted, height);
  for (int level = 0; level < height; ++level) {
    node->next[level] = prev[level]->next[level];
    prev[level]->next[level] = node;
  }
  ++size_;
  return result;
}

SkipList::LookupResult SkipList::Lookup(std::string_view key, std::string* value) const {
  const Node* node = FindGreaterOrEqual(key, nullptr);
  if (node == nullptr || KeyOf(node) != key) {
    return LookupResult::kNotFound;
  }
  if (node->deleted) {
    return LookupResult::kDeleted;
  }
  if (value != nullptr) {
    const std::string_view stored = ValueOf(node);
    value->assign(stored.data(), stored.size());
  }
  return LookupResult::kFound;
}

bool SkipList::Contains(std::string_view key) const {
  return Lookup(key, nullptr) == LookupResult::kFound;
}

void SkipList::ForEach(
    const std::function<bool(std::string_view key, std::string_view value, bool deleted)>& fn) const {
  if (!fn) {
    return;
  }
  for (const Node* node = head_->next[0]; node != nullptr; node = node->next[0]) {
    if (!fn(KeyOf(node), ValueOf(node), node->deleted)) {
      return;
    }
  }
}

}  // namespace minikv
