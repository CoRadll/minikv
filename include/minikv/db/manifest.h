#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "minikv/util/status.h"

namespace minikv {

// TODO(S3): MANIFEST 记录「当前有哪些 SSTable、各属第几层、文件编号分配」。
// 先用单文件实现；若超过 500 行或出现多版本并发需求，再拆成 version + version_set。
// 草案：S3 实现时允许调整接口签名（AddFile 的参数形态很可能要改）。
class Manifest {
 public:
  struct FileMeta {
    int level = 0;
    uint64_t file_number = 0;
    std::string smallest;
    std::string largest;
  };

  static Status Load(const std::filesystem::path& path, Manifest* out);

  Status AddFile(int level, uint64_t file_number, std::string smallest, std::string largest);
  Status Save();

  const std::vector<FileMeta>& files() const { return files_; }
  uint64_t next_file_number() const { return next_file_number_; }

 private:
  std::filesystem::path path_;
  std::vector<FileMeta> files_;
  uint64_t next_file_number_ = 1;
};

}  // namespace minikv
