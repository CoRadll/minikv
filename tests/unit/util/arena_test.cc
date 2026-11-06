#include <cstddef>
#include <cstdint>

#include "minikv/util/arena.h"

#include "support/test_framework.h"

TEST(Arena, AllocationIsAlignedAndDistinct) {
  minikv::Arena arena;

  char* first = arena.Allocate(17);
  char* second = arena.Allocate(33);
  REQUIRE_TRUE(first != nullptr);
  REQUIRE_TRUE(second != nullptr);
  CHECK_TRUE(first != second);
  CHECK_EQ(reinterpret_cast<std::uintptr_t>(second) % alignof(std::max_align_t),
           static_cast<std::uintptr_t>(0));
}

TEST(Arena, AlignedAllocationHonorsAlignment) {
  minikv::Arena arena;

  char* byte = arena.Allocate(1);
  REQUIRE_TRUE(byte != nullptr);

  char* aligned = arena.AllocateAligned(8, 64);
  REQUIRE_TRUE(aligned != nullptr);
  CHECK_EQ(reinterpret_cast<std::uintptr_t>(aligned) % 64, static_cast<std::uintptr_t>(0));
}

TEST(Arena, MemoryUsageGrowsAndResetClears) {
  minikv::Arena arena;
  CHECK_EQ(arena.MemoryUsage(), static_cast<std::size_t>(0));

  arena.Allocate(100);
  const std::size_t after_first = arena.MemoryUsage();
  CHECK_TRUE(after_first >= 100);

  arena.Allocate(100);
  CHECK_TRUE(arena.MemoryUsage() >= after_first);

  arena.Reset();
  CHECK_EQ(arena.MemoryUsage(), static_cast<std::size_t>(0));
}

TEST(Arena, SupportsLargeAllocation) {
  minikv::Arena arena;
  char* big = arena.Allocate(64 * 1024);
  REQUIRE_TRUE(big != nullptr);
  big[0] = 'x';
  big[64 * 1024 - 1] = 'y';
  CHECK_EQ(big[0], 'x');
  CHECK_EQ(big[64 * 1024 - 1], 'y');
}
