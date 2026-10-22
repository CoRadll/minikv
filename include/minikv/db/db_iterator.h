#pragma once

#include <memory>
#include <string_view>

#include "minikv/api/iterator.h"

namespace minikv {

// TODO(S5): 把 MemTable 迭代器与各层 SSTable 迭代器合成对外的有序视图。
// 草案：S5 实现时允许调整接口签名。
class DbIterator : public Iterator {
 public:
  explicit DbIterator(std::unique_ptr<Iterator> internal);

  bool Valid() const override;
  void SeekToFirst() override;
  void Seek(std::string_view target) override;
  void Next() override;

  std::string_view key() const override;
  std::string_view value() const override;
  Status status() const override;

 private:
  std::unique_ptr<Iterator> internal_;
};

}  // namespace minikv
