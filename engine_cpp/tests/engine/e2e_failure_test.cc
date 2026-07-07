#include <gtest/gtest.h>

#include "chaos_util.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/state/superblock.h"
#include "ebbackup/store/chunk_store.h"
#include "ebbackup/store/retention_policy.h"
#include "ebbackup/store/chunk_compactor.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(E2eFailureTest, RestoreAtMissingTxn) {
  const std::string repo = test::TempDir("fail_missing_txn");
  const std::string source = test::TempDir("fail_missing_txn_src");
  const std::string dest = test::TempDir("fail_missing_txn_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/a.txt", "x");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  RestoreOptions opts{};
  opts.snapshot_txn_id = 999999;
  EXPECT_FALSE(engine.Restore(dest, opts).ok());
}

TEST(E2eFailureTest, RestoreAtPrunedSnapshot) {
  const std::string repo = test::TempDir("fail_pruned_snap");
  const std::string source = test::TempDir("fail_pruned_snap_src");
  const std::string dest = test::TempDir("fail_pruned_snap_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  for (int i = 0; i < 3; ++i) {
    test::WriteFile(source + "/v.txt", "v" + std::to_string(i));
    const BackupMode mode =
        i == 0 ? BackupMode::kFull : BackupMode::kIncremental;
    ASSERT_TRUE(engine.RunBackup(source, mode).ok());
  }

  std::vector<SnapshotEntry> snaps;
  ASSERT_TRUE(ListSnapshots(repo, &snaps).ok());
  ASSERT_GE(snaps.size(), 2u);
  const uint64_t old_txn = snaps.front().txn_id;

  RetentionPolicy policy{};
  policy.retain_min = 1;
  policy.tiers.clear();
  PruneReport report{};
  ASSERT_TRUE(PruneSnapshots(repo, policy, false, &report).ok());
  EXPECT_GE(report.pruned_count, 1u);

  RestoreOptions opts{};
  opts.snapshot_txn_id = old_txn;
  EXPECT_FALSE(engine.Restore(dest, opts).ok());
}

TEST(E2eFailureTest, VerifyDetectsCorruptChunk) {
  const std::string repo = test::TempDir("fail_corrupt_chunk");
  const std::string source = test::TempDir("fail_corrupt_chunk_src");
  ASSERT_TRUE(test::InitV03Repo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(256 * 1024, 2));

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  ASSERT_TRUE(test::CorruptChunkPayloadByte(repo, 7).ok());

  BackupEngine verify_engine(repo);
  ASSERT_TRUE(verify_engine.Open().ok());
  EXPECT_EQ(verify_engine.Verify().code(), StatusCode::kCorrupt);
}

TEST(E2eFailureTest, RestoreWrongPassword) {
  const std::string repo = test::TempDir("fail_wrong_pass");
  const std::string source = test::TempDir("fail_wrong_pass_src");
  const std::string dest = test::TempDir("fail_wrong_pass_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/x.txt", "secret");

  BackupOptions opts{};
  opts.use_encryption = true;
  opts.encryption_password = "real";
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());

  RestoreOptions ro{};
  ro.encryption_password = "wrong";
  EXPECT_FALSE(engine.Restore(dest, ro).ok());
}

TEST(E2eFailureTest, BackupMissingSource) {
  const std::string repo = test::TempDir("fail_missing_src");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  const std::string missing =
      (test::TestOutputRoot() / "definitely_missing_source_xyz").string();
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  EXPECT_EQ(engine.RunBackup(missing).code(), StatusCode::kNotFound);
}

TEST(E2eFailureTest, OpenCorruptSuperblock) {
  const std::string repo = test::TempDir("fail_bad_sb");
  const std::string source = test::TempDir("fail_bad_sb_src");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/a.txt", "x");
  {
    BackupEngine engine(repo);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source).ok());
  }
  BackupSuperBlockStore store(repo + "/superblock.bin");
  ASSERT_TRUE(store.CorruptSlotForTest(0).ok());
  ASSERT_TRUE(store.CorruptSlotForTest(1).ok());
  BackupEngine engine(repo);
  EXPECT_FALSE(engine.Open().ok());
}

TEST(E2eFailureTest, GcOrphansWhileBusy) {
  const std::string repo = test::TempDir("fail_gc_busy");
  const std::string source = test::TempDir("fail_gc_busy_src");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/a.txt", "busy");
  {
    BackupEngine engine(repo);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source).ok());
  }
  ASSERT_TRUE(test::InjectPhase(repo, BackupPhase::kStoring, 3).ok());
  CompactReport report{};
  EXPECT_EQ(CompactChunkStore(repo, true, &report).code(), StatusCode::kConflict);
}

}  // namespace
}  // namespace ebbackup
