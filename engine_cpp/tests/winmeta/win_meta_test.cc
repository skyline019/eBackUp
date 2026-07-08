#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "ebbackup/scan/scan_entry.h"
#include "ebbackup/winmeta/win_meta.h"
#include "test_util.h"

namespace ebbackup {
namespace winmeta {
namespace {

#ifdef _WIN32
TEST(WinMetaTest, CaptureSecurityDescriptorAndInode) {
  const std::string dir = test::TempDir("win_meta_cap");
  const std::string file = dir + "/secured.txt";
  test::WriteFile(file, "win-meta-test");

  ScanEntry entry{};
  entry.absolute_path = file;
  entry.relative_path = "secured.txt";
  entry.type = FileType::kRegular;
  entry.size = 12;

  ASSERT_TRUE(CaptureWinMetaFromPath(file, &entry).ok());
  EXPECT_FALSE(entry.security_descriptor_b64.empty());
  EXPECT_NE(entry.inode_id, 0u);

  WinMetaExtension win{};
  ASSERT_TRUE(ReadWinMetaFromEntry(entry.ToManifestSkeleton(), &win).ok());
  EXPECT_EQ(win.security_descriptor_b64, entry.security_descriptor_b64);
  EXPECT_EQ(win.inode_id, entry.inode_id);
}
#endif

}  // namespace
}  // namespace winmeta
}  // namespace ebbackup
