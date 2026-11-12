#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "minikv/table/block.h"
#include "minikv/table/format.h"
#include "minikv/table/table_builder.h"
#include "minikv/table/table_reader.h"
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

void WriteWholeFile(const std::filesystem::path& path, const std::string& content) {
  minikv::File file;
  REQUIRE_OK(file.Open(path, minikv::FileOpenMode::kReadWrite));
  REQUIRE_OK(file.Truncate(0));
  REQUIRE_OK(file.Append(content));
  REQUIRE_OK(file.Sync());
}

std::filesystem::path BuildTable(const std::filesystem::path& dir, int count,
                                 std::size_t block_size = 256) {
  const auto path = dir / "000001.sst";
  minikv::TableBuilder builder(path, block_size);
  for (int i = 0; i < count; ++i) {
    char key[32];
    std::snprintf(key, sizeof(key), "key%04d", i);
    // 注意：REQUIRE_* 展开成 return;，不能用在非 void 函数里
    if (!builder.Add(key, "value" + std::to_string(i)).ok()) {
      return {};
    }
  }
  if (!builder.Finish().ok()) {
    return {};
  }
  return path;
}

// 读取 idx 号数据块的偏移/长度（用于构造损坏文件）
bool FirstBlockRange(const std::filesystem::path& path, std::uint64_t* offset,
                     std::uint64_t* size) {
  const std::string content = ReadWholeFile(path);
  if (content.size() < minikv::kTableFooterSize) {
    return false;
  }
  minikv::TableFooter footer;
  if (!minikv::DecodeFooter(
          std::string_view(content).substr(content.size() - minikv::kTableFooterSize), &footer)) {
    return false;
  }
  const std::string index_raw = content.substr(static_cast<std::size_t>(footer.index_offset),
                                               static_cast<std::size_t>(footer.index_size));
  const minikv::Block index(std::move(index_raw));
  if (!index.valid()) {
    return false;
  }
  bool found = false;
  index.ForEach([&](std::string_view, std::string_view value, bool) {
    std::string_view view = value;
    std::uint32_t size32 = 0;
    if (!minikv::GetFixed64(&view, offset) || !minikv::GetFixed32(&view, &size32)) {
      return false;
    }
    *size = size32;
    found = true;
    return false;  // 只要第一个
  });
  return found;
}

}  // namespace

TEST(TableReader, ReadsAllEntriesAcrossBlocks) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = BuildTable(dir.path(), 200);

  std::unique_ptr<minikv::TableReader> reader;
  REQUIRE_OK(minikv::TableReader::Open(path, &reader));
  REQUIRE_TRUE(reader != nullptr);

  int mismatches = 0;
  for (int i = 0; i < 200; ++i) {
    char key[32];
    std::snprintf(key, sizeof(key), "key%04d", i);
    std::string value;
    if (!reader->Get(key, &value, nullptr).ok() || value != "value" + std::to_string(i)) {
      ++mismatches;
    }
  }
  CHECK_EQ(mismatches, 0);
}

TEST(TableReader, MissingKeyReturnsNotFound) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = BuildTable(dir.path(), 50);

  std::unique_ptr<minikv::TableReader> reader;
  REQUIRE_OK(minikv::TableReader::Open(path, &reader));

  std::string value;
  CHECK_TRUE(reader->Get("key0000before", &value, nullptr).IsNotFound());
  CHECK_TRUE(reader->Get("key002500", &value, nullptr).IsNotFound());
  CHECK_TRUE(reader->Get("zzzz", &value, nullptr).IsNotFound());
}

TEST(TableReader, TombstoneIsVisible) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = dir / "tomb.sst";

  {
    minikv::TableBuilder builder(path);
    REQUIRE_OK(builder.Add("alive", "v"));
    REQUIRE_OK(builder.Add("dead", "", /*deleted=*/true));
    REQUIRE_OK(builder.Finish());
  }

  std::unique_ptr<minikv::TableReader> reader;
  REQUIRE_OK(minikv::TableReader::Open(path, &reader));

  std::string value = "stale";
  bool deleted = false;
  CHECK_OK(reader->Get("dead", &value, &deleted));
  CHECK_TRUE(deleted);
  CHECK_TRUE(value.empty());

  CHECK_OK(reader->Get("alive", &value, &deleted));
  CHECK_FALSE(deleted);
  CHECK_STREQ(value, "v");
}

TEST(TableReader, RejectsTooShortFile) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = dir / "short.sst";
  WriteWholeFile(path, std::string(10, 'x'));

  std::unique_ptr<minikv::TableReader> reader;
  CHECK_EQ(minikv::TableReader::Open(path, &reader).code(),
           minikv::StatusCode::kCorruption);
}

TEST(TableReader, RejectsBadFooterMagic) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = BuildTable(dir.path(), 5);

  std::string content = ReadWholeFile(path);
  const std::size_t footer_at = content.size() - minikv::kTableFooterSize;
  content[footer_at] = 'X';
  WriteWholeFile(path, content);

  std::unique_ptr<minikv::TableReader> reader;
  CHECK_EQ(minikv::TableReader::Open(path, &reader).code(),
           minikv::StatusCode::kCorruption);
}

TEST(TableReader, RejectsCorruptedDataBlock) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = BuildTable(dir.path(), 100);

  std::uint64_t offset = 0;
  std::uint64_t size = 0;
  REQUIRE_TRUE(FirstBlockRange(path, &offset, &size));
  REQUIRE_TRUE(size > 10);

  std::string content = ReadWholeFile(path);
  content[static_cast<std::size_t>(offset) + 5] =
      static_cast<char>(content[static_cast<std::size_t>(offset) + 5] ^ 0x7F);
  WriteWholeFile(path, content);

  std::unique_ptr<minikv::TableReader> reader;
  REQUIRE_OK(minikv::TableReader::Open(path, &reader));

  std::string value;
  CHECK_EQ(reader->Get("key0000", &value, nullptr).code(), minikv::StatusCode::kCorruption);
}

TEST(TableReader, ReportsFileSize) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = BuildTable(dir.path(), 20);

  std::uint64_t on_disk = 0;
  CHECK_OK(minikv::FileSize(path, &on_disk));

  std::unique_ptr<minikv::TableReader> reader;
  REQUIRE_OK(minikv::TableReader::Open(path, &reader));
  CHECK_EQ(reader->file_size(), on_disk);
}

TEST(TableReader, RejectsMissingFile) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  std::unique_ptr<minikv::TableReader> reader;
  const minikv::Status status = minikv::TableReader::Open(dir / "nope.sst", &reader);
  CHECK_FALSE(status.ok());
  CHECK_EQ(status.code(), minikv::StatusCode::kIOError);
}

TEST(TableReader, SstableCarriesBloomFilter) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = BuildTable(dir.path(), 100);

  const std::string content = ReadWholeFile(path);
  REQUIRE_TRUE(content.size() > minikv::kTableFooterSize);

  minikv::TableFooter footer;
  REQUIRE_TRUE(minikv::DecodeFooter(
      std::string_view(content).substr(content.size() - minikv::kTableFooterSize), &footer));

  // 写文件时应把 Bloom filter 一并写进去，并记录在 footer 里
  CHECK_TRUE(footer.filter_size > 0);
  CHECK_TRUE(footer.filter_offset < footer.index_offset);
  CHECK_TRUE(footer.filter_offset + footer.filter_size <= footer.index_offset);
}
