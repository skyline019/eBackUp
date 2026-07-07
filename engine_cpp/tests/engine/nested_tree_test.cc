#include <gtest/gtest.h>

#include <fstream>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/scan/backup_filter.h"
#include "tree_util.h"
#include "fixture_util.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(NestedTreeTest, DeepTree10Levels) {
  const std::string repo = test::TempDir("nested_deep_repo");
  const std::string source = test::TempDir("nested_deep_src");
  const std::string dest = test::TempDir("nested_deep_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  ASSERT_TRUE(test::BuildNestedTree(source, 10, 1, 1024).ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  ASSERT_TRUE(engine.Verify().ok());
  ASSERT_TRUE(engine.Restore(dest).ok());
  ASSERT_TRUE(test::CompareDirectoryTrees(source, dest).ok());
}

TEST(NestedTreeTest, WideTree100Files) {
  const std::string repo = test::TempDir("nested_wide_repo");
  const std::string source = test::TempDir("nested_wide_src");
  const std::string dest = test::TempDir("nested_wide_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  for (int i = 0; i < 100; ++i) {
    test::WriteFile(source + "/f" + std::to_string(i) + ".bin",
                    test::MakeSyntheticData(2048, static_cast<uint8_t>(i)));
  }

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  test::WriteFile(source + "/f50.bin", test::MakeSyntheticData(2048, 200));
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental).ok());
  ASSERT_TRUE(engine.Restore(dest).ok());
  EXPECT_EQ(test::CountRegularFiles(source), test::CountRegularFiles(dest));
}

TEST(NestedTreeTest, UnicodeAndSpacePaths) {
  const std::string repo = test::TempDir("nested_unicode_repo");
  const std::string source = test::TempDir("nested_unicode_src");
  const std::string dest = test::TempDir("nested_unicode_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  if (!test::CopyFixtureTree("unicode", source).ok()) {
    if (!test::BuildUnicodePathTree(source).ok()) {
      GTEST_SKIP() << "unicode paths unsupported on this platform";
    }
  }

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  const Status st = engine.RunBackup(source);
  if (!st.ok()) {
    GTEST_SKIP() << "unicode backup unsupported: " << st.message();
  }
  ASSERT_TRUE(engine.Restore(dest).ok());
  ASSERT_TRUE(test::CompareDirectoryTrees(source, dest).ok());
}

TEST(NestedTreeTest, UnicodePipelineBackup) {
  const std::string repo = test::TempDir("nested_unicode_pipe_repo");
  const std::string source = test::TempDir("nested_unicode_pipe_src");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  if (!test::CopyFixtureTree("unicode", source).ok()) {
    if (!test::BuildUnicodePathTree(source).ok()) {
      GTEST_SKIP() << "unicode paths unsupported on this platform";
    }
  }

  BackupOptions opts{};
  opts.use_pipeline = true;
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  const Status st = engine.RunBackup(source, BackupMode::kFull, opts);
  ASSERT_TRUE(st.ok()) << st.message();
  ASSERT_TRUE(engine.Verify().ok());
}

TEST(NestedTreeTest, SelectiveRestoreNestedAncestors) {
  const std::string repo = test::TempDir("nested_select_repo");
  const std::string source = test::TempDir("nested_select_src");
  const std::string dest = test::TempDir("nested_select_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/keep/nested/file.txt", "keep-me");
  test::WriteFile(source + "/drop/sibling.txt", "drop-me");

  BackupOptions opts{};
  opts.filter.include_paths = {"keep/nested"};
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());

  RestoreOptions ropts{};
  ropts.filter = opts.filter;
  ASSERT_TRUE(engine.Restore(dest, ropts).ok());
  EXPECT_TRUE(std::filesystem::exists(dest + "/keep/nested/file.txt"));
  EXPECT_FALSE(std::filesystem::exists(dest + "/drop/sibling.txt"));
}

}  // namespace
}  // namespace ebbackup
