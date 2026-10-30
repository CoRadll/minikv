// S1 阶段演示：把「WAL + MemTable + 崩溃恢复」跑成一段可读的 transcript。
//
// 设计约束（见 docs/demo-spec.md）：
//   - 输出确定性：除总耗时外逐字节可复现
//   - 行为断言只走 Engine 公开接口；WAL 字节用 wal_format 解码展示
//   - 任一步不符即 FAIL 并返回非零退出码
//   - transcript 同时落盘到 demo/s1-transcript.txt

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

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

}  // namespace tools
}  // namespace minikv
