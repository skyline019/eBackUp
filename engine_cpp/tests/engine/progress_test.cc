#include <gtest/gtest.h>

#include <vector>

#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(ProgressTest, BackupEmitsIncreasingProgress) {
  const std::string repo = test::TempDir("progress_repo");
  const std::string source = test::TempDir("progress_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/file.bin", test::MakeSyntheticData(512 * 1024, 1));

  std::vector<uint64_t> samples;
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  engine.SetProgressCallback(
      [&samples](uint64_t pct, void*) { samples.push_back(pct); }, nullptr);
  ASSERT_TRUE(engine.RunBackup(source).ok());
  ASSERT_FALSE(samples.empty());
  EXPECT_GE(samples.back(), 1000u);
  for (size_t i = 1; i < samples.size(); ++i) {
    EXPECT_GE(samples[i], samples[i - 1]);
  }
}

}  // namespace
}  // namespace ebbackup
