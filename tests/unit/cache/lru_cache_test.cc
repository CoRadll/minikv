#include <cstddef>
#include <cstdint>
#include <string>

#include "minikv/cache/lru_cache.h"

#include "support/test_framework.h"

TEST(LruCache, MissThenHit) {
  minikv::LruCache cache(1024);

  std::string out;
  CHECK_FALSE(cache.Lookup(1, 0, &out));
  cache.Insert(1, 0, "block-data");
  CHECK_TRUE(cache.Lookup(1, 0, &out));
  CHECK_STREQ(out, "block-data");

  CHECK_EQ(cache.hits(), static_cast<std::uint64_t>(1));
  CHECK_EQ(cache.misses(), static_cast<std::uint64_t>(1));
}

TEST(LruCache, EvictsLeastRecentlyUsed) {
  minikv::LruCache cache(200);
  cache.Insert(1, 0, std::string(100, 'a'));
  cache.Insert(2, 0, std::string(100, 'b'));

  // 访问 1，使 2 变成最久未使用
  std::string out;
  CHECK_TRUE(cache.Lookup(1, 0, &out));

  cache.Insert(3, 0, std::string(100, 'c'));  // 应淘汰 2

  CHECK_TRUE(cache.Lookup(1, 0, &out));
  CHECK_FALSE(cache.Lookup(2, 0, &out));
  CHECK_TRUE(cache.Lookup(3, 0, &out));
  CHECK_TRUE(cache.UsedBytes() <= 200);
}

TEST(LruCache, OverwritesExistingKey) {
  minikv::LruCache cache(1024);
  cache.Insert(5, 8, "old");
  cache.Insert(5, 8, "new-value");

  std::string out;
  CHECK_TRUE(cache.Lookup(5, 8, &out));
  CHECK_STREQ(out, "new-value");
  CHECK_EQ(cache.Count(), static_cast<std::size_t>(1));
}

TEST(LruCache, RejectsOversizedEntry) {
  minikv::LruCache cache(100);
  cache.Insert(1, 0, std::string(200, 'x'));

  std::string out;
  CHECK_FALSE(cache.Lookup(1, 0, &out));
  CHECK_EQ(cache.UsedBytes(), static_cast<std::size_t>(0));
}

TEST(LruCache, ClearDropsEntries) {
  minikv::LruCache cache(1024);
  cache.Insert(1, 0, "data");
  cache.Clear();

  std::string out;
  CHECK_FALSE(cache.Lookup(1, 0, &out));
  CHECK_EQ(cache.UsedBytes(), static_cast<std::size_t>(0));
  CHECK_EQ(cache.Count(), static_cast<std::size_t>(0));
}

TEST(LruCache, DifferentOffsetsAreDifferentBlocks) {
  minikv::LruCache cache(1024);
  cache.Insert(1, 0, "first");
  cache.Insert(1, 4096, "second");

  std::string out;
  CHECK_TRUE(cache.Lookup(1, 0, &out));
  CHECK_STREQ(out, "first");
  CHECK_TRUE(cache.Lookup(1, 4096, &out));
  CHECK_STREQ(out, "second");
}
