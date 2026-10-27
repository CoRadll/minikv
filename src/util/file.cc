#include "minikv/util/file.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <sys/types.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace minikv {
namespace {

// 单次系统调用的上限，避免超过 _read/_write 的参数宽度
constexpr std::size_t kMaxChunk = 1u << 30;

std::string ErrorMessage(int error_number) {
  return std::error_code(error_number, std::system_category()).message();
}

int OpenHandle(const std::filesystem::path& path, FileOpenMode mode) {
#ifdef _WIN32
  int flags = _O_BINARY;
  flags |= (mode == FileOpenMode::kReadOnly) ? _O_RDONLY : (_O_RDWR | _O_CREAT | _O_APPEND);
  // _wopen 已弃用，改用 _wsopen_s 共享打开（也支持 Unicode 路径）
  int fd = -1;
  const errno_t result =
      ::_wsopen_s(&fd, path.c_str(), flags, _SH_DENYNO, _S_IREAD | _S_IWRITE);
  if (result != 0) {
    errno = result;
    return -1;
  }
  return fd;
#else
  const int flags = (mode == FileOpenMode::kReadOnly) ? O_RDONLY : (O_RDWR | O_CREAT | O_APPEND);
  return ::open(path.c_str(), flags, 0644);
#endif
}

std::int64_t HandleSize(int fd) {
#ifdef _WIN32
  return ::_filelengthi64(fd);
#else
  struct stat info {};
  if (::fstat(fd, &info) != 0) {
    return -1;
  }
  return static_cast<std::int64_t>(info.st_size);
#endif
}

bool SeekTo(int fd, std::uint64_t offset) {
#ifdef _WIN32
  return ::_lseeki64(fd, static_cast<__int64>(offset), SEEK_SET) >= 0;
#else
  return ::lseek(fd, static_cast<off_t>(offset), SEEK_SET) >= 0;
#endif
}

std::int64_t ReadSome(int fd, char* buffer, std::size_t length) {
#ifdef _WIN32
  const auto chunk = static_cast<unsigned int>(std::min(length, kMaxChunk));
  return ::_read(fd, buffer, chunk);
#else
  return ::read(fd, buffer, length);
#endif
}

std::int64_t WriteSome(int fd, const char* data, std::size_t length) {
#ifdef _WIN32
  const auto chunk = static_cast<unsigned int>(std::min(length, kMaxChunk));
  return ::_write(fd, data, chunk);
#else
  return ::write(fd, data, length);
#endif
}

int SyncHandle(int fd) {
#ifdef _WIN32
  return ::_commit(fd);
#else
  return ::fsync(fd);
#endif
}

int TruncateHandle(int fd, std::uint64_t size) {
#ifdef _WIN32
  return ::_chsize_s(fd, static_cast<__int64>(size));
#else
  return ::ftruncate(fd, static_cast<off_t>(size));
#endif
}

void CloseHandle(int fd) {
#ifdef _WIN32
  ::_close(fd);
#else
  ::close(fd);
#endif
}

}  // namespace

File::~File() { Close(); }

File::File(File&& other) noexcept : fd_(other.fd_), size_(other.size_) {
  other.fd_ = -1;
  other.size_ = 0;
}

File& File::operator=(File&& other) noexcept {
  if (this != &other) {
    Close();
    fd_ = other.fd_;
    size_ = other.size_;
    other.fd_ = -1;
    other.size_ = 0;
  }
  return *this;
}

Status File::Open(const std::filesystem::path& path, FileOpenMode mode) {
  Close();

  const int fd = OpenHandle(path, mode);
  if (fd < 0) {
    return Status::IOError("open " + path.string() + ": " + ErrorMessage(errno));
  }

  const std::int64_t size = HandleSize(fd);
  if (size < 0) {
    const int saved_errno = errno;
    CloseHandle(fd);
    return Status::IOError("stat " + path.string() + ": " + ErrorMessage(saved_errno));
  }

  fd_ = fd;
  size_ = static_cast<std::uint64_t>(size);
  return Status::Ok();
}

Status File::Append(std::string_view data) {
  if (!is_open()) {
    return Status::Closed("append on closed file");
  }

  std::size_t written_total = 0;
  while (written_total < data.size()) {
    const std::size_t remaining = data.size() - written_total;
    const auto written = WriteSome(fd_, data.data() + written_total, remaining);
    if (written < 0) {
      return Status::IOError("write: " + ErrorMessage(errno));
    }
    if (written == 0) {
      return Status::IOError("write returned 0");
    }
    written_total += static_cast<std::size_t>(written);
  }

  // 以 O_APPEND 打开时，每次写都落在文件末尾
  size_ += static_cast<std::uint64_t>(data.size());
  return Status::Ok();
}

Status File::Read(std::uint64_t offset, std::size_t length, std::string* out) const {
  if (!is_open()) {
    return Status::Closed("read on closed file");
  }
  if (out == nullptr) {
    return Status::InvalidArgument("read: out is null");
  }

  out->clear();
  if (offset >= size_) {
    return Status::Ok();
  }

  if (!SeekTo(fd_, offset)) {
    return Status::IOError("seek: " + ErrorMessage(errno));
  }

  const auto available = static_cast<std::size_t>(size_ - offset);
  const std::size_t wanted = std::min(length, available);
  out->resize(wanted);

  std::size_t done = 0;
  while (done < wanted) {
    const auto got = ReadSome(fd_, out->data() + done, wanted - done);
    if (got < 0) {
      out->clear();
      return Status::IOError("read: " + ErrorMessage(errno));
    }
    if (got == 0) {
      break;  // 到达文件末尾（正常情况）
    }
    done += static_cast<std::size_t>(got);
  }

  out->resize(done);
  return Status::Ok();
}

Status File::Sync() {
  if (!is_open()) {
    return Status::Closed("sync on closed file");
  }
  if (SyncHandle(fd_) != 0) {
    return Status::IOError("sync: " + ErrorMessage(errno));
  }
  return Status::Ok();
}

Status File::Truncate(std::uint64_t size) {
  if (!is_open()) {
    return Status::Closed("truncate on closed file");
  }
  if (TruncateHandle(fd_, size) != 0) {
    return Status::IOError("truncate: " + ErrorMessage(errno));
  }
  size_ = size;
  return Status::Ok();
}

void File::Close() {
  if (fd_ >= 0) {
    CloseHandle(fd_);
    fd_ = -1;
  }
  size_ = 0;
}

}  // namespace minikv
