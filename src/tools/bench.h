#pragma once

#include <cstdint>

#include "minikv/api/options.h"

namespace minikv::tools {

// 一次压测的原始结果。demo s5 直接断言这里的字段。
struct BenchResult {
  bool ok = false;
  std::uint64_t key_count = 0;
  std::uint64_t read_count = 0;
  std::uint64_t read_hits = 0;
  std::uint64_t read_misses = 0;
  std::uint64_t scan_entries = 0;
  double write_seconds = 0.0;
  double read_seconds = 0.0;
  double scan_seconds = 0.0;
  std::uint64_t latency_p50_us = 0;
  std::uint64_t latency_p95_us = 0;
  std::uint64_t latency_p99_us = 0;
};

BenchResult RunBenchmark(const Options& options, int key_count, int read_count);

}  // namespace minikv::tools
