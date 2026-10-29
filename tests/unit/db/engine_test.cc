#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "minikv/api/engine.h"
#include "minikv/api/options.h"
#include "minikv/util/env.h"
#include "minikv/util/file.h"
#include "minikv/util/logging.h"
#include "minikv/wal/wal_format.h"

#include "support/temp_dir.h"
#include "support/test_framework.h"

namespace {

minikv::Options MakeOptions(const std::filesystem::path& db_path) {
  minikv::Options options;
  options.db_path = db_path;
  options.sync_on_write = false;
  return options;
}

// 截断是预期行为，屏蔽 WARN 以免污染输出
class SilentLogs {
 public:
  SilentLogs() { minikv::SetLogSink([](const std::string&) {}); }
  ~SilentLogs() { minikv::SetLogSink(minikv::LogSink{}); }

  SilentLogs(const SilentLogs&) = delete;
  SilentLogs& operator=(const SilentLogs&) = delete;
};

}  // namespace

TEST(Engine, OpenCreatesDirectoryAndWal) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  const auto db_path = dir / "db";
  std::unique_ptr<minikv::Engine> engine;
  REQUIRE_OK(minikv::Engine::Open(MakeOptions(db_path), &engine));
  REQUIRE_TRUE(engine != nullptr);

  CHECK_TRUE(std::filesystem::is_directory(db_path));
  CHECK_TRUE(std::filesystem::exists(db_path / "wal.log"));

  CHECK_OK(engine->Close());
}

TEST(Engine, PutGetRoundTrip) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  std::unique_ptr<minikv::Engine> engine;
  REQUIRE_OK(minikv::Engine::Open(MakeOptions(dir / "db"), &engine));

  REQUIRE_OK(engine->Put("user:1", "zhangsan"));
  REQUIRE_OK(engine->Put("user:2", "lisi"));

  std::string value;
  CHECK_OK(engine->Get("user:1", &value));
  CHECK_STREQ(value, "zhangsan");

  CHECK_OK(engine->Get("user:2", &value));
  CHECK_STREQ(value, "lisi");

  CHECK_TRUE(engine->Get("user:3", &value).IsNotFound());
  CHECK_OK(engine->Close());
}

TEST(Engine, OverwriteReturnsLatestValue) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  std::unique_ptr<minikv::Engine> engine;
  REQUIRE_OK(minikv::Engine::Open(MakeOptions(dir / "db"), &engine));

  REQUIRE_OK(engine->Put("k", "v1"));
  REQUIRE_OK(engine->Put("k", "v2"));

  std::string value;
  CHECK_OK(engine->Get("k", &value));
  CHECK_STREQ(value, "v2");
  CHECK_OK(engine->Close());
}

TEST(Engine, DeleteHidesValue) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  std::unique_ptr<minikv::Engine> engine;
  REQUIRE_OK(minikv::Engine::Open(MakeOptions(dir / "db"), &engine));

  REQUIRE_OK(engine->Put("k", "v"));
  REQUIRE_OK(engine->Delete("k"));

  std::string value;
  CHECK_TRUE(engine->Get("k", &value).IsNotFound());
  CHECK_OK(engine->Close());
}

TEST(Engine, ReopenRestoresDataAndDeletes) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  {
    std::unique_ptr<minikv::Engine> engine;
    REQUIRE_OK(minikv::Engine::Open(MakeOptions(db_path), &engine));
    REQUIRE_OK(engine->Put("a", "1"));
    REQUIRE_OK(engine->Put("b", "2"));
    REQUIRE_OK(engine->Delete("a"));
    REQUIRE_OK(engine->Close());
  }

  std::unique_ptr<minikv::Engine> engine;
  REQUIRE_OK(minikv::Engine::Open(MakeOptions(db_path), &engine));

  std::string value;
  CHECK_TRUE(engine->Get("a", &value).IsNotFound());  // 删除也被恢复了
  CHECK_OK(engine->Get("b", &value));
  CHECK_STREQ(value, "2");
  CHECK_OK(engine->Close());
}

TEST(Engine, RejectsEmptyKey) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  std::unique_ptr<minikv::Engine> engine;
  REQUIRE_OK(minikv::Engine::Open(MakeOptions(dir / "db"), &engine));

  const minikv::Status status = engine->Put("", "v");
  CHECK_FALSE(status.ok());
  CHECK_EQ(status.code(), minikv::StatusCode::kInvalidArgument);
  CHECK_OK(engine->Close());
}

TEST(Engine, RejectsOversizedKey) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  std::unique_ptr<minikv::Engine> engine;
  REQUIRE_OK(minikv::Engine::Open(MakeOptions(dir / "db"), &engine));

  const std::string huge_key(minikv::kMaxKeySize + 1, 'k');
  const minikv::Status status = engine->Put(huge_key, "v");
  CHECK_FALSE(status.ok());
  CHECK_EQ(status.code(), minikv::StatusCode::kInvalidArgument);
  CHECK_OK(engine->Close());
}

TEST(Engine, OperationsAfterCloseFail) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  std::unique_ptr<minikv::Engine> engine;
  REQUIRE_OK(minikv::Engine::Open(MakeOptions(dir / "db"), &engine));
  REQUIRE_OK(engine->Close());

  CHECK_EQ(engine->Put("k", "v").code(), minikv::StatusCode::kClosed);
  CHECK_EQ(engine->Delete("k").code(), minikv::StatusCode::kClosed);

  std::string value;
  CHECK_EQ(engine->Get("k", &value).code(), minikv::StatusCode::kClosed);
}

TEST(Engine, SyncOnWriteModeStillWorks) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  minikv::Options options = MakeOptions(dir / "db");
  options.sync_on_write = true;

  std::unique_ptr<minikv::Engine> engine;
  REQUIRE_OK(minikv::Engine::Open(options, &engine));
  REQUIRE_OK(engine->Put("k", "v"));
  REQUIRE_OK(engine->Close());

  REQUIRE_OK(minikv::Engine::Open(options, &engine));
  std::string value;
  CHECK_OK(engine->Get("k", &value));
  CHECK_STREQ(value, "v");
  CHECK_OK(engine->Close());
}

TEST(Engine, ReopenTruncatesGarbageTail) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  std::uint64_t valid_size = 0;
  {
    std::unique_ptr<minikv::Engine> engine;
    REQUIRE_OK(minikv::Engine::Open(MakeOptions(db_path), &engine));
    REQUIRE_OK(engine->Put("keep", "me"));
    REQUIRE_OK(engine->Close());

    CHECK_OK(minikv::FileSize(db_path / "wal.log", &valid_size));
  }

  {
    minikv::File file;
    REQUIRE_OK(file.Open(db_path / "wal.log", minikv::FileOpenMode::kReadWrite));
    REQUIRE_OK(file.Append("garbage-tail"));
  }

  {
    std::unique_ptr<minikv::Engine> engine;
    REQUIRE_OK(minikv::Engine::Open(MakeOptions(db_path), &engine));

    std::string value;
    CHECK_OK(engine->Get("keep", &value));
    CHECK_STREQ(value, "me");
    CHECK_OK(engine->Close());
  }

  std::uint64_t size_after = 0;
  CHECK_OK(minikv::FileSize(db_path / "wal.log", &size_after));
  CHECK_EQ(size_after, valid_size);
}

TEST(Engine, StatsReflectCurrentState) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  std::unique_ptr<minikv::Engine> engine;
  REQUIRE_OK(minikv::Engine::Open(MakeOptions(dir / "db"), &engine));

  const minikv::EngineStats empty_stats = engine->GetStats();
  CHECK_EQ(empty_stats.wal_bytes, static_cast<std::uint64_t>(0));
  CHECK_EQ(empty_stats.memtable_entries, static_cast<std::size_t>(0));

  REQUIRE_OK(engine->Put("a", "1"));
  REQUIRE_OK(engine->Put("b", "22"));

  const minikv::EngineStats stats = engine->GetStats();
  CHECK_EQ(stats.memtable_entries, static_cast<std::size_t>(2));
  CHECK_TRUE(stats.wal_bytes > 0);
  CHECK_TRUE(stats.memtable_bytes > 0);

  CHECK_OK(engine->Close());
}

TEST(Engine, ConcurrentPutsAreAllPersisted) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  std::unique_ptr<minikv::Engine> engine;
  REQUIRE_OK(minikv::Engine::Open(MakeOptions(db_path), &engine));

  constexpr int kPerThread = 100;
  std::thread first([&engine]() {
    for (int i = 0; i < kPerThread; ++i) {
      engine->Put("a" + std::to_string(i), "1");
    }
  });
  std::thread second([&engine]() {
    for (int i = 0; i < kPerThread; ++i) {
      engine->Put("b" + std::to_string(i), "2");
    }
  });
  first.join();
  second.join();

  REQUIRE_OK(engine->Close());

  REQUIRE_OK(minikv::Engine::Open(MakeOptions(db_path), &engine));
  std::string value;
  CHECK_OK(engine->Get("a0", &value));
  CHECK_OK(engine->Get("b" + std::to_string(kPerThread - 1), &value));
  CHECK_STREQ(value, "2");
  CHECK_OK(engine->Close());
}
