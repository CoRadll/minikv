// MiniKV 命令行：交互式 REPL 与单次命令。
//
// 用法：
//   minikv [--dir <path>]
//
// 命令：
//   put <key> <value>   写入 / 覆盖（value 可含空格）
//   get <key>           读取
//   del <key>           删除
//   stats               打印引擎状态
//   help                命令列表
//   exit / quit         退出

#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include "minikv/api/engine.h"
#include "minikv/api/options.h"
#include "minikv/api/version.h"
#include "minikv/util/logging.h"

namespace {

void PrintUsage() {
  std::printf("usage: minikv [--dir <path>] [--verbose] [--help] [--version]\n");
  std::printf("\n");
  std::printf("commands:\n");
  std::printf("  put <key> <value>   write or overwrite a key\n");
  std::printf("  get <key>           read a key\n");
  std::printf("  del <key>           delete a key\n");
  std::printf("  stats               print engine stats\n");
  std::printf("  help                show this help\n");
  std::printf("  exit | quit         leave\n");
}

void PrintStats(minikv::Engine* engine, const std::string& db_path) {
  const minikv::EngineStats stats = engine->GetStats();
  std::printf("db path        : %s\n", db_path.c_str());
  std::printf("wal bytes      : %llu\n", static_cast<unsigned long long>(stats.wal_bytes));
  std::printf("memtable       : %zu entries, %zu bytes (approx)\n", stats.memtable_entries,
              stats.memtable_bytes);
}

// 取第一个空白分隔的 token，其余作为 remainder（不做 trim 以外的处理）
bool SplitFirstToken(const std::string& line, std::string* head, std::string* rest) {
  const std::size_t begin = line.find_first_not_of(" \t");
  if (begin == std::string::npos) {
    return false;
  }
  const std::size_t end = line.find_first_of(" \t", begin);
  if (end == std::string::npos) {
    *head = line.substr(begin);
    rest->clear();
    return true;
  }
  *head = line.substr(begin, end - begin);
  const std::size_t rest_begin = line.find_first_not_of(" \t", end);
  *rest = (rest_begin == std::string::npos) ? std::string() : line.substr(rest_begin);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::string db_path = "data";
  bool verbose = false;

  // REPL 里 INFO 日志太吵，默认只显示 WARN；--verbose 打开完整日志
  minikv::SetLogLevel(minikv::LogLevel::kWarn);

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--dir" && i + 1 < argc) {
      db_path = argv[++i];
    } else if (arg == "--verbose") {
      verbose = true;
    } else if (arg == "--help" || arg == "-h") {
      PrintUsage();
      return 0;
    } else if (arg == "--version") {
      std::printf("%.*s %.*s\n", static_cast<int>(minikv::kProjectName.size()),
                  minikv::kProjectName.data(), static_cast<int>(minikv::kVersion.size()),
                  minikv::kVersion.data());
      return 0;
    } else {
      std::fprintf(stderr, "unknown argument: %.*s\n", static_cast<int>(arg.size()), arg.data());
      return 2;
    }
  }

  minikv::Options options;
  options.db_path = db_path;

  if (verbose) {
    minikv::SetLogLevel(minikv::LogLevel::kInfo);
  }

  std::unique_ptr<minikv::Engine> engine;
  const minikv::Status open_status = minikv::Engine::Open(options, &engine);
  if (!open_status.ok()) {
    std::fprintf(stderr, "failed to open %s: %s\n", db_path.c_str(),
                 open_status.ToString().c_str());
    return 1;
  }

  std::printf("%.*s %.*s  |  db = %s\n", static_cast<int>(minikv::kProjectName.size()),
              minikv::kProjectName.data(), static_cast<int>(minikv::kVersion.size()),
              minikv::kVersion.data(), db_path.c_str());
  std::printf("type 'help' for commands, 'exit' to quit\n\n");

  std::string line;
  while (true) {
    std::printf("minikv> ");
    std::fflush(stdout);

    if (!std::getline(std::cin, line)) {
      std::printf("\n");
      break;
    }

    std::string command;
    std::string rest;
    if (!SplitFirstToken(line, &command, &rest)) {
      continue;
    }

    if (command == "exit" || command == "quit") {
      break;
    }

    if (command == "help") {
      PrintUsage();
      continue;
    }

    if (command == "stats") {
      PrintStats(engine.get(), db_path);
      continue;
    }

    std::string key;
    std::string value;

    // 先确认命令合法，再解析参数：否则未知命令会被误报成「缺少 key」
    const bool is_put = (command == "put");
    const bool is_get = (command == "get");
    const bool is_del = (command == "del" || command == "delete");
    if (!is_put && !is_get && !is_del) {
      std::printf("ERR: unknown command '%s' (type 'help')\n", command.c_str());
      continue;
    }

    if (!SplitFirstToken(rest, &key, &value)) {
      std::printf("ERR: %s needs a key\n", command.c_str());
      continue;
    }

    if (is_put) {
      if (value.empty() && rest.find_first_of(" \t") == std::string::npos) {
        std::printf("ERR: put needs both key and value\n");
        continue;
      }
      const minikv::Status status = engine->Put(key, value);
      std::printf("%s\n", status.ok() ? "OK" : status.ToString().c_str());
      continue;
    }

    if (is_get) {
      std::string out;
      const minikv::Status status = engine->Get(key, &out);
      if (status.ok()) {
        std::printf("\"%s\"\n", out.c_str());
      } else if (status.IsNotFound()) {
        std::printf("NOT FOUND\n");
      } else {
        std::printf("%s\n", status.ToString().c_str());
      }
      continue;
    }

    const minikv::Status status = engine->Delete(key);
    std::printf("%s\n", status.ok() ? "OK" : status.ToString().c_str());
  }

  const minikv::Status close_status = engine->Close();
  if (!close_status.ok()) {
    std::fprintf(stderr, "close failed: %s\n", close_status.ToString().c_str());
    return 1;
  }
  return 0;
}
