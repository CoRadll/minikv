#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "minikv/api/iterator.h"
#include "minikv/table/table_builder.h"
#include "minikv/table/table_reader.h"

#include "support/temp_dir.h"
#include "support/test_framework.h"

namespace {

std::filesystem::path BuildTable(const std::filesystem::path& dir, int count,
                                 std::size_t block_size = 256) {
  const auto path = dir / "iter.sst";
  minikv::TableBuilder builder(path, block_size);
  for (int i = 0; i < count; ++i) {
    char key[32];
    std::snprintf(key, sizeof(key), "key%04d", i);
    if (!builder.Add(key, "value" + std::to_string(i)).ok()) {
      return {};
    }
  }
  if (!builder.Finish().ok()) {
    return {};
  }
  return path;
}

}  // namespace

TEST(TableIterator, VisitsAllEntriesInOrder) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = BuildTable(dir.path(), 200);
  REQUIRE_TRUE(!path.empty());

  std::unique_ptr<minikv::TableReader> reader;
  REQUIRE_OK(minikv::TableReader::Open(path, &reader));

  std::unique_ptr<minikv::Iterator> it = reader->NewIterator();
  REQUIRE_TRUE(it != nullptr);

  int visited = 0;
  int out_of_order = 0;
  std::string previous;

  for (it->SeekToFirst(); it->Valid(); it->Next()) {
    const std::string key(it->key());
    if (!previous.empty() && key <= previous) {
      ++out_of_order;
    }
    previous = key;
    ++visited;
  }

  CHECK_OK(it->status());
  CHECK_EQ(visited, 200);
  CHECK_EQ(out_of_order, 0);
}

TEST(TableIterator, SeekLandsOnFirstGreaterOrEqual) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = BuildTable(dir.path(), 200);
  REQUIRE_TRUE(!path.empty());

  std::unique_ptr<minikv::TableReader> reader;
  REQUIRE_OK(minikv::TableReader::Open(path, &reader));
  std::unique_ptr<minikv::Iterator> it = reader->NewIterator();

  it->Seek("key0100");
  REQUIRE_TRUE(it->Valid());
  CHECK_STREQ(it->key(), "key0100");

  it->Seek("key0100x");  // 落在 key0100 与 key0101 之间
  REQUIRE_TRUE(it->Valid());
  CHECK_STREQ(it->key(), "key0101");

  it->Seek("zzz");  // 超过最大 key
  CHECK_FALSE(it->Valid());
}

TEST(TableIterator, ExposesTombstones) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = dir / "tomb.sst";

  {
    minikv::TableBuilder builder(path);
    REQUIRE_OK(builder.Add("a", "v"));
    REQUIRE_OK(builder.Add("b", std::string_view(), /*deleted=*/true));
    REQUIRE_OK(builder.Finish());
  }

  std::unique_ptr<minikv::TableReader> reader;
  REQUIRE_OK(minikv::TableReader::Open(path, &reader));
  std::unique_ptr<minikv::Iterator> it = reader->NewIterator();

  it->SeekToFirst();
  REQUIRE_TRUE(it->Valid());
  CHECK_STREQ(it->key(), "a");
  CHECK_FALSE(it->IsDeleted());

  it->Next();
  REQUIRE_TRUE(it->Valid());
  CHECK_STREQ(it->key(), "b");
  CHECK_TRUE(it->IsDeleted());
  CHECK_TRUE(it->value().empty());

  it->Next();
  CHECK_FALSE(it->Valid());
}
