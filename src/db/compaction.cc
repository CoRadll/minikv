#include "minikv/db/compaction.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "minikv/api/iterator.h"
#include "minikv/table/merging_iterator.h"
#include "minikv/table/table_builder.h"
#include "minikv/table/table_reader.h"

namespace minikv {

Status Compaction::Run(const std::vector<const TableReader*>& inputs,
                       std::uint64_t output_number, const std::filesystem::path& output_path,
                       CompactionResult* result) {
  if (result == nullptr) {
    return Status::InvalidArgument("compaction result is null");
  }
  *result = CompactionResult();

  if (inputs.empty()) {
    return Status::Ok();
  }

  // 1) 每个输入文件一个迭代器，顺序即优先级（新 → 旧）
  std::vector<std::unique_ptr<Iterator>> children;
  children.reserve(inputs.size());
  for (const TableReader* reader : inputs) {
    if (reader == nullptr) {
      return Status::InvalidArgument("compaction input is null");
    }
    children.push_back(reader->NewIterator());
  }

  MergingIterator merged(std::move(children));
  merged.SeekToFirst();

  // 2) 归并写出：同 key 只取第一条（最新），墓碑直接丢（最底层合并）
  TableBuilder builder(output_path);
  std::string last_key;
  bool has_last = false;
  std::uint64_t entries = 0;

  for (; merged.Valid(); merged.Next()) {
    const Status status = merged.status();
    if (!status.ok()) {
      return status;
    }

    std::string key(merged.key());
    if (has_last && key == last_key) {
      continue;  // 旧版本
    }
    last_key = std::move(key);
    has_last = true;

    if (merged.IsDeleted()) {
      continue;  // 墓碑：两层全量合并后它就是最底层，无需保留
    }

    const Status add_status = builder.Add(last_key, merged.value(), false);
    if (!add_status.ok()) {
      return add_status;
    }
    ++entries;
  }

  // 3) 全是墓碑/空输入：不产出文件（builder 未 Add，文件根本没创建）
  if (entries == 0) {
    result->has_output = false;
    return Status::Ok();
  }

  const Status finish_status = builder.Finish();
  if (!finish_status.ok()) {
    return finish_status;
  }

  result->has_output = true;
  result->file_number = output_number;
  result->entries = entries;
  result->smallest_key = builder.smallest_key();
  result->largest_key = builder.largest_key();
  return Status::Ok();
}

}  // namespace minikv
