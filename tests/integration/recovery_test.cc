// 端到端恢复测试：只通过 Engine 的公开接口操作，
// 覆盖「写入 → 关闭 → 重开」这条真实使用路径。

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <string>

#include "minikv/api/engine.h"
#include "minikv/api/options.h"
#include "minikv/util/env.h"
#include "minikv/util/file.h"
#include "minikv/util/logging.h"

#include "support/temp_dir.h"
#include "support/test_framework.h"

namespace {

minikv::Options MakeOptions(const std::filesystem::path& db_path, bool sync_on_write = false) {
  minikv::Options options;
  options.db_path = db_path;
  options.sync_on_write = sync_on_write;
  return options;
}

std::unique_ptr<minikv::Engine> OpenEngine(const std::filesystem::path& db_path,
                                           bool sync_on_write = false) {
  std::unique_ptr<minikv::Engine> engine;
  const minikv::Status status = minikv::Engine::Open(MakeOptions(db_path, sync_on_write), &engine);
  return status.ok() ? std::move(engine) : nullptr;
}

// 截断与恢复提示是预期行为，屏蔽日志以免污染输出
class SilentLogs {
 public:
  SilentLogs() { minikv::SetLogSink([](const std::string&) {}); }
  ~SilentLogs() { minikv::SetLogSink(minikv::LogSink{}); }

  SilentLogs(const SilentLogs&) = delete;
  SilentLogs& operator=(const SilentLogs&) = delete;
};

}  // namespace

TEST(Recovery, ThousandWritesSurviveRestart) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  constexpr int kCount = 1000;
  {
    auto engine = OpenEngine(db_path);
    REQUIRE_TRUE(engine != nullptr);
    for (int i = 0; i < kCount; ++i) {
      REQUIRE_OK(engine->Put("key:" + std::to_string(i), "value:" + std::to_string(i)));
    }
    REQUIRE_OK(engine->Close());
  }

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);

  int mismatches = 0;
  for (int i = 0; i < kCount; ++i) {
    std::string value;
    const minikv::Status status = engine->Get("key:" + std::to_string(i), &value);
    if (!status.ok() || value != "value:" + std::to_string(i)) {
      ++mismatches;
    }
  }
  CHECK_EQ(mismatches, 0);
  REQUIRE_OK(engine->Close());
}

TEST(Recovery, RandomOperationsMatchModelAfterRestart) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  std::map<std::string, std::string> model;
  std::mt19937 rng(20261030);
  constexpr int kKeys = 50;
  constexpr int kOps = 400;

  {
    auto engine = OpenEngine(db_path);
    REQUIRE_TRUE(engine != nullptr);

    for (int i = 0; i < kOps; ++i) {
      const std::string key = "k" + std::to_string(rng() % kKeys);
      // 1/3 概率删除，其余写入
      if (rng() % 3 == 0) {
        REQUIRE_OK(engine->Delete(key));
        model.erase(key);
      } else {
        const std::string value = "v" + std::to_string(rng() % 1000);
        REQUIRE_OK(engine->Put(key, value));
        model[key] = value;
      }
    }
    REQUIRE_OK(engine->Close());
  }

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);

  int mismatches = 0;
  for (int i = 0; i < kKeys; ++i) {
    const std::string key = "k" + std::to_string(i);
    std::string value;
    const minikv::Status status = engine->Get(key, &value);

    const auto expected = model.find(key);
    if (expected == model.end()) {
      if (!status.IsNotFound()) {
        ++mismatches;
      }
    } else if (!status.ok() || value != expected->second) {
      ++mismatches;
    }
  }
  CHECK_EQ(mismatches, 0);
  REQUIRE_OK(engine->Close());
}

TEST(Recovery, GarbageTailIsDroppedAndDataSurvives) {
  SilentLogs silence;
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  std::uint64_t valid_size = 0;
  {
    auto engine = OpenEngine(db_path);
    REQUIRE_TRUE(engine != nullptr);
    REQUIRE_OK(engine->Put("keep:1", "one"));
    REQUIRE_OK(engine->Put("keep:2", "two"));
    REQUIRE_OK(engine->Close());

    CHECK_OK(minikv::FileSize(db_path / "wal.log", &valid_size));
  }

  {
    minikv::File file;
    REQUIRE_OK(file.Open(db_path / "wal.log", minikv::FileOpenMode::kReadWrite));
    REQUIRE_OK(file.Append("this-is-a-half-written-record"));
  }

  {
    auto engine = OpenEngine(db_path);
    REQUIRE_TRUE(engine != nullptr);

    std::string value;
    CHECK_OK(engine->Get("keep:1", &value));
    CHECK_STREQ(value, "one");
    CHECK_OK(engine->Get("keep:2", &value));
    CHECK_STREQ(value, "two");
    REQUIRE_OK(engine->Close());
  }

  std::uint64_t size_after = 0;
  CHECK_OK(minikv::FileSize(db_path / "wal.log", &size_after));
  CHECK_EQ(size_after, valid_size);
}

TEST(Recovery, OverwriteAndDeletePersist) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  {
    auto engine = OpenEngine(db_path);
    REQUIRE_TRUE(engine != nullptr);
    REQUIRE_OK(engine->Put("k", "v1"));
    REQUIRE_OK(engine->Put("k", "v2"));
    REQUIRE_OK(engine->Put("other", "x"));
    REQUIRE_OK(engine->Delete("other"));
    REQUIRE_OK(engine->Close());
  }

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);

  std::string value;
  CHECK_OK(engine->Get("k", &value));
  CHECK_STREQ(value, "v2");
  CHECK_TRUE(engine->Get("other", &value).IsNotFound());
  REQUIRE_OK(engine->Close());
}

TEST(Recovery, LargeValueSurvivesRestart) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  const std::string large(1024 * 1024, 'x');
  {
    auto engine = OpenEngine(db_path);
    REQUIRE_TRUE(engine != nullptr);
    REQUIRE_OK(engine->Put("big", large));
    REQUIRE_OK(engine->Close());
  }

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);
  std::string value;
  CHECK_OK(engine->Get("big", &value));
  CHECK_EQ(value.size(), large.size());
  CHECK_TRUE(value == large);
  REQUIRE_OK(engine->Close());
}

TEST(Recovery, ReopenDoesNotRewriteWal) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  std::uint64_t first_size = 0;
  {
    auto engine = OpenEngine(db_path);
    REQUIRE_TRUE(engine != nullptr);
    REQUIRE_OK(engine->Put("a", "1"));
    REQUIRE_OK(engine->Put("b", "2"));
    REQUIRE_OK(engine->Close());
    CHECK_OK(minikv::FileSize(db_path / "wal.log", &first_size));
  }

  {
    auto engine = OpenEngine(db_path);
    REQUIRE_TRUE(engine != nullptr);
    REQUIRE_OK(engine->Close());
  }

  std::uint64_t second_size = 0;
  CHECK_OK(minikv::FileSize(db_path / "wal.log", &second_size));
  CHECK_EQ(second_size, first_size);
}

TEST(Recovery, PutAfterDeleteAcrossRestarts) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  {
    auto engine = OpenEngine(db_path);
    REQUIRE_TRUE(engine != nullptr);
    REQUIRE_OK(engine->Put("k", "old"));
    REQUIRE_OK(engine->Delete("k"));
    REQUIRE_OK(engine->Close());
  }

  {
    auto engine = OpenEngine(db_path);
    REQUIRE_TRUE(engine != nullptr);
    REQUIRE_OK(engine->Put("k", "new"));
    REQUIRE_OK(engine->Close());
  }

  auto engine = OpenEngine(db_path);
  REQUIRE_TRUE(engine != nullptr);
  std::string value;
  CHECK_OK(engine->Get("k", &value));
  CHECK_STREQ(value, "new");
  REQUIRE_OK(engine->Close());
}

TEST(Recovery, SyncOnWriteSurvivesRestart) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  const auto db_path = dir / "db";

  {
    auto engine = OpenEngine(db_path, true);
    REQUIRE_TRUE(engine != nullptr);
    REQUIRE_OK(engine->Put("durable", "yes"));
    REQUIRE_OK(engine->Close());
  }

  auto engine = OpenEngine(db_path, true);
  REQUIRE_TRUE(engine != nullptr);
  std::string value;
  CHECK_OK(engine->Get("durable", &value));
  CHECK_STREQ(value, "yes");
  REQUIRE_OK(engine->Close());
}
