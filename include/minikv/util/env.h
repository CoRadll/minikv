#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "minikv/util/status.h"

namespace minikv {

// 文件系统级操作。与 File 的分工：
// File 管「一个文件的内容」，Env 管「目录结构的变化」。
// 崩溃一致性依赖后者的原子性（尤其是 RenameFile 与 SyncDir）。
Status CreateDirIfMissing(const std::filesystem::path& dir);
Status DeleteFile(const std::filesystem::path& path);
Status RenameFile(const std::filesystem::path& from, const std::filesystem::path& to);
Status SyncDir(const std::filesystem::path& dir);
bool FileExists(const std::filesystem::path& path);
Status FileSize(const std::filesystem::path& path, uint64_t* size);
Status ListFiles(const std::filesystem::path& dir, std::vector<std::string>* names);

}  // namespace minikv
