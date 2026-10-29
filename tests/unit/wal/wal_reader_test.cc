#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "minikv/util/env.h"
#include "minikv/util/file.h"
#include "minikv/util/logging.h"
#include "minikv/wal/wal_format.h"
#include "minikv/wal/wal_reader.h"
#include "minikv/wal/wal_writer.h"

#include "support/temp_dir.h"
#include "support/test_framework.h"

namespace {

// 截断是这些用例的预期行为，屏蔽 WARN 以免污染测试输出
class SilentLogs {
 public:
  SilentLogs() { minikv::SetLogSink([](const std::string&) {}); }
  ~SilentLogs() { minikv::SetLogSink(minikv::LogSink{}); }

  SilentLogs(const SilentLogs&) = delete;
  SilentLogs& operator=(const SilentLogs&) = delete;
};

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

std::string Encode(const minikv::WalRecord& record) {
  std::string buffer;
  minikv::EncodeRecord(record, &buffer);
  return buffer;
}

void WriteRecords(const std::filesystem::path& path,
                  const std::vector<minikv::WalRecord>& records) {
  minikv::WalWriter writer;
  REQUIRE_OK(writer.Open(path, false, 0));
  for (const auto& record : records) {
    REQUIRE_OK(writer.Append(record));
  }
  REQUIRE_OK(writer.Sync());
}

void WriteRaw(const std::filesystem::path& path, const std::string& data) {
  minikv::File file;
  REQUIRE_OK(file.Open(path, minikv::FileOpenMode::kReadWrite));
  REQUIRE_OK(file.Append(data));
}

std::vector<minikv::WalRecord> Collect(const std::filesystem::path& path,
                                       std::uint64_t start_offset = 0) {
  std::vector<minikv::WalRecord> records;
  minikv::WalReader reader(path);
  const minikv::Status status = reader.Replay(
      [&records](const minikv::WalRecord& record) { records.push_back(record); }, start_offset);
  if (!status.ok()) {
    records.clear();
  }
  return records;
}

}  // namespace

TEST(WalReader, ReplaysAllRecordsInOrder) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = dir / "wal.log";

  WriteRecords(path, {MakePut("a", "1"), MakePut("b", "2"), MakeDelete("a")});

  std::vector<minikv::WalRecord> records;
  minikv::WalReader reader(path);
  REQUIRE_OK(reader.Replay(
      [&records](const minikv::WalRecord& record) { records.push_back(record); }));

  REQUIRE_TRUE(records.size() == 3);
  CHECK_STREQ(records[0].key, "a");
  CHECK_STREQ(records[1].value, "2");
  CHECK_EQ(records[2].type, minikv::RecordType::kDelete);

  std::uint64_t file_size = 0;
  CHECK_OK(minikv::FileSize(path, &file_size));
  CHECK_EQ(reader.valid_bytes(), file_size);
  CHECK_EQ(reader.dropped_bytes(), static_cast<std::uint64_t>(0));
}

TEST(WalReader, MissingFileIsEmptyLog) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = dir / "no_such_wal.log";

  minikv::WalReader reader(path);
  int calls = 0;
  CHECK_OK(reader.Replay([&calls](const minikv::WalRecord&) { ++calls; }));
  CHECK_EQ(calls, 0);
  CHECK_EQ(reader.valid_bytes(), static_cast<std::uint64_t>(0));
  CHECK_EQ(reader.dropped_bytes(), static_cast<std::uint64_t>(0));
}

TEST(WalReader, EmptyFileReplaysNothing) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = dir / "wal.log";

  WriteRaw(path, "");

  std::vector<minikv::WalRecord> records = Collect(path);
  CHECK_TRUE(records.empty());

  std::uint64_t file_size = 1;
  CHECK_OK(minikv::FileSize(path, &file_size));
  CHECK_EQ(file_size, static_cast<std::uint64_t>(0));
}

TEST(WalReader, TruncatesGarbageTail) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = dir / "wal.log";

  WriteRecords(path, {MakePut("a", "1"), MakePut("b", "2")});

  std::uint64_t valid_size = 0;
  CHECK_OK(minikv::FileSize(path, &valid_size));

  WriteRaw(path, "garbage");

  std::vector<minikv::WalRecord> records;
  minikv::WalReader reader(path);
  REQUIRE_OK(reader.Replay(
      [&records](const minikv::WalRecord& record) { records.push_back(record); }));

  CHECK_EQ(records.size(), static_cast<std::size_t>(2));
  CHECK_EQ(reader.valid_bytes(), valid_size);
  CHECK_EQ(reader.dropped_bytes(), static_cast<std::uint64_t>(7));

  std::uint64_t size_after = 0;
  CHECK_OK(minikv::FileSize(path, &size_after));
  CHECK_EQ(size_after, valid_size);
}

TEST(WalReader, TruncatesCorruptedLastRecord) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = dir / "wal.log";

  const std::string first = Encode(MakePut("keep", "me"));
  std::string second = Encode(MakePut("broken", "record"));
  second.back() = static_cast<char>(second.back() ^ 0xFF);
  WriteRaw(path, first + second);

  std::vector<minikv::WalRecord> records;
  minikv::WalReader reader(path);
  REQUIRE_OK(reader.Replay(
      [&records](const minikv::WalRecord& record) { records.push_back(record); }));

  REQUIRE_TRUE(records.size() == 1);
  CHECK_STREQ(records[0].key, "keep");
  CHECK_EQ(reader.valid_bytes(), static_cast<std::uint64_t>(first.size()));
  CHECK_EQ(reader.dropped_bytes(), static_cast<std::uint64_t>(second.size()));
}

TEST(WalReader, StopsAtFirstCorruptedRecord) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = dir / "wal.log";

  const std::string first = Encode(MakePut("a", "1"));
  std::string second = Encode(MakePut("b", "2"));
  second[5] = static_cast<char>(second[5] ^ 0x10);  // 破坏第二条第 1 个 key 字节
  const std::string third = Encode(MakePut("c", "3"));
  WriteRaw(path, first + second + third);

  std::vector<minikv::WalRecord> records;
  minikv::WalReader reader(path);
  REQUIRE_OK(reader.Replay(
      [&records](const minikv::WalRecord& record) { records.push_back(record); }));

  // 策略：在第一条坏记录处停止，后面的数据一并丢弃
  REQUIRE_TRUE(records.size() == 1);
  CHECK_STREQ(records[0].key, "a");
  CHECK_EQ(reader.valid_bytes(), static_cast<std::uint64_t>(first.size()));
  CHECK_TRUE(reader.dropped_bytes() > 0);
}

TEST(WalReader, ReplayFromOffsetSkipsPrefix) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = dir / "wal.log";

  const std::string first = Encode(MakePut("a", "1"));
  WriteRecords(path, {MakePut("a", "1"), MakePut("b", "2"), MakePut("c", "3")});

  std::vector<minikv::WalRecord> records;
  minikv::WalReader reader(path);
  REQUIRE_OK(reader.Replay(
      [&records](const minikv::WalRecord& record) { records.push_back(record); },
      static_cast<std::uint64_t>(first.size())));

  REQUIRE_TRUE(records.size() == 2);
  CHECK_STREQ(records[0].key, "b");
  CHECK_STREQ(records[1].key, "c");

  std::uint64_t file_size = 0;
  CHECK_OK(minikv::FileSize(path, &file_size));
  CHECK_EQ(reader.valid_bytes(), file_size);
  CHECK_EQ(reader.dropped_bytes(), static_cast<std::uint64_t>(0));
}

TEST(WalReader, StartOffsetBeyondFileFails) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = dir / "wal.log";

  WriteRecords(path, {MakePut("a", "1")});

  minikv::WalReader reader(path);
  const minikv::Status status = reader.Replay([](const minikv::WalRecord&) {}, 1000);
  CHECK_FALSE(status.ok());
  CHECK_EQ(status.code(), minikv::StatusCode::kInvalidArgument);
}

TEST(WalReader, SecondReplaySeesCleanLog) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = dir / "wal.log";

  WriteRecords(path, {MakePut("a", "1")});
  WriteRaw(path, "junk");

  {
    minikv::WalReader reader(path);
    REQUIRE_OK(reader.Replay([](const minikv::WalRecord&) {}));
    CHECK_EQ(reader.dropped_bytes(), static_cast<std::uint64_t>(4));
  }

  // 第一次回放已经截断并落盘，第二次不应再丢任何字节
  minikv::WalReader reader(path);
  int calls = 0;
  REQUIRE_OK(reader.Replay([&calls](const minikv::WalRecord&) { ++calls; }));
  CHECK_EQ(calls, 1);
  CHECK_EQ(reader.dropped_bytes(), static_cast<std::uint64_t>(0));
}

TEST(WalReader, WriterCanContinueAfterTruncation) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto path = dir / "wal.log";

  WriteRecords(path, {MakePut("a", "1"), MakePut("b", "2")});
  WriteRaw(path, "tail-garbage");

  minikv::WalReader reader(path);
  REQUIRE_OK(reader.Replay([](const minikv::WalRecord&) {}));

  minikv::WalWriter writer;
  REQUIRE_OK(writer.Open(path, false, reader.valid_bytes()));
  REQUIRE_OK(writer.Append(MakePut("c", "3")));
  writer.Close();

  std::vector<minikv::WalRecord> records;
  minikv::WalReader second(path);
  REQUIRE_OK(second.Replay(
      [&records](const minikv::WalRecord& record) { records.push_back(record); }));
  REQUIRE_TRUE(records.size() == 3);
  CHECK_STREQ(records[2].key, "c");
  CHECK_EQ(second.dropped_bytes(), static_cast<std::uint64_t>(0));
}
