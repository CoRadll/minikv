#include "minikv/db/manifest.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "minikv/util/env.h"
#include "minikv/util/file.h"

namespace minikv {
namespace {

constexpr const char* kManifestHeader = "MINIKV-MANIFEST v1";

std::string ToHex(std::string_view data) {
  static const char* kDigits = "0123456789abcdef";
  std::string out;
  out.reserve(data.size() * 2);
  for (const char ch : data) {
    const auto byte = static_cast<unsigned char>(ch);
    out.push_back(kDigits[byte >> 4]);
    out.push_back(kDigits[byte & 0x0Fu]);
  }
  return out;
}

bool FromHex(std::string_view hex, std::string* out) {
  if (hex.size() % 2 != 0) {
    return false;
  }
  auto digit = [](char c) -> int {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
    }
    return -1;
  };

  out->clear();
  out->reserve(hex.size() / 2);
  for (std::size_t i = 0; i < hex.size(); i += 2) {
    const int high = digit(hex[i]);
    const int low = digit(hex[i + 1]);
    if (high < 0 || low < 0) {
      return false;
    }
    out->push_back(static_cast<char>((high << 4) | low));
  }
  return true;
}

}  // namespace

Status Manifest::Load(const std::filesystem::path& path, Manifest* out) {
  if (out == nullptr) {
    return Status::InvalidArgument("manifest out pointer is null");
  }

  out->path_ = path;
  out->files_.clear();
  out->next_file_number_ = 1;

  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    return Status::Ok();  // 空库：没有任何文件
  }

  File file;
  Status status = file.Open(path, FileOpenMode::kReadOnly);
  if (!status.ok()) {
    return status;
  }

  std::string content;
  status = file.Read(0, static_cast<std::size_t>(file.Size()), &content);
  if (!status.ok()) {
    return status;
  }

  std::istringstream stream(content);
  std::string line;
  if (!std::getline(stream, line) || line != kManifestHeader) {
    return Status::Corruption("bad manifest header: " + path.string());
  }

  std::uint64_t max_file_number = 0;
  while (std::getline(stream, line)) {
    if (line.empty()) {
      continue;
    }
    std::istringstream row(line);
    FileMeta meta;
    std::string smallest_hex;
    std::string largest_hex;
    if (!(row >> meta.level >> meta.file_number >> smallest_hex >> largest_hex)) {
      return Status::Corruption("bad manifest row: " + line);
    }
    if (!FromHex(smallest_hex, &meta.smallest) || !FromHex(largest_hex, &meta.largest)) {
      return Status::Corruption("bad manifest key encoding: " + line);
    }
    out->files_.push_back(std::move(meta));
    if (out->files_.back().file_number > max_file_number) {
      max_file_number = out->files_.back().file_number;
    }
  }

  out->next_file_number_ = max_file_number + 1;
  return Status::Ok();
}

Status Manifest::AddFile(int level, std::uint64_t file_number, std::string smallest,
                         std::string largest) {
  FileMeta meta;
  meta.level = level;
  meta.file_number = file_number;
  meta.smallest = std::move(smallest);
  meta.largest = std::move(largest);
  files_.push_back(std::move(meta));

  if (file_number + 1 > next_file_number_) {
    next_file_number_ = file_number + 1;
  }
  return Status::Ok();
}

Status Manifest::Save() {
  std::string content = std::string(kManifestHeader) + "\n";
  for (const FileMeta& meta : files_) {
    content += std::to_string(meta.level) + " " + std::to_string(meta.file_number) + " " +
               ToHex(meta.smallest) + " " + ToHex(meta.largest) + "\n";
  }

  // 先写临时文件并落盘，再原子重命名，避免崩溃后留下半个 MANIFEST
  const std::filesystem::path tmp_path = path_.string() + ".tmp";
  {
    File file;
    Status status = file.Open(tmp_path, FileOpenMode::kReadWrite);
    if (!status.ok()) {
      return status;
    }
    status = file.Truncate(0);
    if (status.ok()) {
      status = file.Append(content);
    }
    if (status.ok()) {
      status = file.Sync();
    }
    file.Close();
    if (!status.ok()) {
      return status;
    }
  }

  Status status = RenameFile(tmp_path, path_);
  if (!status.ok()) {
    return status;
  }
  return SyncDir(path_.parent_path());
}

}  // namespace minikv
