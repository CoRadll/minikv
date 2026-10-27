#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

#include "minikv/util/file.h"

#include "support/temp_dir.h"
#include "support/test_framework.h"

TEST(File, AppendAndRead) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  minikv::File file;
  CHECK_OK(file.Open(dir / "data.bin", minikv::FileOpenMode::kReadWrite));
  CHECK_TRUE(file.is_open());
  CHECK_EQ(file.Size(), static_cast<std::uint64_t>(0));

  CHECK_OK(file.Append("hello"));
  CHECK_OK(file.Append(" world"));
  CHECK_EQ(file.Size(), static_cast<std::uint64_t>(11));
  CHECK_OK(file.Sync());

  std::string out;
  CHECK_OK(file.Read(0, 5, &out));
  CHECK_STREQ(out, "hello");

  CHECK_OK(file.Read(6, 5, &out));
  CHECK_STREQ(out, "world");

  CHECK_OK(file.Read(0, 100, &out));
  CHECK_STREQ(out, "hello world");

  CHECK_OK(file.Read(100, 10, &out));
  CHECK_TRUE(out.empty());
}

TEST(File, ReopenReadOnly) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  {
    minikv::File writer;
    REQUIRE_OK(writer.Open(dir / "data.bin", minikv::FileOpenMode::kReadWrite));
    REQUIRE_OK(writer.Append("abc"));
  }

  minikv::File reader;
  CHECK_OK(reader.Open(dir / "data.bin", minikv::FileOpenMode::kReadOnly));
  CHECK_EQ(reader.Size(), static_cast<std::uint64_t>(3));

  std::string out;
  CHECK_OK(reader.Read(1, 1, &out));
  CHECK_STREQ(out, "b");

  // 只读打开后写入应失败
  CHECK_FALSE(reader.Append("x").ok());
}

TEST(File, OpenMissingReadOnlyFails) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  minikv::File file;
  const minikv::Status status = file.Open(dir / "missing.bin", minikv::FileOpenMode::kReadOnly);
  CHECK_FALSE(status.ok());
  CHECK_EQ(status.code(), minikv::StatusCode::kIOError);
  CHECK_FALSE(file.is_open());
}

TEST(File, TruncateShrinksFile) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  minikv::File file;
  REQUIRE_OK(file.Open(dir / "data.bin", minikv::FileOpenMode::kReadWrite));
  REQUIRE_OK(file.Append("hello world"));
  REQUIRE_OK(file.Truncate(5));

  CHECK_EQ(file.Size(), static_cast<std::uint64_t>(5));

  std::string out;
  CHECK_OK(file.Read(0, 100, &out));
  CHECK_STREQ(out, "hello");

  file.Close();
  minikv::File reopened;
  CHECK_OK(reopened.Open(dir / "data.bin", minikv::FileOpenMode::kReadOnly));
  CHECK_EQ(reopened.Size(), static_cast<std::uint64_t>(5));
}

TEST(File, MoveTransfersOwnership) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  minikv::File first;
  REQUIRE_OK(first.Open(dir / "move.bin", minikv::FileOpenMode::kReadWrite));
  REQUIRE_OK(first.Append("xyz"));

  minikv::File second(std::move(first));
  CHECK_FALSE(first.is_open());
  CHECK_TRUE(second.is_open());
  CHECK_EQ(second.Size(), static_cast<std::uint64_t>(3));

  std::string out;
  CHECK_OK(second.Read(0, 3, &out));
  CHECK_STREQ(out, "xyz");
}

TEST(File, OperationsOnClosedFileFail) {
  minikv::File file;
  std::string out;

  CHECK_EQ(file.Append("x").code(), minikv::StatusCode::kClosed);
  CHECK_EQ(file.Read(0, 1, &out).code(), minikv::StatusCode::kClosed);
  CHECK_EQ(file.Sync().code(), minikv::StatusCode::kClosed);
  CHECK_EQ(file.Truncate(0).code(), minikv::StatusCode::kClosed);
}

TEST(File, DoubleCloseIsSafe) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  minikv::File file;
  REQUIRE_OK(file.Open(dir / "close.bin", minikv::FileOpenMode::kReadWrite));
  file.Close();
  file.Close();
  CHECK_FALSE(file.is_open());
}
