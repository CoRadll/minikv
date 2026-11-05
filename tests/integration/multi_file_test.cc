// 多文件恢复：验证 SSTable 之间的新旧顺序、缺失文件、SSTable + WAL 混合恢复。

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "minikv/api/engine.h"
#include "minikv/api/options.h"
#include "minikv/util/env.h"
#include "minikv/util/file.h"
#include "minikv/util/logging.h"

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

}  // namespace

TEST(MultiFile, NewerFileWinsOverOlder) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  {
    auto engine = OpenEngine(db_path);
    REQUIRE_TRUE(engine != nullptr);

    REQUIRE_OK(engine->Put("k", "v1"));
    REQUIRE_OK(engine->Flush());  // sst1: v1

    REQUIRE_OK(engine->Put("k", "v2"));
    REQUIRE_OK(engine->Flush());  // sst2: v2

    REQUIRE_OK(engine->Close());
  }

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);
  CHECK_EQ(engine->GetStats().sstable_count, static_cast<std::size_t>(2));

  std::string value;
  CHECK_OK(engine->Get("k", &value));
  CHECK_STREQ(value, "v2");
  REQUIRE_OK(engine->Close());
}

TEST(MultiFile, DeleteInNewestFileHidesAllOlderVersions) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  {
    auto engine = OpenEngine(db_path);
    REQUIRE_TRUE(engine != nullptr);
    REQUIRE_OK(engine->Put("k", "v1"));
    REQUIRE_OK(engine->Flush());
    REQUIRE_OK(engine->Put("k", "v2"));
    REQUIRE_OK(engine->Flush());
    REQUIRE_OK(engine->Delete("k"));
    REQUIRE_OK(engine->Flush());
    REQUIRE_OK(engine->Close());
  }

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);
  CHECK_EQ(engine->GetStats().sstable_count, static_cast<std::size_t>(3));

  std::string value;
  CHECK_TRUE(engine->Get("k", &value).IsNotFound());
  REQUIRE_OK(engine->Close());
}

TEST(MultiFile, MissingSstableFailsOpen) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  {
    auto engine = OpenEngine(db_path);
    REQUIRE_TRUE(engine != nullptr);
    REQUIRE_OK(engine->Put("a", "1"));
    REQUIRE_OK(engine->Flush());
    REQUIRE_OK(engine->Put("b", "2"));
    REQUIRE_OK(engine->Flush());
    REQUIRE_OK(engine->Close());
  }

  // 删掉被 MANIFEST 引用的文件：必须报错，而不是静默丢数据
  REQUIRE_OK(minikv::RemoveFile(db_path / "000001.sst"));

  std::unique_ptr<minikv::Engine> engine;
  const minikv::Status status = minikv::Engine::Open(MakeOptions(db_path), &engine);
  CHECK_FALSE(status.ok());
  CHECK_EQ(status.code(), minikv::StatusCode::kCorruption);
}

TEST(MultiFile, ManyFilesSurviveRestart) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  constexpr int kRounds = 12;
  {
    auto engine = OpenEngine(db_path);
    REQUIRE_TRUE(engine != nullptr);
    for (int i = 0; i < kRounds; ++i) {
      REQUIRE_OK(engine->Put("key" + std::to_string(i), "value" + std::to_string(i)));
      REQUIRE_OK(engine->Flush());
    }
    REQUIRE_OK(engine->Close());
  }

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);
  CHECK_EQ(engine->GetStats().sstable_count, static_cast<std::size_t>(kRounds));

  int mismatches = 0;
  for (int i = 0; i < kRounds; ++i) {
    std::string value;
    if (!engine->Get("key" + std::to_string(i), &value).ok() ||
        value != "value" + std::to_string(i)) {
      ++mismatches;
    }
  }
  CHECK_EQ(mismatches, 0);
  REQUIRE_OK(engine->Close());
}

TEST(MultiFile, RecoversFromSstablesPlusWal) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  {
    auto engine = OpenEngine(db_path);
    REQUIRE_TRUE(engine != nullptr);

    REQUIRE_OK(engine->Put("from_sst", "1"));
    REQUIRE_OK(engine->Flush());  // 数据进入 sst1

    REQUIRE_OK(engine->Put("from_wal", "2"));
    REQUIRE_OK(engine->Put("from_sst", "updated"));  // 覆盖旧文件里的值

    // 模拟崩溃：直接丢弃引擎，不调用 Close（Close 会刷盘）
    engine.release();
  }

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);

  // 1 个 sst + WAL 里的两条记录
  CHECK_EQ(engine->GetStats().sstable_count, static_cast<std::size_t>(1));

  std::string value;
  CHECK_OK(engine->Get("from_sst", &value));
  CHECK_STREQ(value, "updated");  // 内存表(WAL 重放)优先于 SSTable
  CHECK_OK(engine->Get("from_wal", &value));
  CHECK_STREQ(value, "2");
  REQUIRE_OK(engine->Close());
}

TEST(MultiFile, NonNumericSstableNameIsIgnored) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";
  REQUIRE_OK(minikv::CreateDirIfMissing(db_path));

  // 非 "6 位数字.sst" 的文件不属于引擎，不能在清理时被误删
  const auto user_file = db_path / "notes.sst";
  {
    minikv::File file;
    REQUIRE_OK(file.Open(user_file, minikv::FileOpenMode::kReadWrite));
    REQUIRE_OK(file.Append("user data"));
  }

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);
  CHECK_TRUE(minikv::FileExists(user_file));
  REQUIRE_OK(engine->Close());
  CHECK_TRUE(minikv::FileExists(user_file));
}
