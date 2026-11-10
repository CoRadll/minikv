#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "minikv/api/iterator.h"
#include "minikv/table/merging_iterator.h"

#include "support/test_framework.h"

namespace {

// 用内存数组模拟一个有序来源，验证归并逻辑本身
class FakeIterator final : public minikv::Iterator {
 public:
  struct Entry {
    std::string key;
    std::string value;
    bool deleted = false;
  };

  explicit FakeIterator(std::vector<Entry> entries) : entries_(std::move(entries)) {}

  bool Valid() const override { return index_ < entries_.size(); }

  void SeekToFirst() override { index_ = 0; }

  void Seek(std::string_view target) override {
    index_ = 0;
    while (index_ < entries_.size() && entries_[index_].key < target) {
      ++index_;
    }
  }

  void Next() override {
    if (index_ < entries_.size()) {
      ++index_;
    }
  }

  std::string_view key() const override {
    return Valid() ? std::string_view(entries_[index_].key) : std::string_view();
  }

  std::string_view value() const override {
    return Valid() ? std::string_view(entries_[index_].value) : std::string_view();
  }

  bool IsDeleted() const override { return Valid() && entries_[index_].deleted; }

  minikv::Status status() const override { return minikv::Status::Ok(); }

 private:
  std::vector<Entry> entries_;
  std::size_t index_ = 0;
};

std::vector<FakeIterator::Entry> MakeEntries(
    std::initializer_list<std::pair<const char*, const char*>> items) {
  std::vector<FakeIterator::Entry> entries;
  for (const auto& item : items) {
    entries.push_back(FakeIterator::Entry{item.first, item.second, false});
  }
  return entries;
}

}  // namespace

TEST(MergingIterator, MergesSourcesInKeyOrder) {
  std::vector<std::unique_ptr<minikv::Iterator>> children;
  children.push_back(std::make_unique<FakeIterator>(MakeEntries({{"a", "new"}, {"b", "2"}})));
  children.push_back(std::make_unique<FakeIterator>(MakeEntries({{"a", "old"}, {"c", "3"}})));

  minikv::MergingIterator merged(std::move(children));
  merged.SeekToFirst();

  std::vector<std::string> keys;
  std::vector<std::string> values;
  for (; merged.Valid(); merged.Next()) {
    keys.emplace_back(merged.key());
    values.emplace_back(merged.value());
  }

  // 归并器不去重：同 key 会按"新的在前"各出现一次，去重由 Compaction 负责
  REQUIRE_TRUE(keys.size() == 4);
  CHECK_EQ(keys[0], std::string("a"));
  CHECK_EQ(keys[1], std::string("a"));
  CHECK_EQ(keys[2], std::string("b"));
  CHECK_EQ(keys[3], std::string("c"));
  // 下标小的来源更新，所以第一个 a 取 "new"，第二个取 "old"
  CHECK_EQ(values[0], std::string("new"));
  CHECK_EQ(values[1], std::string("old"));
}

TEST(MergingIterator, NewerSourceWinsForSameKey) {
  std::vector<std::unique_ptr<minikv::Iterator>> children;
  children.push_back(std::make_unique<FakeIterator>(MakeEntries({{"k", "v3"}})));
  children.push_back(std::make_unique<FakeIterator>(MakeEntries({{"k", "v2"}})));
  children.push_back(std::make_unique<FakeIterator>(MakeEntries({{"k", "v1"}})));

  minikv::MergingIterator merged(std::move(children));
  merged.SeekToFirst();
  REQUIRE_TRUE(merged.Valid());
  CHECK_STREQ(merged.value(), "v3");
}

TEST(MergingIterator, SeekAdvancesAllChildren) {
  std::vector<std::unique_ptr<minikv::Iterator>> children;
  children.push_back(std::make_unique<FakeIterator>(MakeEntries({{"a", "1"}, {"c", "3"}})));
  children.push_back(std::make_unique<FakeIterator>(MakeEntries({{"b", "2"}, {"d", "4"}})));

  minikv::MergingIterator merged(std::move(children));
  merged.Seek("b");

  REQUIRE_TRUE(merged.Valid());
  CHECK_STREQ(merged.key(), "b");

  merged.Next();
  REQUIRE_TRUE(merged.Valid());
  CHECK_STREQ(merged.key(), "c");
  merged.Next();
  CHECK_STREQ(merged.key(), "d");
  merged.Next();
  CHECK_FALSE(merged.Valid());
}

TEST(MergingIterator, PropagatesTombstoneFlag) {
  std::vector<FakeIterator::Entry> entries;
  entries.push_back(FakeIterator::Entry{"gone", "", true});

  std::vector<std::unique_ptr<minikv::Iterator>> children;
  children.push_back(std::make_unique<FakeIterator>(std::move(entries)));

  minikv::MergingIterator merged(std::move(children));
  merged.SeekToFirst();
  REQUIRE_TRUE(merged.Valid());
  CHECK_TRUE(merged.IsDeleted());
}
