#include "minikv/util/env.h"

#include <algorithm>
#include <cstdint>
#include <system_error>
#include <vector>

#include "minikv/util/logging.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace minikv {
namespace {

#ifndef _WIN32
Status FsError(const std::string& what) {
  return Status::IOError(what + ": " + std::error_code(errno, std::system_category()).message());
}
#endif

}  // namespace

Status CreateDirIfMissing(const std::filesystem::path& dir) {
  std::error_code ec;
  if (std::filesystem::exists(dir, ec)) {
    if (ec) {
      return Status::IOError("exists " + dir.string() + ": " + ec.message());
    }
    if (std::filesystem::is_directory(dir, ec)) {
      return Status::Ok();
    }
    return Status::IOError(dir.string() + " exists but is not a directory");
  }

  std::filesystem::create_directories(dir, ec);
  if (ec) {
    return Status::IOError("create " + dir.string() + ": " + ec.message());
  }
  return Status::Ok();
}

Status RemoveFile(const std::filesystem::path& path) {
  std::error_code ec;
  // 删除不存在的文件视为成功：崩溃恢复时可能重复执行删除
  std::filesystem::remove(path, ec);
  if (ec) {
    return Status::IOError("delete " + path.string() + ": " + ec.message());
  }
  return Status::Ok();
}

Status RenameFile(const std::filesystem::path& from, const std::filesystem::path& to) {
#ifdef _WIN32
  // MOVEFILE_REPLACE_EXISTING 让重命名可覆盖目标，WRITE_THROUGH 走盘
  if (::MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
    return Status::IOError("rename " + from.string() + " -> " + to.string() + ": " +
                           std::error_code(static_cast<int>(::GetLastError()), std::system_category())
                               .message());
  }
  return Status::Ok();
#else
  std::error_code ec;
  std::filesystem::rename(from, to, ec);
  if (ec) {
    return Status::IOError("rename " + from.string() + " -> " + to.string() + ": " + ec.message());
  }
  return Status::Ok();
#endif
}

Status SyncDir(const std::filesystem::path& dir) {
#ifdef _WIN32
  // Windows/NTFS 不支持对目录句柄 FlushFileBuffers（通常返回 ACCESS_DENIED）。
  // 目录项的一致性由 RenameFile 的 MOVEFILE_WRITE_THROUGH 保证，因此这里
  // 尽力而为：打不开或不支持时只记 DEBUG，不让上层失败。
  HANDLE handle = ::CreateFileW(dir.c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    const DWORD open_error = ::GetLastError();
    if (open_error == ERROR_FILE_NOT_FOUND || open_error == ERROR_PATH_NOT_FOUND) {
      return Status::IOError("sync dir " + dir.string() + ": directory not found");
    }
    LOG_DEBUG("sync dir unsupported on this filesystem: " + dir.string());
    return Status::Ok();
  }

  const BOOL flushed = ::FlushFileBuffers(handle);
  const DWORD flush_error = ::GetLastError();
  ::CloseHandle(handle);

  if (flushed != 0) {
    return Status::Ok();
  }
  LOG_DEBUG("sync dir not supported on this filesystem: " + dir.string() + " (error " +
            std::to_string(flush_error) + ")");
  return Status::Ok();
#else
  const int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
  if (fd < 0) {
    return FsError("sync dir " + dir.string());
  }
  const int result = ::fsync(fd);
  const int saved_errno = errno;
  ::close(fd);
  if (result != 0) {
    if (saved_errno == EINVAL) {
      LOG_WARN("sync dir not supported on this filesystem: " + dir.string());
      return Status::Ok();
    }
    errno = saved_errno;
    return FsError("sync dir " + dir.string());
  }
  return Status::Ok();
#endif
}

bool FileExists(const std::filesystem::path& path) {
  std::error_code ec;
  const bool exists = std::filesystem::exists(path, ec);
  return exists && !ec;
}

Status FileSize(const std::filesystem::path& path, std::uint64_t* size) {
  if (size == nullptr) {
    return Status::InvalidArgument("file size: size is null");
  }
  std::error_code ec;
  const auto measured = std::filesystem::file_size(path, ec);
  if (ec) {
    return Status::IOError("file size " + path.string() + ": " + ec.message());
  }
  *size = static_cast<std::uint64_t>(measured);
  return Status::Ok();
}

Status ListFiles(const std::filesystem::path& dir, std::vector<std::string>* names) {
  if (names == nullptr) {
    return Status::InvalidArgument("list files: names is null");
  }
  names->clear();

  std::error_code ec;
  std::filesystem::directory_iterator iterator(dir, ec);
  if (ec) {
    return Status::IOError("list " + dir.string() + ": " + ec.message());
  }

  for (const auto& entry : iterator) {
    if (entry.is_regular_file(ec) && !ec) {
      names->push_back(entry.path().filename().string());
    }
  }
  std::sort(names->begin(), names->end());
  return Status::Ok();
}

}  // namespace minikv
