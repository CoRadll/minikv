// 作为库使用 MiniKV 的最小示例。
//
// 编译：随主工程一起构建（CMake 目标 minikv_example），
//       或把 include/ 加进头文件路径、链接 minikv_core 静态库。
//
// 运行：.\build\bin\Debug\minikv_example.exe --dir .\example-data

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "minikv/api/engine.h"
#include "minikv/api/iterator.h"
#include "minikv/api/options.h"

namespace {

void PrintStatus(const char* what, const minikv::Status& status) {
  std::printf("%-12s %s\n", what, status.ok() ? "OK" : status.ToString().c_str());
}

}  // namespace

int main(int argc, char** argv) {
  std::string db_path = "example-data";
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--dir") == 0) {
      db_path = argv[i + 1];
    }
  }

  // 1) 配置：所有字段都有默认值，这里只改关心的几项
  minikv::Options options;
  options.db_path = db_path;
  options.sync_on_write = false;              // 测试/性能场景关闭；要最安全就置 true
  options.memtable_size = 64 * 1024;          // 64KB 就刷盘，便于演示
  options.level0_compaction_trigger = 4;      // L0 文件数达到 4 触发合并
  options.block_cache_size = 4 * 1024 * 1024; // 4MB 块缓存

  // 2) 打开（不存在则创建；存在则自动恢复）
  std::unique_ptr<minikv::Engine> db;
  const minikv::Status open_status = minikv::Engine::Open(options, &db);
  PrintStatus("open", open_status);
  if (!open_status.ok()) {
    return 1;
  }

  // 3) 写入、读取、删除（全部返回 Status，不抛异常）
  PrintStatus("put user:1", db->Put("user:1", "alice"));
  PrintStatus("put user:2", db->Put("user:2", "bob"));
  PrintStatus("put config", db->Put("config", "timeout=30"));
  PrintStatus("del user:2", db->Delete("user:2"));

  std::string value;
  if (db->Get("user:1", &value).ok()) {
    std::printf("get user:1  -> %s\n", value.c_str());
  }
  if (db->Get("user:2", &value).IsNotFound()) {
    std::printf("get user:2  -> NOT FOUND (deleted)\n");
  }

  // 4) 有序扫描（内存表 + 各层 SSTable 合并后的视图）
  std::unique_ptr<minikv::Iterator> it = db->NewIterator();
  std::printf("scan        ->\n");
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
    std::printf("  %s = %s\n", std::string(it->key()).c_str(), std::string(it->value()).c_str());
  }

  // 5) 观测指标（只读，不影响行为）
  const minikv::EngineStats stats = db->GetStats();
  std::printf("stats       -> sstables=%zu, wal=%llu bytes, cache=%llu hit / %llu miss\n",
              stats.sstable_count, static_cast<unsigned long long>(stats.wal_bytes),
              static_cast<unsigned long long>(stats.block_cache_hits),
              static_cast<unsigned long long>(stats.block_cache_misses));

  // 6) 关闭：会把内存表刷成 SSTable 并丢弃 WAL
  PrintStatus("close", db->Close());
  return 0;
}
