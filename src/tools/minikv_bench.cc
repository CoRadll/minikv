#include "bench.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "minikv/api/engine.h"
#include "minikv/api/iterator.h"

namespace minikv::tools {
namespace {

using Clock = std::chrono::steady_clock;

double SecondsSince(Clock::time_point start) {
  const auto elapsed = Clock::now() - start;
  return std::chrono::duration<double>(elapsed).count();
}

std::uint64_t Percentile(const std::vector<std::uint64_t>& sorted, double ratio) {
  if (sorted.empty()) {
    return 0;
  }
  const double position = ratio * static_cast<double>(sorted.size() - 1);
  const auto index = static_cast<std::size_t>(position + 0.5);
  return sorted[std::min(index, sorted.size() - 1)];
}

std::string BenchKey(int index) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "key%08d", index);
  return std::string(buffer);
}

std::string BenchValue(int index) {
  // 固定 100 字节左右的 value，让写入量与 key 数量关系稳定
  std::string value(100, 'v');
  const std::string suffix = std::to_string(index);
  value.replace(0, suffix.size(), suffix);
  return value;
}

}  // namespace

BenchResult RunBenchmark(const Options& options, int key_count, int read_count) {
  BenchResult result;
  if (key_count <= 0 || read_count <= 0) {
    return result;
  }
  result.key_count = static_cast<std::uint64_t>(key_count);
  result.read_count = static_cast<std::uint64_t>(read_count);

  std::unique_ptr<Engine> engine;
  const Status open_status = Engine::Open(options, &engine);
  if (!open_status.ok()) {
    return result;
  }

  // 1) 写入
  const auto write_start = Clock::now();
  for (int i = 0; i < key_count; ++i) {
    if (!engine->Put(BenchKey(i), BenchValue(i)).ok()) {
      return result;
    }
  }
  result.write_seconds = SecondsSince(write_start);

  // 2) 点查（约 90% 命中，10% 不存在）
  std::vector<std::uint64_t> latencies_us;
  latencies_us.reserve(static_cast<std::size_t>(read_count));

  std::uint64_t state = 0x2545F4914F6CDD1Dull;
  const auto read_start = Clock::now();
  for (int i = 0; i < read_count; ++i) {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    const int pick = static_cast<int>((state >> 33) % static_cast<std::uint64_t>(key_count));
    const bool missing = (i % 10 == 0);

    const std::string key = missing ? ("absent:" + std::to_string(i)) : BenchKey(pick);

    const auto op_start = Clock::now();
    std::string value;
    const Status status = engine->Get(key, &value);
    const auto op_end = Clock::now();

    latencies_us.push_back(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(op_end - op_start).count()));

    if (status.ok()) {
      ++result.read_hits;
    } else if (status.IsNotFound()) {
      ++result.read_misses;
    } else {
      return result;  // 读错误：压测结果无效
    }
  }
  result.read_seconds = SecondsSince(read_start);

  // 3) 全量扫描
  const auto scan_start = Clock::now();
  std::unique_ptr<Iterator> it = engine->NewIterator();
  if (it == nullptr) {
    return result;
  }
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
    ++result.scan_entries;
  }
  if (!it->status().ok()) {
    return result;
  }
  result.scan_seconds = SecondsSince(scan_start);

  std::sort(latencies_us.begin(), latencies_us.end());
  result.latency_p50_us = Percentile(latencies_us, 0.50);
  result.latency_p95_us = Percentile(latencies_us, 0.95);
  result.latency_p99_us = Percentile(latencies_us, 0.99);

  if (!engine->Close().ok()) {
    return result;
  }

  result.ok = true;
  return result;
}

}  // namespace minikv::tools
