#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "minikv/table/block.h"
#include "minikv/table/block_builder.h"

#include "support/test_framework.h"

namespace {

using Entry = std::pair<std::string, std::string>;

std::string BuildBlock(const std::vector<Entry>& entries, bool deleted_last = false) {
  minikv::BlockBuilder builder;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const bool deleted = deleted_last && (i + 1 == entries.size());
    builder.Add(entries[i].first, entries[i].second, deleted);
  }
  return builder.Finish();
}

std::vector<Entry> MakeEntries(int count) {
  std::vector<Entry> entries;
  for (int i = 0; i < count; ++i) {
    entries.emplace_back("key" + std::to_string(1000 + i), "value" + std::to_string(i));
  }
  return entries;
}

}  // namespace

TEST(Block, SingleEntryRoundTrip) {
  const std::string raw = BuildBlock({{"user:1", "zhangsan"}});
  minikv::Block block(raw);

  CHECK_TRUE(block.valid());

  std::string value;
  bool deleted = true;
  CHECK_OK(block.Get("user:1", &value, &deleted));
  CHECK_STREQ(value, "zhangsan");
  CHECK_FALSE(deleted);
}

TEST(Block, AllEntriesAreFound) {
  const std::vector<Entry> entries = MakeEntries(100);
  minikv::Block block(BuildBlock(entries));
  REQUIRE_TRUE(block.valid());

  int mismatches = 0;
  for (const auto& entry : entries) {
    std::string value;
    if (!block.Get(entry.first, &value, nullptr).ok() || value != entry.second) {
      ++mismatches;
    }
  }
  CHECK_EQ(mismatches, 0);
}

TEST(Block, MissingKeyReturnsNotFound) {
  minikv::Block block(BuildBlock(MakeEntries(50)));
  REQUIRE_TRUE(block.valid());

  std::string value;

  // 落在范围内但不存在
  CHECK_TRUE(block.Get("key1025.5", &value, nullptr).IsNotFound());
  // 小于最小值
  CHECK_TRUE(block.Get("aaa", &value, nullptr).IsNotFound());
  // 大于最大值
  CHECK_TRUE(block.Get("zzz", &value, nullptr).IsNotFound());
}

TEST(Block, PrefixCompressionShrinksBlock) {
  std::vector<Entry> entries;
  const std::string shared_prefix(32, 'k');
  for (int i = 0; i < 100; ++i) {
    entries.emplace_back(shared_prefix + std::to_string(i), std::string(20, 'v'));
  }

  std::size_t raw_bytes = 0;
  for (const auto& entry : entries) {
    raw_bytes += entry.first.size() + entry.second.size();
  }

  const std::string encoded = BuildBlock(entries);
  CHECK_TRUE(encoded.size() < raw_bytes);
  // 长公共前缀应该带来显著压缩
  CHECK_TRUE(encoded.size() < raw_bytes * 7 / 10);
}

TEST(Block, RestartCountFollowsInterval) {
  minikv::BlockBuilder builder;
  for (int i = 0; i < 40; ++i) {
    builder.Add("key" + std::to_string(i), "v");
  }
  minikv::Block block(builder.Finish());

  REQUIRE_TRUE(block.valid());
  // 第 1 条 + 第 17 条 + 第 33 条
  CHECK_EQ(block.NumRestarts(), static_cast<std::size_t>(3));
}

TEST(Block, EmptyBlockIsValidAndNotFound) {
  minikv::BlockBuilder builder;
  minikv::Block block(builder.Finish());

  CHECK_TRUE(block.valid());
  std::string value;
  CHECK_TRUE(block.Get("anything", &value, nullptr).IsNotFound());
}

TEST(Block, CorruptedBlockIsInvalid) {
  std::string raw = BuildBlock(MakeEntries(20));
  raw[raw.size() / 2] = static_cast<char>(raw[raw.size() / 2] ^ 0x5A);

  minikv::Block block(raw);
  CHECK_FALSE(block.valid());

  std::string value;
  CHECK_EQ(block.Get("key1000", &value, nullptr).code(), minikv::StatusCode::kCorruption);
}

TEST(Block, TruncatedBlockIsInvalid) {
  std::string raw = BuildBlock(MakeEntries(10));
  minikv::Block block(raw.substr(0, raw.size() - 5));
  CHECK_FALSE(block.valid());
}

TEST(Block, DeletedFlagIsPreserved) {
  minikv::Block block(BuildBlock(MakeEntries(5), true));
  REQUIRE_TRUE(block.valid());

  std::string value = "stale";
  bool deleted = false;
  CHECK_OK(block.Get("key1004", &value, &deleted));
  CHECK_TRUE(deleted);
  CHECK_TRUE(value.empty());

  // 其他条目不受影响
  CHECK_OK(block.Get("key1000", &value, &deleted));
  CHECK_FALSE(deleted);
}

TEST(Block, HandlesBinaryKeys) {
  const std::string key1("\0\1\2", 3);
  const std::string key2("\0\1\3", 3);
  minikv::Block block(BuildBlock({{key1, "a"}, {key2, "b"}}));
  REQUIRE_TRUE(block.valid());

  std::string value;
  CHECK_OK(block.Get(key2, &value, nullptr));
  CHECK_STREQ(value, "b");
}

TEST(Block, SizeEstimateGrowsWithEntries) {
  minikv::BlockBuilder builder;
  // 初始：1 个重启点占位 4 字节 + 尾部 8 字节
  CHECK_EQ(builder.CurrentSizeEstimate(), static_cast<std::size_t>(12));

  builder.Add("a", "1");
  const std::size_t after_one = builder.CurrentSizeEstimate();
  CHECK_TRUE(after_one > 12);

  builder.Add("b", "2");
  CHECK_TRUE(builder.CurrentSizeEstimate() > after_one);
}
