#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "minikv/api/engine.h"
#include "minikv/api/options.h"
#include "minikv/util/env.h"
#include "minikv/util/logging.h"

#include "support/temp_dir.h"
#include "support/test_framework.h"

namespace {

minikv::Options MakeOptions(const std::filesystem::path& db_path, std::size_t memtable_size = 0) {
  minikv::Options options;
  options.db_path = db_path;
  options.sync_on_write = false;
  if (memtable_size > 0) {
    options.memtable_size = memtable_size;
  }
  return options;
}

std::unique_ptr<minikv::Engine> OpenEngine(const minikv::Options& options) {
  std::unique_ptr<minikv::Engine> engine;
  const minikv::Status status = minikv::Engine::Open(options, &engine);
  return status.ok() ? std::move(engine) : nullptr;
}

int CountFilesWithSuffix(const std::filesystem::path& dir, const std::string& suffix) {
  int count = 0;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (entry.is_regular_file() && entry.path().extension() == suffix) {
      ++count;
    }
  }
  return count;
}

class SilentLogs {
 public:
  SilentLogs() { minikv::SetLogSink([](const std::string&) {}); }
  ~SilentLogs() { minikv::SetLogSink(minikv::LogSink{}); }

  SilentLogs(const SilentLogs&) = delete;
  SilentLogs& operator=(const SilentLogs&) = delete;
};

}  // namespace

TEST(Flush, CreatesSstableAndClearsWal) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  auto engine = OpenEngine(MakeOptions(db_path));
  REQUIRE_TRUE(engine != nullptr);
  REQUIRE_OK(engine->Put("a", "1"));
  REQUIRE_OK(engine->Put("b", "2"));

  const minikv::EngineStats before = engine->GetStats();
  CHECK_EQ(before.sstable_count, static_cast<std::size_t>(0));
  CHECK_TRUE(before.wal_bytes > 0);

  REQUIRE_OK(engine->Flush());

  const minikv::EngineStats after = engine->GetStats();
  CHECK_EQ(after.sstable_count, static_cast<std::size_t>(1));
  CHECK_EQ(after.wal_bytes, static_cast<std::uint64_t>(0));
  CHECK_EQ(after.memtable_entries, static_cast<std::size_t>(0));

  CHECK_EQ(CountFilesWithSuffix(db_path, ".sst"), 1);
  CHECK_TRUE(minikv::FileExists(db_path / "MANIFEST"));
  REQUIRE_OK(engine->Close());
}

TEST(Flush, DataRemainsReadableAfterFlush) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  auto engine = OpenEngine(MakeOptions(dir / "db"));
  REQUIRE_TRUE(engine != nullptr);
  REQUIRE_OK(engine->Put("k1", "v1"));
  REQUIRE_OK(engine->Put("k2", "v2"));
  REQUIRE_OK(engine->Put("k1", "v1-new"));  // 覆盖写
  REQUIRE_OK(engine->Flush());

  std::string value;
  CHECK_OK(engine->Get("k1", &value));
  CHECK_STREQ(value, "v1-new");
  CHECK_OK(engine->Get("k2", &value));
  CHECK_STREQ(value, "v2");
  CHECK_TRUE(engine->Get("missing", &value).IsNotFound());
  REQUIRE_OK(engine->Close());
}

TEST(Flush, RestartLoadsFromSstable) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  {
    auto engine = OpenEngine(MakeOptions(db_path));
    REQUIRE_TRUE(engine != nullptr);
    for (int i = 0; i < 50; ++i) {
      REQUIRE_OK(engine->Put("key" + std::to_string(i), "value" + std::to_string(i)));
    }
    REQUIRE_OK(engine->Flush());
    REQUIRE_OK(engine->Close());
  }

  auto engine = OpenEngine(MakeOptions(db_path));
  REQUIRE_TRUE(engine != nullptr);

  const minikv::EngineStats stats = engine->GetStats();
  CHECK_TRUE(stats.sstable_count >= 1);
  CHECK_EQ(stats.wal_bytes, static_cast<std::uint64_t>(0));

  int mismatches = 0;
  for (int i = 0; i < 50; ++i) {
    std::string value;
    if (!engine->Get("key" + std::to_string(i), &value).ok() ||
        value != "value" + std::to_string(i)) {
      ++mismatches;
    }
  }
  CHECK_EQ(mismatches, 0);
  REQUIRE_OK(engine->Close());
}

TEST(Flush, TombstoneInSstablePreventsResurrection) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  {
    auto engine = OpenEngine(MakeOptions(db_path));
    REQUIRE_TRUE(engine != nullptr);
    REQUIRE_OK(engine->Put("ghost", "alive"));
    REQUIRE_OK(engine->Flush());  // sst1: ghost = alive

    REQUIRE_OK(engine->Delete("ghost"));
    REQUIRE_OK(engine->Flush());  // sst2: ghost = 墓碑
    REQUIRE_OK(engine->Close());
  }

  auto engine = OpenEngine(MakeOptions(db_path));
  REQUIRE_TRUE(engine != nullptr);

  // 如果墓碑被当成"不存在"，就会穿透到 sst1 读到 alive
  std::string value;
  CHECK_TRUE(engine->Get("ghost", &value).IsNotFound());
  REQUIRE_OK(engine->Close());
}

TEST(Flush, AutoFlushWhenMemtableExceedsThreshold) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  auto engine = OpenEngine(MakeOptions(dir / "db", 1024));
  REQUIRE_TRUE(engine != nullptr);

  constexpr int kCount = 100;
  for (int i = 0; i < kCount; ++i) {
    REQUIRE_OK(engine->Put("key" + std::to_string(i), std::string(20, 'v')));
  }

  const minikv::EngineStats stats = engine->GetStats();
  CHECK_TRUE(stats.sstable_count > 1);
  CHECK_TRUE(stats.memtable_bytes < 1024);

  int mismatches = 0;
  for (int i = 0; i < kCount; ++i) {
    std::string value;
    if (!engine->Get("key" + std::to_string(i), &value).ok() || value != std::string(20, 'v')) {
      ++mismatches;
    }
  }
  CHECK_EQ(mismatches, 0);
  REQUIRE_OK(engine->Close());
}

TEST(Flush, MultipleFlushesCreateMultipleFiles) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  auto engine = OpenEngine(MakeOptions(dir / "db"));
  REQUIRE_TRUE(engine != nullptr);

  for (int round = 0; round < 3; ++round) {
    REQUIRE_OK(engine->Put("k" + std::to_string(round), "v"));
    REQUIRE_OK(engine->Flush());
  }

  CHECK_EQ(engine->GetStats().sstable_count, static_cast<std::size_t>(3));
  REQUIRE_OK(engine->Close());

  auto reopened = OpenEngine(MakeOptions(dir / "db"));
  REQUIRE_TRUE(reopened != nullptr);
  CHECK_EQ(reopened->GetStats().sstable_count, static_cast<std::size_t>(3));

  std::string value;
  CHECK_OK(reopened->Get("k0", &value));
  CHECK_OK(reopened->Get("k2", &value));
  REQUIRE_OK(reopened->Close());
}

TEST(Flush, FlushOnEmptyMemtableIsNoop) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  auto engine = OpenEngine(MakeOptions(dir / "db"));
  REQUIRE_TRUE(engine != nullptr);

  REQUIRE_OK(engine->Flush());
  CHECK_EQ(engine->GetStats().sstable_count, static_cast<std::size_t>(0));
  REQUIRE_OK(engine->Close());
}
