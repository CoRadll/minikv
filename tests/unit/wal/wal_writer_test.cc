#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "minikv/util/env.h"
#include "minikv/util/file.h"
#include "minikv/wal/wal_format.h"
#include "minikv/wal/wal_writer.h"

#include "support/temp_dir.h"
#include "support/test_framework.h"

namespace {

minikv::WalRecord MakePut(const std::string& key, const std::string& value) {
  minikv::WalRecord record;
  record.type = minikv::RecordType::kPut;
  record.key = key;
  record.value = value;
  return record;
}

minikv::WalRecord MakeDelete(const std::string& key) {
  minikv::WalRecord record;
  record.type = minikv::RecordType::kDelete;
  record.key = key;
  return record;
}

std::size_t EncodedSize(const minikv::WalRecord& record) {
  std::string buffer;
  minikv::EncodeRecord(record, &buffer);
  return buffer.size();
}

bool ReadAll(const std::filesystem::path& path, std::vector<minikv::WalRecord>* out) {
  out->clear();

  minikv::File file;
  if (!file.Open(path, minikv::FileOpenMode::kReadOnly).ok()) {
    return false;
  }

  std::string content;
  if (!file.Read(0, static_cast<std::size_t>(file.Size()), &content).ok()) {
    return false;
  }

  std::string_view remaining(content);
  while (!remaining.empty()) {
    minikv::WalRecord record;
    std::size_t consumed = 0;
    if (!minikv::DecodeRecord(remaining, &record, &consumed)) {
      return false;
    }
    out->push_back(record);
    remaining.remove_prefix(consumed);
  }
  return true;
}

}  // namespace

TEST(WalWriter, AppendUpdatesSizeAndFile) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  minikv::WalWriter writer;
  CHECK_EQ(writer.size(), static_cast<std::uint64_t>(0));
  REQUIRE_OK(writer.Open(dir / "wal.log", false, 0));
  CHECK_EQ(writer.size(), static_cast<std::uint64_t>(0));

  const minikv::WalRecord first = MakePut("a", "1");
  const minikv::WalRecord second = MakePut("b", "22");
  REQUIRE_OK(writer.Append(first));
  REQUIRE_OK(writer.Append(second));

  const std::uint64_t expected = EncodedSize(first) + EncodedSize(second);
  CHECK_EQ(writer.size(), expected);

  std::uint64_t on_disk = 0;
  CHECK_OK(minikv::FileSize(dir / "wal.log", &on_disk));
  CHECK_EQ(on_disk, expected);
}

TEST(WalWriter, AppendThenReadBackAllRecords) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  {
    minikv::WalWriter writer;
    REQUIRE_OK(writer.Open(dir / "wal.log", false, 0));
    REQUIRE_OK(writer.Append(MakePut("user:1", "zhangsan")));
    REQUIRE_OK(writer.Append(MakePut("user:2", "lisi")));
    REQUIRE_OK(writer.Append(MakeDelete("user:1")));
    REQUIRE_OK(writer.Sync());
  }

  std::vector<minikv::WalRecord> records;
  REQUIRE_TRUE(ReadAll(dir / "wal.log", &records));
  REQUIRE_TRUE(records.size() == 3);

  CHECK_EQ(records[0].type, minikv::RecordType::kPut);
  CHECK_STREQ(records[0].key, "user:1");
  CHECK_STREQ(records[0].value, "zhangsan");

  CHECK_EQ(records[2].type, minikv::RecordType::kDelete);
  CHECK_STREQ(records[2].key, "user:1");
  CHECK_TRUE(records[2].value.empty());
}

TEST(WalWriter, ReopenContinuesAppending) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  std::uint64_t offset = 0;
  {
    minikv::WalWriter writer;
    REQUIRE_OK(writer.Open(dir / "wal.log", false, 0));
    REQUIRE_OK(writer.Append(MakePut("a", "1")));
    REQUIRE_OK(writer.Append(MakePut("b", "2")));
    offset = writer.size();
  }

  {
    minikv::WalWriter writer;
    REQUIRE_OK(writer.Open(dir / "wal.log", false, offset));
    CHECK_EQ(writer.size(), offset);
    REQUIRE_OK(writer.Append(MakePut("c", "3")));
    CHECK_TRUE(writer.size() > offset);
  }

  std::vector<minikv::WalRecord> records;
  REQUIRE_TRUE(ReadAll(dir / "wal.log", &records));
  REQUIRE_TRUE(records.size() == 3);
  CHECK_STREQ(records[2].key, "c");
}

TEST(WalWriter, StartOffsetBeyondEndFails) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  std::uint64_t size = 0;
  {
    minikv::WalWriter writer;
    REQUIRE_OK(writer.Open(dir / "wal.log", false, 0));
    REQUIRE_OK(writer.Append(MakePut("a", "1")));
    size = writer.size();
  }

  minikv::WalWriter writer;
  const minikv::Status status = writer.Open(dir / "wal.log", false, size + 10);
  CHECK_FALSE(status.ok());
  CHECK_EQ(status.code(), minikv::StatusCode::kInvalidArgument);
}

TEST(WalWriter, TruncatesTrailingGarbage) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  const minikv::WalRecord good = MakePut("keep", "me");
  std::uint64_t valid_size = 0;
  {
    minikv::WalWriter writer;
    REQUIRE_OK(writer.Open(dir / "wal.log", false, 0));
    REQUIRE_OK(writer.Append(good));
    valid_size = writer.size();
  }

  // 在有效记录后面追加 7 个垃圾字节，模拟“写了半条记录就崩溃”
  {
    minikv::File file;
    REQUIRE_OK(file.Open(dir / "wal.log", minikv::FileOpenMode::kReadWrite));
    REQUIRE_OK(file.Append("garbage"));
  }

  minikv::WalWriter writer;
  REQUIRE_OK(writer.Open(dir / "wal.log", false, valid_size));
  CHECK_EQ(writer.size(), valid_size);

  std::uint64_t on_disk = 0;
  CHECK_OK(minikv::FileSize(dir / "wal.log", &on_disk));
  CHECK_EQ(on_disk, valid_size);

  std::vector<minikv::WalRecord> records;
  REQUIRE_TRUE(ReadAll(dir / "wal.log", &records));
  REQUIRE_TRUE(records.size() == 1);
  CHECK_STREQ(records[0].key, "keep");

  // 截断后继续写仍然可用
  REQUIRE_OK(writer.Append(MakePut("after", "truncate")));
  records.clear();
  REQUIRE_TRUE(ReadAll(dir / "wal.log", &records));
  CHECK_EQ(records.size(), static_cast<std::size_t>(2));
}

TEST(WalWriter, AppendOnClosedWriterFails) {
  minikv::WalWriter writer;
  const minikv::Status status = writer.Append(MakePut("a", "1"));
  CHECK_FALSE(status.ok());
  CHECK_EQ(status.code(), minikv::StatusCode::kClosed);

  CHECK_EQ(writer.Sync().code(), minikv::StatusCode::kClosed);
}

TEST(WalWriter, CloseResetsSize) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  minikv::WalWriter writer;
  REQUIRE_OK(writer.Open(dir / "wal.log", false, 0));
  REQUIRE_OK(writer.Append(MakePut("a", "1")));
  CHECK_TRUE(writer.size() > 0);

  writer.Close();
  CHECK_EQ(writer.size(), static_cast<std::uint64_t>(0));
  CHECK_FALSE(writer.Append(MakePut("b", "2")).ok());
}

TEST(WalWriter, SyncOnWriteModeStillWritesCorrectBytes) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  {
    minikv::WalWriter writer;
    REQUIRE_OK(writer.Open(dir / "wal.log", true, 0));
    REQUIRE_OK(writer.Append(MakePut("k", "v")));
  }

  std::vector<minikv::WalRecord> records;
  REQUIRE_TRUE(ReadAll(dir / "wal.log", &records));
  REQUIRE_TRUE(records.size() == 1);
  CHECK_STREQ(records[0].value, "v");
}
