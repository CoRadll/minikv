// 崩溃边界：模拟"进程在刷盘/登记/截断 WAL 的任意一步崩溃后，磁盘上会留下什么"，
// 验证重启时能正确处理这些残留状态。

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "minikv/api/engine.h"
#include "minikv/api/options.h"
#include "minikv/table/table_builder.h"
#include "minikv/util/env.h"
#include "minikv/util/file.h"
#include "minikv/util/logging.h"
#include "minikv/wal/wal_format.h"
#include "minikv/wal/wal_writer.h"

#include "support/temp_dir.h"
#include "support/test_framework.h"

namespace {

minikv::Options MakeOptions(const std::filesystem::path& db_path) {
  minikv::Options options;
  options.db_path = db_path;
  options.sync_on_write = false;
  return options;
}

std::unique_ptr<minikv::Engine> OpenEngine(const std::filesystem::path& db_path) {
  std::unique_ptr<minikv::Engine> engine;
  const minikv::Status status = minikv::Engine::Open(MakeOptions(db_path), &engine);
  return status.ok() ? std::move(engine) : nullptr;
}

class SilentLogs {
 public:
  SilentLogs() { minikv::SetLogSink([](const std::string&) {}); }
  ~SilentLogs() { minikv::SetLogSink(minikv::LogSink{}); }

  SilentLogs(const SilentLogs&) = delete;
  SilentLogs& operator=(const SilentLogs&) = delete;
};

void WriteRawFile(const std::filesystem::path& path, const std::string& content) {
  minikv::File file;
  REQUIRE_OK(file.Open(path, minikv::FileOpenMode::kReadWrite));
  REQUIRE_OK(file.Truncate(0));
  REQUIRE_OK(file.Append(content));
  REQUIRE_OK(file.Sync());
}

}  // namespace

TEST(CrashBoundary, OrphanSstableIsIgnoredAndRemoved) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";
  REQUIRE_OK(minikv::CreateDirIfMissing(db_path));

  // 模拟"写完 sst 但还没登记 MANIFEST 就崩了"
  {
    minikv::TableBuilder builder(db_path / "000042.sst");
    REQUIRE_OK(builder.Add("orphan", "value"));
    REQUIRE_OK(builder.Finish());
  }

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);

  std::string value;
  CHECK_TRUE(engine->Get("orphan", &value).IsNotFound());
  CHECK_EQ(engine->GetStats().sstable_count, static_cast<std::size_t>(0));
  CHECK_FALSE(minikv::FileExists(db_path / "000042.sst"));

  // 正常写入仍然可用，且新文件用 000001 编号
  REQUIRE_OK(engine->Put("a", "1"));
  REQUIRE_OK(engine->Flush());
  CHECK_TRUE(minikv::FileExists(db_path / "000001.sst"));
  REQUIRE_OK(engine->Close());
}

TEST(CrashBoundary, StaleManifestTmpIsIgnored) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";
  REQUIRE_OK(minikv::CreateDirIfMissing(db_path));

  // 模拟"MANIFEST 写了一半就崩了"
  WriteRawFile((db_path / "MANIFEST").string() + ".tmp", "half-written-header");

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);
  CHECK_FALSE(minikv::FileExists((db_path / "MANIFEST").string() + ".tmp"));

  REQUIRE_OK(engine->Put("k", "v"));
  REQUIRE_OK(engine->Close());
}

TEST(CrashBoundary, CorruptedManifestFailsLoudly) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";
  REQUIRE_OK(minikv::CreateDirIfMissing(db_path));

  WriteRawFile(db_path / "MANIFEST", "NOT-A-MANIFEST\n");

  std::unique_ptr<minikv::Engine> engine;
  const minikv::Status status = minikv::Engine::Open(MakeOptions(db_path), &engine);
  CHECK_FALSE(status.ok());
  CHECK_EQ(status.code(), minikv::StatusCode::kCorruption);
}

TEST(CrashBoundary, ReplayedDuplicateRecordsAreIdempotent) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  // 1) 正常写入并刷盘（WAL 会被清空）
  {
    auto engine = OpenEngine(db_path);
    REQUIRE_TRUE(engine != nullptr);
    REQUIRE_OK(engine->Put("k", "v1"));
    REQUIRE_OK(engine->Delete("gone"));
    REQUIRE_OK(engine->Flush());
    REQUIRE_OK(engine->Close());
  }

  // 2) 模拟"MANIFEST 已保存，但 WAL 还没截断就崩了"：把同样的记录再追加一次
  {
    minikv::WalWriter writer;
    REQUIRE_OK(writer.Open(db_path / "wal.log", false, 0));

    minikv::WalRecord put;
    put.type = minikv::RecordType::kPut;
    put.key = "k";
    put.value = "v1";
    REQUIRE_OK(writer.Append(put));

    minikv::WalRecord del;
    del.type = minikv::RecordType::kDelete;
    del.key = "gone";
    REQUIRE_OK(writer.Append(del));
    REQUIRE_OK(writer.Sync());
  }

  // 3) 重放这些重复记录不应改变最终状态
  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);

  std::string value;
  CHECK_OK(engine->Get("k", &value));
  CHECK_STREQ(value, "v1");
  CHECK_TRUE(engine->Get("gone", &value).IsNotFound());
  REQUIRE_OK(engine->Close());
}

TEST(CrashBoundary, HigherNumberedOrphanDoesNotBlockNextFlush) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";
  REQUIRE_OK(minikv::CreateDirIfMissing(db_path));

  {
    minikv::TableBuilder builder(db_path / "000999.sst");
    REQUIRE_OK(builder.Add("z", "z"));
    REQUIRE_OK(builder.Finish());
  }

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);

  REQUIRE_OK(engine->Put("k", "v"));
  REQUIRE_OK(engine->Flush());
  CHECK_TRUE(minikv::FileExists(db_path / "000001.sst"));
  CHECK_FALSE(minikv::FileExists(db_path / "000999.sst"));
  CHECK_EQ(engine->GetStats().sstable_count, static_cast<std::size_t>(1));

  std::string value;
  CHECK_OK(engine->Get("k", &value));
  CHECK_STREQ(value, "v");
  REQUIRE_OK(engine->Close());
}

TEST(CrashBoundary, EmptyDatabaseHasNoFilesAfterOpen) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);
  CHECK_EQ(engine->GetStats().sstable_count, static_cast<std::size_t>(0));
  CHECK_EQ(engine->GetStats().wal_bytes, static_cast<std::uint64_t>(0));
  CHECK_FALSE(minikv::FileExists(db_path / "MANIFEST"));
  REQUIRE_OK(engine->Close());
}
