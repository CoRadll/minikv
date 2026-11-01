#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "minikv/table/block.h"
#include "minikv/table/format.h"
#include "minikv/table/table_builder.h"
#include "minikv/util/coding.h"
#include "minikv/util/env.h"
#include "minikv/util/file.h"

#include "support/temp_dir.h"
#include "support/test_framework.h"

namespace {

std::string ReadWholeFile(const std::filesystem::path& path) {
  minikv::File file;
  if (!file.Open(path, minikv::FileOpenMode::kReadOnly).ok()) {
    return {};
  }
  std::string content;
  file.Read(0, static_cast<std::size_t>(file.Size()), &content);
  return content;
}

struct IndexEntry {
  std::string key;
  std::uint64_t offset = 0;
  std::uint64_t size = 0;
};

// 读文件尾部 footer → 解析索引块 → 列出每个数据块的上界 key 与位置
bool ReadIndex(const std::filesystem::path& path, std::vector<IndexEntry>* entries) {
  const std::string content = ReadWholeFile(path);
  if (content.size() < minikv::kTableFooterSize) {
    return false;
  }

  minikv::TableFooter footer;
  if (!minikv::DecodeFooter(std::string_view(content).substr(content.size() - minikv::kTableFooterSize),
                            &footer)) {
    return false;
  }
  if (footer.index_offset + footer.index_size > content.size()) {
    return false;
  }

  const std::string index_raw =
      content.substr(static_cast<std::size_t>(footer.index_offset),
                     static_cast<std::size_t>(footer.index_size));
  minikv::Block index_block(index_raw);
  if (!index_block.valid()) {
    return false;
  }

  entries->clear();
  const minikv::Status status = index_block.ForEach(
      [entries](std::string_view key, std::string_view value, bool deleted) {
        if (deleted) {
          return false;
        }
        std::string_view view = value;
        IndexEntry entry;
        entry.key.assign(key.data(), key.size());
        std::uint32_t size32 = 0;
        if (!minikv::GetFixed64(&view, &entry.offset) || !minikv::GetFixed32(&view, &size32)) {
          return false;
        }
        entry.size = size32;
        entries->push_back(entry);
        return true;
      });
  return status.ok();
}

}  // namespace

TEST(TableBuilder, WritesSingleBlockTable) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = dir / "000001.sst";

  minikv::TableBuilder builder(path);
  for (int i = 0; i < 10; ++i) {
    REQUIRE_OK(builder.Add("key" + std::to_string(i), "value" + std::to_string(i)));
  }
  REQUIRE_OK(builder.Finish());

  CHECK_TRUE(builder.finished());
  CHECK_EQ(builder.NumEntries(), static_cast<std::uint64_t>(10));
  CHECK_EQ(builder.NumDataBlocks(), static_cast<std::uint64_t>(1));
  CHECK_STREQ(builder.smallest_key(), "key0");
  CHECK_STREQ(builder.largest_key(), "key9");

  std::vector<IndexEntry> index;
  REQUIRE_TRUE(ReadIndex(path, &index));
  REQUIRE_TRUE(index.size() == 1);
  CHECK_STREQ(index[0].key, "key9");  // 数据块的上界

  const std::string content = ReadWholeFile(path);
  const minikv::Block block(content.substr(static_cast<std::size_t>(index[0].offset),
                                           static_cast<std::size_t>(index[0].size)));
  REQUIRE_TRUE(block.valid());

  std::string value;
  CHECK_OK(block.Get("key5", &value, nullptr));
  CHECK_STREQ(value, "value5");
}

TEST(TableBuilder, SplitsIntoMultipleDataBlocks) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = dir / "000002.sst";

  minikv::TableBuilder builder(path, 256);  // 小阈值，强制多块
  constexpr int kCount = 100;
  for (int i = 0; i < kCount; ++i) {
    REQUIRE_OK(builder.Add("key" + std::to_string(1000 + i), std::string(20, 'v')));
  }
  REQUIRE_OK(builder.Finish());

  CHECK_TRUE(builder.NumDataBlocks() > 1);

  std::vector<IndexEntry> index;
  REQUIRE_TRUE(ReadIndex(path, &index));
  CHECK_EQ(index.size(), static_cast<std::size_t>(builder.NumDataBlocks()));

  // 每个索引项指向的块都应能正确读出其边界 key
  const std::string content = ReadWholeFile(path);
  int mismatches = 0;
  for (const IndexEntry& entry : index) {
    const minikv::Block block(content.substr(static_cast<std::size_t>(entry.offset),
                                             static_cast<std::size_t>(entry.size)));
    if (!block.valid()) {
      ++mismatches;
      continue;
    }
    std::string value;
    if (!block.Get(entry.key, &value, nullptr).ok()) {
      ++mismatches;
    }
  }
  CHECK_EQ(mismatches, 0);
}

TEST(TableBuilder, RejectsNonIncreasingKeys) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  minikv::TableBuilder builder(dir / "bad.sst");
  REQUIRE_OK(builder.Add("b", "1"));
  CHECK_EQ(builder.Add("a", "2").code(), minikv::StatusCode::kInvalidArgument);
  CHECK_EQ(builder.Add("b", "3").code(), minikv::StatusCode::kInvalidArgument);
}

TEST(TableBuilder, RejectsAddAfterFinish) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  minikv::TableBuilder builder(dir / "done.sst");
  REQUIRE_OK(builder.Add("a", "1"));
  REQUIRE_OK(builder.Finish());
  CHECK_EQ(builder.Add("b", "2").code(), minikv::StatusCode::kInvalidArgument);
}

TEST(TableBuilder, RejectsEmptyTable) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  minikv::TableBuilder builder(dir / "empty.sst");
  CHECK_EQ(builder.Finish().code(), minikv::StatusCode::kInvalidArgument);
}

TEST(TableBuilder, TombstoneIsWrittenWithoutValue) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = dir / "tomb.sst";

  minikv::TableBuilder builder(path);
  REQUIRE_OK(builder.Add("alive", "v"));
  REQUIRE_OK(builder.Add("dead", "should-be-dropped", /*deleted=*/true));
  REQUIRE_OK(builder.Finish());

  std::vector<IndexEntry> index;
  REQUIRE_TRUE(ReadIndex(path, &index));

  const std::string content = ReadWholeFile(path);
  const minikv::Block block(content.substr(static_cast<std::size_t>(index[0].offset),
                                           static_cast<std::size_t>(index[0].size)));
  REQUIRE_TRUE(block.valid());

  std::string value = "stale";
  bool deleted = false;
  CHECK_OK(block.Get("dead", &value, &deleted));
  CHECK_TRUE(deleted);
  CHECK_TRUE(value.empty());
}

TEST(TableBuilder, FileSizeMatchesActualFile) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = dir / "size.sst";

  minikv::TableBuilder builder(path);
  REQUIRE_OK(builder.Add("k", "v"));
  REQUIRE_OK(builder.Finish());

  std::uint64_t actual = 0;
  CHECK_OK(minikv::FileSize(path, &actual));
  CHECK_EQ(builder.FileSize(), actual);
  CHECK_TRUE(actual > minikv::kTableFooterSize);
}

TEST(TableBuilder, CorruptedMagicIsRejected) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = dir / "magic.sst";

  minikv::TableBuilder builder(path);
  REQUIRE_OK(builder.Add("k", "v"));
  REQUIRE_OK(builder.Finish());

  std::string content = ReadWholeFile(path);
  const std::size_t footer_at = content.size() - minikv::kTableFooterSize;
  content[footer_at] = 'X';

  minikv::TableFooter footer;
  CHECK_FALSE(minikv::DecodeFooter(
      std::string_view(content).substr(footer_at), &footer));
}
