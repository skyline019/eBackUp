#include <gtest/gtest.h>

#include "chaos_util.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/state/backup_phase.h"
#include "ebbackup/state/superblock.h"
#include "ebbackup/store/chunk_compactor.h"
#include "subprocess_util.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(PowerfailExtendedTest, KillDuringIncremental) {
  const std::string repo = test::TempDir("pf_incr_repo");
  const std::string source = test::TempDir("pf_incr_src");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/a.bin", test::MakeSyntheticData(2 * 1024 * 1024, 1));

  {
    BackupEngine engine(repo);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source).ok());
  }

  BackupOptions opts{};
  opts.use_pipeline = true;
  ASSERT_TRUE(
      test::RunBackupSubprocessAndKill(repo, source, BackupMode::kIncremental, opts, 50)
          .ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  if (engine.phase() != BackupPhase::kIdle &&
      engine.phase() != BackupPhase::kComplete) {
    ASSERT_TRUE(engine.Recover().ok());
  }
  test::WriteFile(source + "/b.bin", test::MakeSyntheticData(1024 * 1024, 2));
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental).ok());
  ASSERT_TRUE(engine.Verify().ok());
}

TEST(PowerfailExtendedTest, AbortDuringCommitManifest) {
  const std::string repo = test::TempDir("pf_commit_repo");
  const std::string source = test::TempDir("pf_commit_src");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(256 * 1024, 5));

  {
    test::ScopedAbortAfterPhase guard(BackupPhase::kCommittingMeta);
    BackupEngine engine(repo);
    ASSERT_TRUE(engine.Open().ok());
    EXPECT_FALSE(engine.RunBackup(source).ok());
  }

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  if (engine.phase() != BackupPhase::kIdle) {
    ASSERT_TRUE(engine.Recover().ok());
  }
  ASSERT_TRUE(engine.RunBackup(source).ok());
  ASSERT_TRUE(engine.Verify().ok());
}

TEST(PowerfailExtendedTest, InterruptedDuringAuditingPhase) {
  const std::string repo = test::TempDir("pf_audit_repo");
  const std::string source = test::TempDir("pf_audit_src");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(256 * 1024, 6));

  {
    BackupEngine engine(repo);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source).ok());
  }
  ASSERT_TRUE(test::InjectPhase(repo, BackupPhase::kAuditing, 1).ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  EXPECT_EQ(engine.phase(), BackupPhase::kAborted);
  ASSERT_TRUE(engine.Verify().ok());
}

TEST(PowerfailExtendedTest, DualSlotBothCorruptOneRecoverable) {
  const std::string repo = test::TempDir("pf_dual_both");
  const std::string source = test::TempDir("pf_dual_both_src");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/a.txt", "dual-slot");

  {
    BackupEngine engine(repo);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source).ok());
  }

  BackupSuperBlockStore store(repo + "/superblock.bin");
  ASSERT_TRUE(store.CorruptSlotForTest(0).ok());
  {
    BackupEngine engine(repo);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.Verify().ok());
  }
  ASSERT_TRUE(store.CorruptSlotForTest(1).ok());
  BackupEngine engine(repo);
  EXPECT_FALSE(engine.Open().ok());
}

TEST(PowerfailExtendedTest, KillDuringStrictPipelineFull) {
  const std::string repo = test::TempDir("pf_strict_pipe_repo");
  const std::string source = test::TempDir("pf_strict_pipe_src");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(1024 * 1024, 17));

  BackupOptions opts{};
  opts.use_pipeline = true;
  opts.durability = DurabilityMode::kStrict;
  ASSERT_TRUE(
      test::RunBackupSubprocessAndKill(repo, source, BackupMode::kFull, opts, 50)
          .ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  if (engine.phase() != BackupPhase::kIdle &&
      engine.phase() != BackupPhase::kComplete) {
    ASSERT_TRUE(engine.Recover().ok());
  }
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());
  ASSERT_TRUE(engine.Verify().ok());
}

TEST(PowerfailExtendedTest, KillDuringV05EbPackCoalescedPipeline) {
  const std::string repo = test::TempDir("pf_v05_pipe_repo");
  const std::string source = test::TempDir("pf_v05_pipe_src");
  ASSERT_TRUE(test::InitV05Repo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(2 * 1024 * 1024, 21));

  BackupOptions opts{};
  opts.use_pipeline = true;
  opts.durability = DurabilityMode::kStrict;
  ASSERT_TRUE(
      test::RunBackupSubprocessAndKill(repo, source, BackupMode::kFull, opts, 50)
          .ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  if (engine.phase() != BackupPhase::kIdle &&
      engine.phase() != BackupPhase::kComplete) {
    ASSERT_TRUE(engine.Recover().ok());
  }
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());
  ASSERT_TRUE(engine.Verify().ok());
}

TEST(PowerfailExtendedTest, KillDuringShardedEbPackPipeline) {
  const std::string repo = test::TempDir("pf_shard_pipe_repo");
  const std::string source = test::TempDir("pf_shard_pipe_src");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  for (int i = 0; i < 8; ++i) {
    test::WriteFile(source + "/f" + std::to_string(i) + ".bin",
                    test::MakeSyntheticData(256 * 1024, static_cast<uint8_t>(i + 1)));
  }

  BackupOptions opts{};
  opts.use_pipeline = true;
  opts.durability = DurabilityMode::kStrict;
  opts.worker_count = 8;
  ASSERT_TRUE(
      test::RunBackupSubprocessAndKill(repo, source, BackupMode::kFull, opts, 50)
          .ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  if (engine.phase() != BackupPhase::kIdle &&
      engine.phase() != BackupPhase::kComplete) {
    ASSERT_TRUE(engine.Recover().ok());
  }
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());
  ASSERT_TRUE(engine.Verify().ok());
}

TEST(PowerfailExtendedTest, CompactIdempotentAfterOrphanInject) {
  const std::string repo = test::TempDir("pf_compact_repo");
  const std::string source = test::TempDir("pf_compact_src");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/file.bin", test::MakeSyntheticData(128 * 1024, 8));

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  CompactReport dry{};
  ASSERT_TRUE(CompactChunkStore(repo, true, &dry).ok());
  CompactReport apply{};
  ASSERT_TRUE(CompactChunkStore(repo, false, &apply).ok());
  BackupEngine verify_engine(repo);
  ASSERT_TRUE(verify_engine.Open().ok());
  ASSERT_TRUE(verify_engine.Verify().ok());
}

}  // namespace
}  // namespace ebbackup
