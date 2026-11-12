#include <cstddef>
#include <string>

#include "minikv/table/bloom.h"

#include "support/test_framework.h"

TEST(BloomFilter, NeverHasFalseNegatives) {
  minikv::BloomFilter bloom(1000);
  for (int i = 0; i < 1000; ++i) {
    bloom.Add("key" + std::to_string(i));
  }

  int missed = 0;
  for (int i = 0; i < 1000; ++i) {
    if (!bloom.MayContain("key" + std::to_string(i))) {
      ++missed;
    }
  }
  CHECK_EQ(missed, 0);  // 已插入的 key 绝不能判为不存在
}

TEST(BloomFilter, FalsePositiveRateIsLow) {
  constexpr int kKeys = 2000;
  constexpr int kProbes = 10000;

  minikv::BloomFilter bloom(kKeys);
  for (int i = 0; i < kKeys; ++i) {
    bloom.Add("present:" + std::to_string(i));
  }

  int false_positives = 0;
  for (int i = 0; i < kProbes; ++i) {
    if (bloom.MayContain("absent:" + std::to_string(i))) {
      ++false_positives;
    }
  }

  // 10 bits/key 的理论误判率约 1%，留足余量要求 < 5%
  const double rate = static_cast<double>(false_positives) / kProbes;
  CHECK_TRUE(rate < 0.05);
}

TEST(BloomFilter, SerializedFormIsQueryable) {
  minikv::BloomFilter bloom(500);
  for (int i = 0; i < 500; ++i) {
    bloom.Add("k" + std::to_string(i));
  }

  const std::string encoded = bloom.Finish();
  CHECK_TRUE(encoded.size() > 1);

  // 落盘格式可以直接查询：这是 TableReader 的用法
  CHECK_TRUE(minikv::BloomFilter::MayContainIn(encoded, "k123"));
  CHECK_TRUE(minikv::BloomFilter::MayContainIn(encoded, "k499"));
}

TEST(BloomFilter, EmptyFilterRejectsEverything) {
  minikv::BloomFilter bloom(0);
  CHECK_FALSE(bloom.MayContain("anything"));
}

TEST(BloomFilter, CorruptFilterFailsOpen) {
  // 数据缺失时保守放行，避免误删真实数据
  CHECK_TRUE(minikv::BloomFilter::MayContainIn("", "k"));
  CHECK_TRUE(minikv::BloomFilter::MayContainIn(std::string(1, '\0'), "k"));
}
