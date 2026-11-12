#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "minikv/api/engine.h"
#include "minikv/api/iterator.h"
#include "minikv/api/options.h"
#include "minikv/util/logging.h"

#include "support/temp_dir.h"
#include "support/test_framework.h"

namespace {

minikv::Options MakeOptions(const std::filesystem::path& db_path,
                            std::size_t level0_trigger = 1000) {
  minikv::Options options;
  options.db_path = db_path;
  options.sync_on_write = false;
  options.level0_compaction_trigger = level0_trigger;
  return options;
}

std::unique_ptr<minikv::Engine> OpenEngine(const std::filesystem::path& db_path,
                                           std::size_t level0_trigger = 1000) {
  std::unique_ptr<minikv::Engine> engine;
  const minikv::Status status =
      minikv::Engine::Open(MakeOptions(db_path, level0_trigger), &engine);
  return status.ok() ? std::move(engine) : nullptr;
}

std::vector<std::string> CollectKeys(minikv::Iterator* it) {
  std::vector<std::string> keys;
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
    keys.emplace_back(it->key());
  }
  return keys;
}

class SilentLogs {
 public:
  SilentLogs() { minikv::SetLogSink([](const std::string&) {}); }
  ~SilentLogs() { minikv::SetLogSink(minikv::LogSink{}); }

  SilentLogs(const SilentLogs&) = delete;
  SilentLogs& operator=(const SilentLogs&) = delete;
};

}  // namespace

TEST(Scan, MergesMemTableAndSstables) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);

  REQUIRE_OK(engine->Put("b", "1"));
  REQUIRE_OK(engine->Put("d", "2"));
  REQUIRE_OK(engine->Flush());  // b、d 进入 sst

  REQUIRE_OK(engine->Put("a", "3"));
  REQUIRE_OK(engine->Put("c", "4"));  // a、c 还在内存表

  std::unique_ptr<minikv::Iterator> it = engine->NewIterator();
  REQUIRE_TRUE(it != nullptr);

  const std::vector<std::string> keys = CollectKeys(it.get());
  REQUIRE_TRUE(keys.size() == 4);
  CHECK_EQ(keys[0], std::string("a"));
  CHECK_EQ(keys[1], std::string("b"));
  CHECK_EQ(keys[2], std::string("c"));
  CHECK_EQ(keys[3], std::string("d"));
  CHECK_OK(it->status());
  REQUIRE_OK(engine->Close());
}

TEST(Scan, ReturnsOnlyNewestVersionPerKey) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);

  REQUIRE_OK(engine->Put("k", "v1"));
  REQUIRE_OK(engine->Flush());
  REQUIRE_OK(engine->Put("k", "v2"));
  REQUIRE_OK(engine->Flush());
  REQUIRE_OK(engine->Put("k", "v3"));  // 最新版本在内存表

  std::unique_ptr<minikv::Iterator> it = engine->NewIterator();
  REQUIRE_TRUE(it != nullptr);

  std::vector<std::string> keys;
  std::string value;
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
    keys.emplace_back(it->key());
    value.assign(it->value());
  }

  REQUIRE_TRUE(keys.size() == 1);
  CHECK_EQ(value, std::string("v3"));
  REQUIRE_OK(engine->Close());
}

TEST(Scan, SkipsTombstones) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);

  REQUIRE_OK(engine->Put("a", "1"));
  REQUIRE_OK(engine->Put("b", "2"));
  REQUIRE_OK(engine->Put("c", "3"));
  REQUIRE_OK(engine->Flush());
  REQUIRE_OK(engine->Delete("b"));  // 墓碑在内存表

  std::unique_ptr<minikv::Iterator> it = engine->NewIterator();
  REQUIRE_TRUE(it != nullptr);

  const std::vector<std::string> keys = CollectKeys(it.get());
  REQUIRE_TRUE(keys.size() == 2);
  CHECK_EQ(keys[0], std::string("a"));
  CHECK_EQ(keys[1], std::string("c"));
  REQUIRE_OK(engine->Close());
}

TEST(Scan, SeekStartsAtOrAfterTarget) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);
  for (int i = 0; i < 20; ++i) {
    REQUIRE_OK(engine->Put("key" + std::to_string(i), "v"));
  }
  REQUIRE_OK(engine->Flush());

  std::unique_ptr<minikv::Iterator> it = engine->NewIterator();
  REQUIRE_TRUE(it != nullptr);

  it->Seek("key7");
  REQUIRE_TRUE(it->Valid());
  CHECK_STREQ(it->key(), "key7");

  it->Seek("key7x");  // 落在 key7 与 key8 之间
  REQUIRE_TRUE(it->Valid());
  CHECK_STREQ(it->key(), "key8");

  it->Seek("zzz");
  CHECK_FALSE(it->Valid());
  REQUIRE_OK(engine->Close());
}

TEST(Scan, EmptyDatabaseYieldsNothing) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  auto engine = OpenEngine(dir / "db");
  REQUIRE_TRUE(engine != nullptr);

  std::unique_ptr<minikv::Iterator> it = engine->NewIterator();
  REQUIRE_TRUE(it != nullptr);
  it->SeekToFirst();
  CHECK_FALSE(it->Valid());
  REQUIRE_OK(engine->Close());
}

TEST(Scan, SurvivesConcurrentCompaction) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  auto engine = OpenEngine(db_path, 2);  // 低阈值，扫描期间可能触发合并
  REQUIRE_TRUE(engine != nullptr);

  for (int i = 0; i < 30; ++i) {
    REQUIRE_OK(engine->Put("key" + std::to_string(i), "v"));
    if (i % 5 == 4) {
      REQUIRE_OK(engine->Flush());
    }
  }
  REQUIRE_OK(engine->Flush());

  // 先拿迭代器，再触发合并（合并会删除旧文件，迭代器应仍然可用）
  std::unique_ptr<minikv::Iterator> it = engine->NewIterator();
  REQUIRE_TRUE(it != nullptr);
  REQUIRE_OK(engine->Compact());

  int visited = 0;
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
    ++visited;
  }
  CHECK_EQ(visited, 30);
  CHECK_OK(it->status());
  REQUIRE_OK(engine->Close());
}
