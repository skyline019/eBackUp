#include <gtest/gtest.h>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/state/backup_phase.h"
#include "ebbackup/state/superblock.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(CoalescedMetaTest, RecoverManifestAheadOfSuperBlock) {
  const std::string repo = test::TempDir("coalesced_recover");
  const std::string source = test::TempDir("coalesced_source");
  ASSERT_TRUE(test::InitV05Repo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(512 * 1024, 4));

  {
    BackupEngine engine(repo);
    ASSERT_TRUE(engine.Open().ok());
    BackupOptions opts{};
    opts.use_pipeline = true;
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());
    ASSERT_TRUE(engine.Verify().ok());
  }

  ManifestDocument doc{};
  ASSERT_TRUE(ReadManifestAuto(repo + "/manifest", &doc).ok());
  ASSERT_GT(doc.txn_id, 0u);

  {
    BackupSuperBlockStore sb_store(repo + "/superblock.bin");
    BackupSuperBlock sb{};
    ASSERT_TRUE(sb_store.Load(&sb).ok());
    SetPhase(&sb, BackupPhase::kStoring);
    sb.critical.txn_id = doc.txn_id - 1;
    sb.critical.chunks_written = 3;
    ASSERT_TRUE(sb_store.Commit(sb).ok());
  }

  {
    BackupEngine engine(repo);
    ASSERT_TRUE(engine.Open().ok());
    EXPECT_EQ(engine.phase(), BackupPhase::kIdle);
    ASSERT_TRUE(engine.Verify().ok());
  }
}

}  // namespace
}  // namespace ebbackup
