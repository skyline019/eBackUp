#include <gtest/gtest.h>

#include <filesystem>

#include "ebbackup/scan/scan_entry.h"
#include "test_util.h"

namespace ebbackup {
namespace {

#ifndef _WIN32
TEST(ScanErrorsTest, PermissionDeniedCollected) {
  const std::string source = test::TempDir("scan_perm_source");
  std::error_code ec;
  std::filesystem::create_directories(source + "/open", ec);
  std::filesystem::create_directories(source + "/secret", ec);
  test::WriteFile(source + "/open/file.txt", "ok");
  test::WriteFile(source + "/secret/hidden.txt", "hidden");
  std::filesystem::permissions(source + "/secret",
                               std::filesystem::perms::none, ec);
  if (ec) GTEST_SKIP() << "chmod not supported";

  ScanResult result;
  ASSERT_TRUE(ScanDirectory(source, &result).ok());
  bool saw_perm = false;
  for (const auto& issue : result.issues) {
    if (issue.reason == "permission_denied") saw_perm = true;
  }
  EXPECT_TRUE(saw_perm);
  bool saw_open = false;
  for (const auto& entry : result.entries) {
    if (entry.relative_path == "open/file.txt") saw_open = true;
  }
  EXPECT_TRUE(saw_open);
}
#endif

TEST(ScanErrorsTest, DepthLimitCollected) {
  const std::string source = test::TempDir("scan_depth_source");
  std::error_code ec;
  std::string path = source;
  for (int i = 0; i < 66; ++i) {
    path += "/a";
    std::filesystem::create_directories(path, ec);
    if (ec) GTEST_SKIP() << "cannot create deep tree: " << ec.message();
  }
  test::WriteFile(path + "/deep.txt", "deep");

  ScanResult result;
  ASSERT_TRUE(ScanDirectory(source, &result).ok());
  bool saw_depth = false;
  for (const auto& issue : result.issues) {
    if (issue.reason == "depth_exceeded") saw_depth = true;
  }
  EXPECT_TRUE(saw_depth);
}

#ifndef _WIN32
TEST(ScanErrorsTest, SymlinkLoopCollected) {
  const std::string source = test::TempDir("scan_symlink_loop");
  std::error_code ec;
  std::filesystem::create_directories(source + "/loop", ec);
  std::filesystem::create_symlink(source + "/loop", source + "/loop/link", ec);
  if (ec) GTEST_SKIP() << "symlink not supported";

  ScanResult result;
  ASSERT_TRUE(ScanDirectory(source, &result).ok());
  bool saw_loop = false;
  for (const auto& issue : result.issues) {
    if (issue.reason == "symlink_loop") saw_loop = true;
  }
  EXPECT_TRUE(saw_loop);
}
#endif

}  // namespace
}  // namespace ebbackup
