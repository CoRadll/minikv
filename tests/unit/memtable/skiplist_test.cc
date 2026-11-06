#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "minikv/memtable/skiplist.h"
#include "minikv/util/arena.h"

#include "support/test_framework.h"

namespace {

std::string Key(int index) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "key%04d", index);
  return std::string(buffer);
}

}  // namespace

TEST(SkipList, InsertAndLookupManyKeys) {
  minikv::Arena arena;
  minikv::SkipList list(&arena);

  constexpr int kCount = 1000;
  for (int i = kCount - 1; i >= 0; --i) {  // 倒序插入，验证不依赖插入顺序
    list.Insert(Key(i), "value" + std::to_string(i), false);
  }
  CHECK_EQ(list.EntryCount(), static_cast<std::size_t>(kCount));

  int mismatches = 0;
  for (int i = 0; i < kCount; ++i) {
    std::string value;
    if (list.Lookup(Key(i), &value) != minikv::SkipList::LookupResult::kFound ||
        value != "value" + std::to_string(i)) {
      ++mismatches;
    }
  }
  CHECK_EQ(mismatches, 0);

  std::string value;
  CHECK_EQ(list.Lookup("missing", &value), minikv::SkipList::LookupResult::kNotFound);
}

TEST(SkipList, ForEachYieldsSortedOrder) {
  minikv::Arena arena;
  minikv::SkipList list(&arena);

  constexpr int kCount = 500;
  for (int i = 0; i < kCount; ++i) {
    list.Insert(Key((i * 37) % kCount), "v", false);
  }

  std::string previous;
  int out_of_order = 0;
  int visited = 0;
  list.ForEach([&](std::string_view key, std::string_view, bool) {
    const std::string current(key);
    if (!previous.empty() && current <= previous) {
      ++out_of_order;
    }
    previous = current;
    ++visited;
    return true;
  });

  CHECK_EQ(out_of_order, 0);
  CHECK_EQ(visited, kCount);
}

TEST(SkipList, OverwriteUpdatesValueAndReportsOldSize) {
  minikv::Arena arena;
  minikv::SkipList list(&arena);

  const minikv::SkipList::InsertResult first = list.Insert("k", "v1", false);
  CHECK_FALSE(first.existed);
  CHECK_EQ(first.old_value_size, static_cast<std::size_t>(0));

  const minikv::SkipList::InsertResult second = list.Insert("k", "value-2", false);
  CHECK_TRUE(second.existed);
  CHECK_FALSE(second.was_deleted);
  CHECK_EQ(second.old_value_size, static_cast<std::size_t>(2));

  CHECK_EQ(list.EntryCount(), static_cast<std::size_t>(1));

  std::string value;
  CHECK_EQ(list.Lookup("k", &value), minikv::SkipList::LookupResult::kFound);
  CHECK_STREQ(value, "value-2");
}

TEST(SkipList, DeleteMarksTombstone) {
  minikv::Arena arena;
  minikv::SkipList list(&arena);

  list.Insert("k", "v", false);
  const minikv::SkipList::InsertResult result = list.Insert("k", std::string_view(), true);
  CHECK_TRUE(result.existed);
  CHECK_FALSE(result.was_deleted);
  CHECK_EQ(result.old_value_size, static_cast<std::size_t>(1));

  std::string value;
  CHECK_EQ(list.Lookup("k", &value), minikv::SkipList::LookupResult::kDeleted);
  CHECK_FALSE(list.Contains("k"));
  // 墓碑仍占一个条目
  CHECK_EQ(list.EntryCount(), static_cast<std::size_t>(1));
}

TEST(SkipList, PutBackAfterDelete) {
  minikv::Arena arena;
  minikv::SkipList list(&arena);

  list.Insert("k", "v1", false);
  list.Insert("k", std::string_view(), true);
  const minikv::SkipList::InsertResult result = list.Insert("k", "v2", false);
  CHECK_TRUE(result.existed);
  CHECK_TRUE(result.was_deleted);

  std::string value;
  CHECK_EQ(list.Lookup("k", &value), minikv::SkipList::LookupResult::kFound);
  CHECK_STREQ(value, "v2");
}

TEST(SkipList, AllocatesFromArena) {
  minikv::Arena arena;
  CHECK_EQ(arena.MemoryUsage(), static_cast<std::size_t>(0));

  minikv::SkipList list(&arena);
  for (int i = 0; i < 100; ++i) {
    list.Insert(Key(i), "value", false);
  }
  CHECK_TRUE(arena.MemoryUsage() > 0);
}

TEST(SkipList, HandlesBinaryKeys) {
  minikv::Arena arena;
  minikv::SkipList list(&arena);

  const std::string key("\0\1\2", 3);
  list.Insert(key, "bin", false);

  std::string value;
  CHECK_EQ(list.Lookup(key, &value), minikv::SkipList::LookupResult::kFound);
  CHECK_STREQ(value, "bin");
}
