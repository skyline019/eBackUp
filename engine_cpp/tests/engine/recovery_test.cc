#include <gtest/gtest.h>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/state/superblock.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(RecoveryTest, RecoverAfterInterruptedBackup) {
  const std::string repo = test::TempDir("backup_recover_repo");
  const std::string source = test::TempDir("backup_recover_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(1024 * 1024, 3));

  {
    BackupEngine engine(repo);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source).ok());
    ASSERT_TRUE(engine.Verify().ok());
  }

  {
    BackupSuperBlockStore sb_store(repo + "/superblock.bin");
    BackupSuperBlock sb{};
    ASSERT_TRUE(sb_store.Load(&sb).ok());
    SetPhase(&sb, BackupPhase::kStoring);
    sb.critical.chunks_written = 7;
    ASSERT_TRUE(sb_store.Commit(sb).ok());
  }

  {
    BackupEngine engine(repo);
    ASSERT_TRUE(engine.Open().ok());
    EXPECT_EQ(engine.phase(), BackupPhase::kAborted);
    EXPECT_GE(engine.stats().orphan_chunks_hint, 1u);
    ASSERT_TRUE(engine.Verify().ok());
  }
}

}  // namespace
}  // namespace ebbackup
