#include "minikv/table/merging_iterator.h"

#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

namespace minikv {

MergingIterator::MergingIterator(std::vector<std::unique_ptr<Iterator>> children)
    : children_(std::move(children)) {}

void MergingIterator::FindSmallest() {
  valid_ = false;

  std::size_t best = 0;
  for (std::size_t i = 0; i < children_.size(); ++i) {
    if (children_[i] == nullptr || !children_[i]->Valid()) {
      continue;
    }
    // 严格小于：key 相同时保留先出现的（下标更小 = 更新的来源）
    if (!valid_ || children_[i]->key() < children_[best]->key()) {
      best = i;
      valid_ = true;
    }
  }

  if (valid_) {
    current_ = best;
  }
}

void MergingIterator::SeekToFirst() {
  for (auto& child : children_) {
    if (child != nullptr) {
      child->SeekToFirst();
    }
  }
  FindSmallest();
}

void MergingIterator::Seek(std::string_view target) {
  for (auto& child : children_) {
    if (child != nullptr) {
      child->Seek(target);
    }
  }
  FindSmallest();
}

void MergingIterator::Next() {
  if (!valid_) {
    return;
  }
  children_[current_]->Next();
  FindSmallest();
}

std::string_view MergingIterator::key() const {
  if (!valid_) {
    return std::string_view();
  }
  return children_[current_]->key();
}

std::string_view MergingIterator::value() const {
  if (!valid_) {
    return std::string_view();
  }
  return children_[current_]->value();
}

bool MergingIterator::IsDeleted() const {
  if (!valid_) {
    return false;
  }
  return children_[current_]->IsDeleted();
}

Status MergingIterator::status() const {
  for (const auto& child : children_) {
    if (child == nullptr) {
      continue;
    }
    const Status child_status = child->status();
    if (!child_status.ok()) {
      return child_status;
    }
  }
  return Status::Ok();
}

}  // namespace minikv
