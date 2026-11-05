// S1 阶段演示：把「WAL + MemTable + 崩溃恢复」跑成一段可读的 transcript。
//
// 设计约束（见 docs/demo-spec.md）：
//   - 输出确定性：除总耗时外逐字节可复现
//   - 行为断言只走 Engine 公开接口；WAL 字节用 wal_format 解码展示
//   - 任一步不符即 FAIL 并返回非零退出码
//   - transcript 同时落盘到 demo/s1-transcript.txt

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "minikv/api/engine.h"
#include "minikv/api/options.h"
#include "minikv/api/version.h"
#include "minikv/util/file.h"
#include "minikv/util/logging.h"
#include "minikv/wal/wal_format.h"

namespace minikv {
namespace tools {
namespace {

namespace fs = std::filesystem;

class Transcript {
 public:
  void Line(const std::string& text) {
    std::printf("%s\n", text.c_str());
    std::fflush(stdout);
    text_ += text;
    text_ += '\n';
  }

  void Separator() { Line(std::string(60, '=')); }

  bool Save(const fs::path& path) const {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    File file;
    if (!file.Open(path, FileOpenMode::kReadWrite).ok()) {
      return false;
    }
    if (!file.Truncate(0).ok()) {
      return false;
    }
    return file.Append(text_).ok();
  }

 private:
  std::string text_;
};

class Checker {
 public:
  explicit Checker(Transcript& out) : out_(out) {}

  void Check(bool condition, const std::string& what) {
    ++checks_;
    if (condition) {
      ++passed_;
      return;
    }
    failed_ = true;
    out_.Line("  [FAIL] " + what);
  }

  bool ok() const { return !failed_; }
  int checks() const { return checks_; }
  int passed() const { return passed_; }

 private:
  Transcript& out_;
  bool failed_ = false;
  int checks_ = 0;
  int passed_ = 0;
};

const char* TypeName(RecordType type) {
  return (type == RecordType::kPut) ? "PUT" : "DEL";
}

// 逐条解码 WAL，打印每条记录的偏移 / 类型 / 长度 / key
std::size_t DumpWal(const fs::path& wal_path, Transcript& out) {
  File file;
  if (!file.Open(wal_path, FileOpenMode::kReadOnly).ok()) {
    out.Line("  (cannot open wal file)");
    return 0;
  }

  std::string content;
  file.Read(0, static_cast<std::size_t>(file.Size()), &content);
  out.Line("  #   offset  type  key_len  val_len  key");

  std::string_view remaining(content);
  std::size_t offset = 0;
  std::size_t index = 0;
  while (!remaining.empty()) {
    WalRecord record;
    std::size_t consumed = 0;
    if (!DecodeRecord(remaining, &record, &consumed)) {
      out.Line("  !   decode failed at offset " + std::to_string(offset));
      break;
    }

    char line[256];
    std::snprintf(line, sizeof(line), "  %-3zu %-7zu %-5s %-8zu %-8zu %s", index, offset,
                  TypeName(record.type), record.key.size(), record.value.size(),
                  record.key.c_str());
    out.Line(line);

    offset += consumed;
    remaining.remove_prefix(consumed);
    ++index;
  }

  out.Line("  total " + std::to_string(index) + " records, " + std::to_string(content.size()) +
           " bytes");
  return index;
}

std::uint64_t FileSizeOrZero(const fs::path& path) {
  std::error_code ec;
  const auto size = fs::file_size(path, ec);
  return ec ? 0 : static_cast<std::uint64_t>(size);
}

void ShowGet(Transcript& out, Checker& checker, Engine& engine, const std::string& key,
             const std::string& expected) {
  std::string value;
  const Status status = engine.Get(key, &value);
  if (expected == "<notfound>") {
    out.Line("  get " + key + "  -> " + status.ToString());
    checker.Check(status.IsNotFound(), "expected NotFound for " + key);
    return;
  }
  out.Line("  get " + key + "  -> \"" + value + "\"");
  checker.Check(status.ok() && value == expected, "expected value for " + key);
}

}  // namespace

int RunDemoS1(const std::string& db_dir) {
  // 演示不关心日志噪音，全部由 transcript 自述
  SetLogSink([](const std::string&) {});

  Transcript out;
  Checker checker(out);

  const fs::path db_path(db_dir);
  const fs::path wal_path = db_path / "wal.log";

  out.Separator();
  out.Line(" MiniKV S1 Demo - WAL + MemTable + crash recovery");
  out.Line(" data dir : " + db_path.string());
  out.Separator();
  out.Line("");

  std::error_code ec;
  fs::remove_all(db_path, ec);

  // ---------------------------------------------------------------------
  out.Line("[1/8] open an empty database");
  std::unique_ptr<Engine> engine;
  {
    Options options;
    options.db_path = db_path;
    options.sync_on_write = true;  // 演示用最安全的模式
    const Status status = Engine::Open(options, &engine);
    checker.Check(status.ok(), "open engine: " + status.ToString());
    if (!status.ok()) {
      out.Line("  abort");
      return 1;
    }
  }
  checker.Check(fs::exists(wal_path), "wal.log created");
  out.Line("  wal file : " + wal_path.string());
  out.Line("  wal size : " + std::to_string(FileSizeOrZero(wal_path)) + " bytes");
  out.Line("");

  // ---------------------------------------------------------------------
  out.Line("[2/8] append 3 records");
  {
    const char* keys[] = {"user:1", "user:2", "counter"};
    const char* values[] = {"zhangsan", "lisi", "42"};
    for (int i = 0; i < 3; ++i) {
      const Status status = engine->Put(keys[i], values[i]);
      checker.Check(status.ok(), std::string("put ") + keys[i]);
      out.Line(std::string("  put ") + keys[i] + " \"" + values[i] + "\"  -> " +
               (status.ok() ? "OK" : status.ToString()));
    }
  }
  const std::uint64_t size_after_puts = FileSizeOrZero(wal_path);
  out.Line("  wal size : " + std::to_string(size_after_puts) + " bytes");
  out.Line("");

  // ---------------------------------------------------------------------
  out.Line("[3/8] decode the WAL (this is the evidence the log is correct)");
  {
    const std::size_t count = DumpWal(wal_path, out);
    checker.Check(count == 3, "wal contains 3 decodable records");
  }
  out.Line("");

  // ---------------------------------------------------------------------
  out.Line("[4/8] read back");
  ShowGet(out, checker, *engine, "user:1", "zhangsan");
  ShowGet(out, checker, *engine, "user:2", "lisi");
  ShowGet(out, checker, *engine, "counter", "42");
  out.Line("");

  // ---------------------------------------------------------------------
  out.Line("[5/8] delete user:2 (a tombstone is appended)");
  {
    const Status status = engine->Delete("user:2");
    checker.Check(status.ok(), "delete user:2");
    out.Line(std::string("  del user:2  -> ") + (status.ok() ? "OK" : status.ToString()));
  }
  ShowGet(out, checker, *engine, "user:2", "<notfound>");
  const std::uint64_t size_after_delete = FileSizeOrZero(wal_path);
  out.Line("  wal size : " + std::to_string(size_after_puts) + " -> " +
           std::to_string(size_after_delete) + " bytes");
  checker.Check(size_after_delete > size_after_puts, "wal grew after delete");
  out.Line("");

  // ---------------------------------------------------------------------
  out.Line("[6/8] simulate a crash: drop the engine without closing it");
  engine.release();  // 故意泄漏：不调用 Close、不刷盘
  out.Line("  process state discarded");
  out.Line("");

  // ---------------------------------------------------------------------
  out.Line("[7/8] reopen and replay the WAL");
  {
    std::size_t replayed = 0;
    {
      // 先自己数一遍记录条数，作为「回放了几条」的证据
      File file;
      if (file.Open(wal_path, FileOpenMode::kReadOnly).ok()) {
        std::string content;
        file.Read(0, static_cast<std::size_t>(file.Size()), &content);
        std::string_view remaining(content);
        while (!remaining.empty()) {
          WalRecord record;
          std::size_t consumed = 0;
          if (!DecodeRecord(remaining, &record, &consumed)) {
            break;
          }
          remaining.remove_prefix(consumed);
          ++replayed;
        }
      }
    }
    out.Line("  records on disk : " + std::to_string(replayed));
    checker.Check(replayed == 4, "4 records on disk (3 puts + 1 delete)");

    Options options;
    options.db_path = db_path;
    options.sync_on_write = true;
    const Status status = Engine::Open(options, &engine);
    checker.Check(status.ok(), "reopen engine: " + status.ToString());
    if (!status.ok()) {
      out.Line("  abort");
      return 1;
    }
  }
  ShowGet(out, checker, *engine, "user:1", "zhangsan");
  ShowGet(out, checker, *engine, "user:2", "<notfound>");  // 删除也被恢复
  ShowGet(out, checker, *engine, "counter", "42");
  out.Line("");

  // ---------------------------------------------------------------------
  out.Line("[8/8] simulate a half-written record at the tail");
  const Status close_status = engine->Close();
  checker.Check(close_status.ok(), "close engine");
  const std::uint64_t valid_size = FileSizeOrZero(wal_path);

  {
    File file;
    const Status open_status = file.Open(wal_path, FileOpenMode::kReadWrite);
    checker.Check(open_status.ok(), "open wal for garbage append");
    file.Append("garbage");
    out.Line("  appended 7 garbage bytes, size : " + std::to_string(FileSizeOrZero(wal_path)));
  }

  {
    Options options;
    options.db_path = db_path;
    options.sync_on_write = true;
    const Status status = Engine::Open(options, &engine);
    checker.Check(status.ok(), "reopen after garbage: " + status.ToString());
    if (status.ok()) {
      ShowGet(out, checker, *engine, "user:1", "zhangsan");
      out.Line("  wal size after recovery : " + std::to_string(FileSizeOrZero(wal_path)) +
               " bytes");
      checker.Check(FileSizeOrZero(wal_path) == valid_size, "wal truncated back to valid size");
      engine->Close();
    }
  }
  out.Line("");

  out.Separator();
  char summary[256];
  std::snprintf(summary, sizeof(summary), " result: %d/%d checks passed  (%s)", checker.passed(),
                checker.checks(), checker.ok() ? "OK" : "FAILED");
  out.Line(summary);
  out.Separator();

  const fs::path transcript_path = "demo/s1-transcript.txt";
  const bool saved = out.Save(transcript_path);
  std::printf("\ntranscript written to %s%s\n", transcript_path.string().c_str(),
              saved ? "" : " (FAILED)");

  return checker.ok() ? 0 : 1;
}

namespace {

struct DataFile {
  std::string name;
  std::uint64_t size = 0;
};

std::vector<DataFile> ListDataFiles(const fs::path& dir) {
  std::vector<DataFile> files;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    DataFile file;
    file.name = entry.path().filename().string();
    const auto size = fs::file_size(entry.path(), ec);
    file.size = ec ? 0 : static_cast<std::uint64_t>(size);
    files.push_back(std::move(file));
  }
  std::sort(files.begin(), files.end(),
            [](const DataFile& a, const DataFile& b) { return a.name < b.name; });
  return files;
}

int CountSstables(const std::vector<DataFile>& files) {
  int count = 0;
  for (const DataFile& file : files) {
    if (file.name.size() == 10 && file.name.compare(6, 4, ".sst") == 0) {
      ++count;
    }
  }
  return count;
}

}  // namespace

int RunDemoS2(const std::string& db_dir) {
  SetLogSink([](const std::string&) {});

  Transcript out;
  Checker checker(out);

  const fs::path db_path(db_dir);
  out.Separator();
  out.Line(" MiniKV S2 Demo - SSTable flush + restart");
  out.Line(" data dir : " + db_path.string());
  out.Line(" flush threshold : 2 KB");
  out.Separator();
  out.Line("");

  std::error_code ec;
  fs::remove_all(db_path, ec);

  Options options;
  options.db_path = db_path;
  options.sync_on_write = true;
  options.memtable_size = 2048;

  std::unique_ptr<Engine> engine;
  auto open_engine = [&]() -> bool {
    engine.reset();
    const Status status = Engine::Open(options, &engine);
    checker.Check(status.ok(), "open engine: " + status.ToString());
    return status.ok();
  };

  // ---------------------------------------------------------------------
  out.Line("[1/7] open an empty database");
  if (!open_engine()) {
    return 1;
  }
  {
    const EngineStats stats = engine->GetStats();
    out.Line("  sstables : " + std::to_string(stats.sstable_count) + ", wal bytes : " +
             std::to_string(stats.wal_bytes));
    checker.Check(stats.sstable_count == 0, "empty database has no sstable");
  }
  out.Line("");

  // ---------------------------------------------------------------------
  out.Line("[2/7] write 200 entries (auto flush at 2 KB)");
  constexpr int kCount = 200;
  {
    int failed = 0;
    for (int i = 0; i < kCount; ++i) {
      char key[32];
      char value[48];
      std::snprintf(key, sizeof(key), "user:%04d", i);
      std::snprintf(value, sizeof(value), "value-%04dxxxxxxxx", i);
      if (!engine->Put(key, value).ok()) {
        ++failed;
      }
    }
    checker.Check(failed == 0, "all 200 puts succeeded");
    const EngineStats stats = engine->GetStats();
    out.Line("  sstables after writes : " + std::to_string(stats.sstable_count));
    out.Line("  memtable              : " + std::to_string(stats.memtable_entries) +
             " entries, " + std::to_string(stats.memtable_bytes) + " bytes");
    checker.Check(stats.sstable_count >= 2, "threshold triggered at least 2 flushes");
  }
  out.Line("");

  // ---------------------------------------------------------------------
  out.Line("[3/7] files on disk");
  {
    const std::vector<DataFile> files = ListDataFiles(db_path);
    for (const DataFile& file : files) {
      out.Line("  " + file.name + "  " + std::to_string(file.size) + " bytes");
    }
    checker.Check(CountSstables(files) ==
                      static_cast<int>(engine->GetStats().sstable_count),
                  "on-disk sstable count matches stats");
    checker.Check(fs::exists(db_path / "MANIFEST"), "MANIFEST exists");
    checker.Check(fs::exists(db_path / "wal.log"), "wal.log exists");
  }
  out.Line("");

  // ---------------------------------------------------------------------
  out.Line("[4/7] close (flushes the tail) and reopen");
  {
    checker.Check(engine->Close().ok(), "close engine");
    if (!open_engine()) {
      return 1;
    }
    const EngineStats stats = engine->GetStats();
    out.Line("  after reopen : sstables = " + std::to_string(stats.sstable_count) +
             ", wal bytes = " + std::to_string(stats.wal_bytes));
    checker.Check(stats.wal_bytes == 0, "wal is empty because everything was flushed");

    int mismatches = 0;
    for (int i = 0; i < kCount; ++i) {
      char key[32];
      char expected[48];
      std::snprintf(key, sizeof(key), "user:%04d", i);
      std::snprintf(expected, sizeof(expected), "value-%04dxxxxxxxx", i);
      std::string value;
      if (!engine->Get(key, &value).ok() || value != expected) {
        ++mismatches;
      }
    }
    out.Line("  verified " + std::to_string(kCount) + " keys, mismatches = " +
             std::to_string(mismatches));
    checker.Check(mismatches == 0, "all entries survive restart");
  }
  out.Line("");

  // ---------------------------------------------------------------------
  out.Line("[5/7] delete 10 keys, then flush");
  {
    int failed = 0;
    for (int i = 0; i < 10; ++i) {
      char key[32];
      std::snprintf(key, sizeof(key), "user:%04d", i);
      if (!engine->Delete(key).ok()) {
        ++failed;
      }
    }
    checker.Check(failed == 0, "10 deletes succeeded");
    checker.Check(engine->Flush().ok(), "flush after deletes");
    out.Line("  sstables after flush : " +
             std::to_string(engine->GetStats().sstable_count));
  }
  out.Line("");

  // ---------------------------------------------------------------------
  out.Line("[6/7] restart and verify tombstones");
  {
    checker.Check(engine->Close().ok(), "close before restart");
    if (!open_engine()) {
      return 1;
    }

    int resurrected = 0;
    int lost = 0;
    for (int i = 0; i < kCount; ++i) {
      char key[32];
      std::snprintf(key, sizeof(key), "user:%04d", i);
      std::string value;
      const Status status = engine->Get(key, &value);
      if (i < 10) {
        if (!status.IsNotFound()) {
          ++resurrected;
        }
      } else if (!status.ok()) {
        ++lost;
      }
    }
    out.Line("  deleted-but-resurrected = " + std::to_string(resurrected) +
             ", lost = " + std::to_string(lost));
    checker.Check(resurrected == 0, "deleted keys stay deleted");
    checker.Check(lost == 0, "remaining keys are intact");
  }
  out.Line("");

  // ---------------------------------------------------------------------
  out.Line("[7/7] summary");
  {
    const std::vector<DataFile> files = ListDataFiles(db_path);
    std::uint64_t total_bytes = 0;
    for (const DataFile& file : files) {
      total_bytes += file.size;
    }
    out.Line("  sstables     : " + std::to_string(CountSstables(files)));
    out.Line("  total on disk: " + std::to_string(total_bytes) + " bytes");
    out.Line("  wal bytes    : " + std::to_string(engine->GetStats().wal_bytes));
    checker.Check(CountSstables(files) >= 3, "deletes produced an additional sstable");
    checker.Check(engine->Close().ok(), "final close");
  }

  out.Line("");
  out.Separator();
  char summary[256];
  std::snprintf(summary, sizeof(summary), " result: %d/%d checks passed  (%s)", checker.passed(),
                checker.checks(), checker.ok() ? "OK" : "FAILED");
  out.Line(summary);
  out.Separator();

  const fs::path transcript_path = "demo/s2-transcript.txt";
  const bool saved = out.Save(transcript_path);
  std::printf("\ntranscript written to %s%s\n", transcript_path.string().c_str(),
              saved ? "" : " (FAILED)");

  return checker.ok() ? 0 : 1;
}

}  // namespace tools
}  // namespace minikv
