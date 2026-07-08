#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/common/path_encoding.h"
#include "test_util.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace ebbackup {
namespace {

#ifdef _WIN32
uint64_t ReadFileIndex(const std::string& path) {
  const std::wstring wide = Utf8ToWide(path);
  HANDLE h = CreateFileW(wide.c_str(), FILE_READ_ATTRIBUTES,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return 0;
  BY_HANDLE_FILE_INFORMATION info{};
  const BOOL ok = GetFileInformationByHandle(h, &info);
  CloseHandle(h);
  if (!ok) return 0;
  return (static_cast<uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
}

TEST(HardlinkBackupRestoreTest, RoundTripSameInode) {
  const std::string repo = test::TempDir("hardlink_repo");
  const std::string source = test::TempDir("hardlink_source");
  const std::string dest = test::TempDir("hardlink_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/primary.txt", "hardlink-payload");
  const std::string cmd =
      "cmd /c mklink /H \"" + source + "\\alias.txt\" \"" + source +
      "\\primary.txt\" >nul 2>&1";
  if (std::system(cmd.c_str()) != 0) {
    GTEST_SKIP() << "hard link creation not supported";
  }

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  ASSERT_TRUE(engine.Verify().ok());
  ASSERT_TRUE(engine.Restore(dest).ok());

  std::ifstream a(dest + "/primary.txt");
  std::ifstream b(dest + "/alias.txt");
  const std::string got_a((std::istreambuf_iterator<char>(a)),
                          std::istreambuf_iterator<char>());
  const std::string got_b((std::istreambuf_iterator<char>(b)),
                          std::istreambuf_iterator<char>());
  EXPECT_EQ(got_a, "hardlink-payload");
  EXPECT_EQ(got_b, "hardlink-payload");
  EXPECT_NE(ReadFileIndex(dest + "/primary.txt"), 0u);
  EXPECT_EQ(ReadFileIndex(dest + "/primary.txt"), ReadFileIndex(dest + "/alias.txt"));
}
#endif

}  // namespace
}  // namespace ebbackup
