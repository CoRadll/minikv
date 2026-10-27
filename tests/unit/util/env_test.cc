#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "minikv/util/env.h"
#include "minikv/util/file.h"

#include "support/temp_dir.h"
#include "support/test_framework.h"

namespace {

void WriteFile(const std::filesystem::path& path, const std::string& content) {
  minikv::File file;
  REQUIRE_OK(file.Open(path, minikv::FileOpenMode::kReadWrite));
  REQUIRE_OK(file.Append(content));
}

std::string ReadFile(const std::filesystem::path& path) {
  minikv::File file;
  std::string out;
  // 注意：REQUIRE_* 会展开成 return;，只能用在 void 函数里
  if (!file.Open(path, minikv::FileOpenMode::kReadOnly).ok()) {
    return {};
  }
  if (!file.Read(0, 1024, &out).ok()) {
    return {};
  }
  return out;
}

}  // namespace

TEST(Env, CreateNestedDirectories) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  const auto nested = dir / "a" / "b" / "c";
  CHECK_OK(minikv::CreateDirIfMissing(nested));
  CHECK_TRUE(std::filesystem::is_directory(nested));

  // 已存在时应视为成功
  CHECK_OK(minikv::CreateDirIfMissing(nested));
}

TEST(Env, CreateDirOnExistingFileFails) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  const auto file_path = dir / "not_a_dir";
  WriteFile(file_path, "x");

  const minikv::Status status = minikv::CreateDirIfMissing(file_path);
  CHECK_FALSE(status.ok());
  CHECK_EQ(status.code(), minikv::StatusCode::kIOError);
}

TEST(Env, ListFilesReturnsSortedNames) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  WriteFile(dir / "b.txt", "b");
  WriteFile(dir / "a.txt", "a");
  CHECK_OK(minikv::CreateDirIfMissing(dir / "subdir"));

  std::vector<std::string> names;
  CHECK_OK(minikv::ListFiles(dir.path(), &names));

  CHECK_EQ(names.size(), static_cast<std::size_t>(2));
  CHECK_STREQ(names[0], "a.txt");
  CHECK_STREQ(names[1], "b.txt");
}

TEST(Env, RenameOverwritesTarget) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  const auto from = dir / "from.txt";
  const auto to = dir / "to.txt";
  WriteFile(from, "new");
  WriteFile(to, "old");

  CHECK_OK(minikv::RenameFile(from, to));
  CHECK_FALSE(minikv::FileExists(from));
  CHECK_TRUE(minikv::FileExists(to));
  CHECK_STREQ(ReadFile(to), "new");
}

TEST(Env, DeleteMissingFileIsOk) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  CHECK_OK(minikv::RemoveFile(dir / "never_existed"));
}

TEST(Env, DeleteExistingFile) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  const auto path = dir / "delete_me.txt";
  WriteFile(path, "x");
  CHECK_TRUE(minikv::FileExists(path));

  CHECK_OK(minikv::RemoveFile(path));
  CHECK_FALSE(minikv::FileExists(path));
}

TEST(Env, FileSizeMatchesContent) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());

  const auto path = dir / "sized.txt";
  WriteFile(path, "12345");

  std::uint64_t size = 0;
  CHECK_OK(minikv::FileSize(path, &size));
  CHECK_EQ(size, static_cast<std::uint64_t>(5));

  CHECK_FALSE(minikv::FileSize(dir / "missing.txt", &size).ok());
}

TEST(Env, SyncDirSucceeds) {
  minikv::test::TempDir dir;
  REQUIRE_TRUE(dir.ok());
  CHECK_OK(minikv::SyncDir(dir.path()));
}
