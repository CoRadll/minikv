#pragma once

#include "minikv/util/status.h"

namespace minikv {

class Manifest;

// TODO(S4): 选取待合并文件、多路归并、原子安装新版本、删除旧文件、限速。
// 草案：S4 实现时接口大概率变化 —— Run() 需要 task / input_version /
//       output_limit / 限速参数，现在的无参签名只是占位。
class Compaction {
 public:
  explicit Compaction(Manifest* manifest);

  Status Run();

 private:
  Manifest* manifest_;
};

}  // namespace minikv
