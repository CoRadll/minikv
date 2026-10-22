#pragma once

#include <cstdint>

namespace minikv {

// 高质量伪随机数。不使用 rand()：不可复现、分布差、线程不安全。
// 用途：跳表随机层高、测试数据生成、压测 workload。
class Random {
 public:
  explicit Random(uint64_t seed);

  uint32_t Next();
  uint64_t Next64();
  uint32_t Uniform(uint32_t n);  // [0, n)
  bool OneIn(int n);
  int Skewed(int max_log);  // 跳表层高，返回 [0, max_log]

 private:
  uint64_t state_;
};

}  // namespace minikv
