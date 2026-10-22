#pragma once

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

#include "minikv/api/iterator.h"

namespace minikv {

// TODO(S5): 多路归并。同一个 key 在多个来源出现时，只保留最新版本。
// 草案：S5 实现时允许调整接口签名（需要来源优先级来判定"最新"）。
class MergingIterator : public Iterator {
 public:
  explicit MergingIterator(std::vector<std::unique_ptr<Iterator>> children);

  bool Valid() const override;
  void SeekToFirst() override;
  void Seek(std::string_view target) override;
  void Next() override;

  std::string_view key() const override;
  std::string_view value() const override;
  Status status() const override;

 private:
  std::vector<std::unique_ptr<Iterator>> children_;
  std::size_t current_ = 0;
};

}  // namespace minikv
