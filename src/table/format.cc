#include "minikv/table/format.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "minikv/util/coding.h"

namespace minikv {

std::string EncodeFooter(const TableFooter& footer) {
  std::string out;
  out.reserve(kTableFooterSize);
  out.append(kTableMagic.data(), kTableMagic.size());
  PutFixed32(&out, kTableVersion);
  PutFixed32(&out, 0);  // reserved
  PutFixed64(&out, footer.index_offset);
  PutFixed64(&out, footer.index_size);
  PutFixed64(&out, footer.filter_offset);
  PutFixed64(&out, footer.filter_size);
  return out;
}

bool DecodeFooter(std::string_view tail, TableFooter* footer) {
  if (footer == nullptr || tail.size() < kTableFooterSize) {
    return false;
  }

  if (tail.substr(0, kTableMagic.size()) != kTableMagic) {
    return false;
  }

  std::string_view view = tail.substr(kTableMagic.size());
  std::uint32_t version = 0;
  std::uint32_t reserved = 0;
  if (!GetFixed32(&view, &version) || !GetFixed32(&view, &reserved)) {
    return false;
  }
  if (version != kTableVersion) {
    return false;
  }

  if (!GetFixed64(&view, &footer->index_offset) || !GetFixed64(&view, &footer->index_size) ||
      !GetFixed64(&view, &footer->filter_offset) || !GetFixed64(&view, &footer->filter_size)) {
    return false;
  }
  return true;
}

}  // namespace minikv
