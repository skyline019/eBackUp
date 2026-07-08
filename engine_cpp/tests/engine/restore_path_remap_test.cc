#include <gtest/gtest.h>

#include "ebbackup/engine/restore_path_remap.h"

namespace ebbackup {
namespace {

TEST(RestorePathRemapTest, KeepMode) {
  RestorePathRemap remap{};
  ResolvedDestPath out{};
  ASSERT_TRUE(ResolveDestRelativePath("keep/nested/file.txt", FileType::kRegular,
                                      remap, &out)
                  .ok());
  EXPECT_EQ(out.path, "keep/nested/file.txt");
  EXPECT_FALSE(out.skip);
}

TEST(RestorePathRemapTest, StripPrefix) {
  RestorePathRemap remap{};
  remap.mode = RestoreLayoutMode::kStripPrefix;
  remap.strip_prefix = "keep";
  ResolvedDestPath out{};
  ASSERT_TRUE(ResolveDestRelativePath("keep/nested/file.txt", FileType::kRegular,
                                      remap, &out)
                  .ok());
  EXPECT_EQ(out.path, "nested/file.txt");
}

TEST(RestorePathRemapTest, StripPrefixSkipsRootDir) {
  RestorePathRemap remap{};
  remap.mode = RestoreLayoutMode::kStripPrefix;
  remap.strip_prefix = "keep";
  ResolvedDestPath out{};
  ASSERT_TRUE(
      ResolveDestRelativePath("keep", FileType::kDirectory, remap, &out).ok());
  EXPECT_TRUE(out.skip);
}

TEST(RestorePathRemapTest, FlattenBasename) {
  RestorePathRemap remap{};
  remap.mode = RestoreLayoutMode::kFlatten;
  ResolvedDestPath out{};
  ASSERT_TRUE(ResolveDestRelativePath("a/b/c.txt", FileType::kRegular, remap, &out)
                  .ok());
  EXPECT_EQ(out.path, "c.txt");
}

TEST(RestorePathRemapTest, FlattenSkipsDirectory) {
  RestorePathRemap remap{};
  remap.mode = RestoreLayoutMode::kFlatten;
  ResolvedDestPath out{};
  ASSERT_TRUE(
      ResolveDestRelativePath("a/b", FileType::kDirectory, remap, &out).ok());
  EXPECT_TRUE(out.skip);
}

TEST(RestorePathRemapTest, RemapPrefix) {
  RestorePathRemap remap{};
  remap.mode = RestoreLayoutMode::kRemapPrefix;
  remap.map_from = "old";
  remap.map_to = "new/root";
  ResolvedDestPath out{};
  ASSERT_TRUE(ResolveDestRelativePath("old/sub/file.txt", FileType::kRegular,
                                      remap, &out)
                  .ok());
  EXPECT_EQ(out.path, "new/root/sub/file.txt");
}

TEST(RestorePathRemapTest, ConflictFail) {
  std::unordered_map<std::string, uint32_t> seen;
  std::string assigned;
  ASSERT_TRUE(
      AssignDestPathWithConflict("a.txt", RestoreConflictPolicy::kFail, &seen,
                                 &assigned)
          .ok());
  EXPECT_EQ(assigned, "a.txt");
  EXPECT_FALSE(AssignDestPathWithConflict("a.txt", RestoreConflictPolicy::kFail,
                                          &seen, &assigned)
                   .ok());
}

TEST(RestorePathRemapTest, ConflictSuffix) {
  std::unordered_map<std::string, uint32_t> seen;
  std::string assigned;
  ASSERT_TRUE(
      AssignDestPathWithConflict("a.txt", RestoreConflictPolicy::kSuffix, &seen,
                                 &assigned)
          .ok());
  EXPECT_EQ(assigned, "a.txt");
  ASSERT_TRUE(
      AssignDestPathWithConflict("a.txt", RestoreConflictPolicy::kSuffix, &seen,
                                 &assigned)
          .ok());
  EXPECT_EQ(assigned, "a_1.txt");
}

TEST(RestorePathRemapTest, CollapseIncludePaths) {
  const auto collapsed = CollapseIncludePaths(
      {"keep/nested/file.txt", "keep", "drop/other.txt", "keep/nested"});
  ASSERT_EQ(collapsed.size(), 2u);
  EXPECT_EQ(collapsed[0], "keep");
  EXPECT_EQ(collapsed[1], "drop/other.txt");
}

}  // namespace
}  // namespace ebbackup
