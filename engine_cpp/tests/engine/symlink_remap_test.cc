#include <gtest/gtest.h>

#include "ebbackup/engine/restore_path_remap.h"

namespace ebbackup {
namespace {

TEST(SymlinkRemapTest, ApplyPrefixRemap) {
  SymlinkRemap remap{};
  remap.map_from = "C:/old/project";
  remap.map_to = "D:/new/project";
  EXPECT_EQ(ApplySymlinkTargetRemap("C:/old/project/sub/link", remap),
            "D:/new/project/sub/link");
  EXPECT_EQ(ApplySymlinkTargetRemap("C:/other/path", remap), "C:/other/path");
}

TEST(SymlinkRemapTest, ApplyBackslashPaths) {
  SymlinkRemap remap{};
  remap.map_from = "C:\\old";
  remap.map_to = "E:\\new";
  EXPECT_EQ(ApplySymlinkTargetRemap("C:\\old\\file.txt", remap), "E:/new/file.txt");
}

}  // namespace
}  // namespace ebbackup
