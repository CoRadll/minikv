#include <cstddef>
#include <string>
#include <thread>

#include "minikv/memtable/memtable.h"

#include "support/test_framework.h"

TEST(MemTable, PutThenGet) {
  minikv::MemTable table;
  table.Put("user:1", "zhangsan");

  std::string value;
  CHECK_OK(table.Get("user:1", &value));
  CHECK_STREQ(value, "zhangsan");
  CHECK_FALSE(table.Empty());
  CHECK_EQ(table.EntryCount(), static_cast<std::size_t>(1));
}

TEST(MemTable, OverwriteReturnsLatestValue) {
  minikv::MemTable table;
  table.Put("k", "v1");
  table.Put("k", "v2");

  std::string value;
  CHECK_OK(table.Get("k", &value));
  CHECK_STREQ(value, "v2");
  CHECK_EQ(table.EntryCount(), static_cast<std::size_t>(1));
}

TEST(MemTable, DeleteHidesValue) {
  minikv::MemTable table;
  table.Put("k", "v");
  table.Delete("k");

  std::string value;
  const minikv::Status status = table.Get("k", &value);
  CHECK_FALSE(status.ok());
  CHECK_TRUE(status.IsNotFound());
}

TEST(MemTable, PutAfterDeleteBringsValueBack) {
  minikv::MemTable table;
  table.Put("k", "v1");
  table.Delete("k");
  table.Put("k", "v2");

  std::string value;
  CHECK_OK(table.Get("k", &value));
  CHECK_STREQ(value, "v2");
}

TEST(MemTable, DeleteMissingKeyStillWritesTombstone) {
  minikv::MemTable table;
  table.Delete("ghost");

  std::string value;
  CHECK_TRUE(table.Get("ghost", &value).IsNotFound());
  // 墓碑要保留，否则重启后旧文件里的值会“复活”
  CHECK_EQ(table.EntryCount(), static_cast<std::size_t>(1));
}

TEST(MemTable, GetMissingKeyIsNotFound) {
  minikv::MemTable table;
  std::string value;
  CHECK_TRUE(table.Get("nope", &value).IsNotFound());
}

TEST(MemTable, EmptyTableReportsEmpty) {
  minikv::MemTable table;
  CHECK_TRUE(table.Empty());
  CHECK_EQ(table.EntryCount(), static_cast<std::size_t>(0));
  CHECK_EQ(table.ApproximateSize(), static_cast<std::size_t>(0));
}

TEST(MemTable, ApproximateSizeGrowsAndShrinks) {
  minikv::MemTable table;
  CHECK_EQ(table.ApproximateSize(), static_cast<std::size_t>(0));

  table.Put("key", "12345");
  const std::size_t after_put = table.ApproximateSize();
  CHECK_TRUE(after_put > 0);

  // 覆盖成更短的值，体积应下降
  table.Put("key", "1");
  CHECK_TRUE(table.ApproximateSize() < after_put);

  const std::size_t before_delete = table.ApproximateSize();
  table.Delete("key");
  CHECK_TRUE(table.ApproximateSize() < before_delete);
}

TEST(MemTable, HandlesBinaryKeys) {
  minikv::MemTable table;
  const std::string key("\0\1\2", 3);
  const std::string value("\xFF\x00", 2);

  table.Put(key, value);

  std::string out;
  CHECK_OK(table.Get(key, &out));
  CHECK_TRUE(out == value);
}

TEST(MemTable, ConcurrentReadsDuringWrites) {
  minikv::MemTable table;
  constexpr int kCount = 500;

  std::thread writer([&table]() {
    for (int i = 0; i < kCount; ++i) {
      table.Put("k" + std::to_string(i), "v" + std::to_string(i));
    }
  });

  std::thread reader([&table]() {
    for (int i = 0; i < kCount; ++i) {
      std::string value;
      table.Get("k" + std::to_string(i), &value);
    }
  });

  writer.join();
  reader.join();

  CHECK_EQ(table.EntryCount(), static_cast<std::size_t>(kCount));
  std::string value;
  CHECK_OK(table.Get("k499", &value));
  CHECK_STREQ(value, "v499");
}
