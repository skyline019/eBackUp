#include <gtest/gtest.h>

#include "ebbackup/common/path_util.h"

namespace ebbackup {
namespace {

TEST(PathNormalizeTest, BackslashToSlash) {
  EXPECT_EQ(NormalizeRepoPath("folder\\file.txt"), "folder/file.txt");
}

TEST(PathNormalizeTest, RelativePathFromRoot) {
  std::string rel;
  const Status st =
      RelativePathFromRoot("C:/root", "C:/root/sub/file.txt", &rel);
  ASSERT_TRUE(st.ok());
  EXPECT_EQ(rel, "sub/file.txt");
}

}  // namespace
}  // namespace ebbackup
