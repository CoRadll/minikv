#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "minikv/api/engine.h"
#include "minikv/api/options.h"
#include "minikv/util/env.h"
#include "minikv/util/logging.h"

#include "support/temp_dir.h"
#include "support/test_framework.h"

namespace {

minikv::Options MakeOptions(const std::filesystem::path& db_path) {
  minikv::Options options;
  options.db_path = db_path;
  options.sync_on_write = false;
  options.level0_compaction_trigger = 2;  // 两次刷盘就触发，便于测试
  return options;
}

std::unique_ptr<minikv::Engine> OpenEngine(const std::filesystem::path& db_path) {
  std::unique_ptr<minikv::Engine> engine;
  const minikv::Status status = minikv::Engine::Open(MakeOptions(db_path), &engine);
  return status.ok() ? std::move(engine) : nullptr;
}

int CountSstables(const std::filesystem::path& dir) {
  int count = 0;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (entry.is_regular_file() && entry.path().extension() == ".sst") {
      ++count;
    }
  }
  return count;
}

int CountManifestLines(const std::filesystem::path& db_path) {
  std::ifstream input((db_path / "MANIFEST").string());
  int lines = 0;
  std::string line;
  while (std::getline(input, line)) {
    ++lines;
  }
  return lines;
}

class SilentLogs {
 public:
  SilentLogs() { minikv::SetLogSink([](const std::string&) {}); }
  ~SilentLogs() { minikv::SetLogSink(minikv::LogSink{}); }

  SilentLogs(const SilentLogs&) = delete;
  SilentLogs& operator=(const SilentLogs&) = delete;
};

}  // namespace

TEST(Compaction, MergesWhenLevel0ReachesThreshold) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);

  REQUIRE_OK(engine->Put("a", "1"));
  REQUIRE_OK(engine->Flush());
  CHECK_EQ(engine->GetStats().sstable_count, static_cast<std::size_t>(1));

  REQUIRE_OK(engine->Put("b", "2"));
  REQUIRE_OK(engine->Flush());  // L0=2 → 触发合并

  CHECK_EQ(engine->GetStats().sstable_count, static_cast<std::size_t>(1));
  CHECK_EQ(CountSstables(db_path), 1);

  std::string value;
  CHECK_OK(engine->Get("a", &value));
  CHECK_STREQ(value, "1");
  CHECK_OK(engine->Get("b", &value));
  CHECK_STREQ(value, "2");
  REQUIRE_OK(engine->Close());
}

TEST(Compaction, KeepsNewestVersionByKey) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);

  REQUIRE_OK(engine->Put("k", "v1"));
  REQUIRE_OK(engine->Flush());
  REQUIRE_OK(engine->Put("k", "v2"));
  REQUIRE_OK(engine->Flush());  // 合并：v2
  REQUIRE_OK(engine->Put("k", "v3"));
  REQUIRE_OK(engine->Flush());
  REQUIRE_OK(engine->Put("pad", "x"));
  REQUIRE_OK(engine->Flush());  // 再次合并：v3

  std::string value;
  CHECK_OK(engine->Get("k", &value));
  CHECK_STREQ(value, "v3");
  REQUIRE_OK(engine->Close());
}

TEST(Compaction, DropsTombstones) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);

  REQUIRE_OK(engine->Put("alive", "1"));
  REQUIRE_OK(engine->Put("dead", "2"));
  REQUIRE_OK(engine->Flush());

  REQUIRE_OK(engine->Delete("dead"));
  REQUIRE_OK(engine->Flush());  // 合并：墓碑与旧值一起丢弃

  std::string value;
  CHECK_TRUE(engine->Get("dead", &value).IsNotFound());
  CHECK_OK(engine->Get("alive", &value));
  CHECK_STREQ(value, "1");

  // 墓碑不应该留下痕迹：文件数仍是 1，MANIFEST 只有一行文件记录
  CHECK_EQ(engine->GetStats().sstable_count, static_cast<std::size_t>(1));
  CHECK_EQ(CountManifestLines(db_path), 2);  // 头部 + 一行
  REQUIRE_OK(engine->Close());
}

TEST(Compaction, AllDeletedProducesNoFile) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);

  REQUIRE_OK(engine->Put("only", "value"));
  REQUIRE_OK(engine->Flush());
  REQUIRE_OK(engine->Delete("only"));
  REQUIRE_OK(engine->Flush());

  CHECK_EQ(engine->GetStats().sstable_count, static_cast<std::size_t>(0));
  CHECK_EQ(CountSstables(db_path), 0);

  std::string value;
  CHECK_TRUE(engine->Get("only", &value).IsNotFound());
  REQUIRE_OK(engine->Close());
}

TEST(Compaction, DataSurvivesRestartAfterCompaction) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  {
    auto engine = OpenEngine(db_path);
    REQUIRE_TRUE(engine != nullptr);
    for (int i = 0; i < 12; ++i) {
      REQUIRE_OK(engine->Put("key" + std::to_string(i), "value" + std::to_string(i)));
      REQUIRE_OK(engine->Flush());
    }
    REQUIRE_OK(engine->Close());
  }

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);

  int mismatches = 0;
  for (int i = 0; i < 12; ++i) {
    std::string value;
    if (!engine->Get("key" + std::to_string(i), &value).ok() ||
        value != "value" + std::to_string(i)) {
      ++mismatches;
    }
  }
  CHECK_EQ(mismatches, 0);
  REQUIRE_OK(engine->Close());
}

TEST(Compaction, ManifestMatchesFileCount) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);
  for (int i = 0; i < 6; ++i) {
    REQUIRE_OK(engine->Put("k" + std::to_string(i), "v"));
    REQUIRE_OK(engine->Flush());
  }

  const std::size_t sstables = engine->GetStats().sstable_count;
  CHECK_EQ(static_cast<int>(sstables), CountSstables(db_path));
  // 头部 1 行 + 每个文件 1 行
  CHECK_EQ(CountManifestLines(db_path), 1 + static_cast<int>(sstables));
  REQUIRE_OK(engine->Close());
}
