#pragma once

#include <string_view>

#include "minikv/util/status.h"

namespace minikv {

// 有序迭代器。MemTable 与 SSTable 都实现这一接口，
// 上层可以用同一个循环遍历任意来源的有序数据。
class Iterator {
 public:
  virtual ~Iterator() = default;

  virtual bool Valid() const = 0;
  virtual void SeekToFirst() = 0;
  virtual void Seek(std::string_view target) = 0;
  virtual void Next() = 0;

  virtual std::string_view key() const = 0;
  virtual std::string_view value() const = 0;

  virtual Status status() const = 0;
};

}  // namespace minikv
