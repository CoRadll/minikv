#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

#include "minikv/util/status.h"

namespace minikv {

enum class FileOpenMode {
  kReadOnly,   // 只读打开，文件必须已存在
  kReadWrite,  // 读写打开，不存在则创建
};

// RAII 文件句柄，封装平台差异（Windows 用 _open/_write/_commit，
// POSIX 用 open/write/fsync）。不保证线程安全，调用方负责串行化。
class File {
 public:
  File() = default;
  ~File();

  File(File&& other) noexcept;
  File& operator=(File&& other) noexcept;

  File(const File&) = delete;
  File& operator=(const File&) = delete;

  Status Open(const std::filesystem::path& path, FileOpenMode mode);
  Status Append(std::string_view data);
  Status Read(uint64_t offset, std::size_t length, std::string* out) const;
  Status Sync();
  Status Truncate(uint64_t size);
  void Close();

  bool is_open() const { return fd_ >= 0; }
  uint64_t Size() const { return size_; }

 private:
  int fd_ = -1;
  uint64_t size_ = 0;
};

}  // namespace minikv
