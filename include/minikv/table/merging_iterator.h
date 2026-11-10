#pragma once

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

#include "minikv/api/iterator.h"

namespace minikv {

// 多路归并：同时遍历多个有序来源，按 key 升序输出。
//
// 优先级约定：children_ 中下标越小越"新"。同一个 key 在多个来源出现时，
// 归并器返回下标最小的那个，所以调用方应按「新的在前」顺序传入。
class MergingIterator : public Iterator {
 public:
  explicit MergingIterator(std::vector<std::unique_ptr<Iterator>> children);

  bool Valid() const override { return valid_; }
  void SeekToFirst() override;
  void Seek(std::string_view target) override;
  void Next() override;

  std::string_view key() const override;
  std::string_view value() const override;
  bool IsDeleted() const override;
  Status status() const override;

 private:
  void FindSmallest();

  std::vector<std::unique_ptr<Iterator>> children_;
  std::size_t current_ = 0;
  bool valid_ = false;
};

}  // namespace minikv
