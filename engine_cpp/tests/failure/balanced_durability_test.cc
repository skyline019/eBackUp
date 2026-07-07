#include <gtest/gtest.h>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/state/backup_phase.h"
#include "ebbackup/state/superblock.h"
#include "ebbackup/store/chunk_store.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(CommitPointDurabilityTest, BalancedInterruptedBackupRecoversAndVerifies) {
  const std::string repo = test::TempDir("balanced_interrupt");
  const std::string source = test::TempDir("balanced_interrupt_src");
  ASSERT_TRUE(test::InitV03Repo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(512 * 1024, 21));

  BackupOptions opts{};
  opts.durability = DurabilityMode::kBalanced;
  {
    BackupEngine engine(repo);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());
  }

  {
    BackupSuperBlockStore sb_store(repo + "/superblock.bin");
    BackupSuperBlock sb{};
    ASSERT_TRUE(sb_store.Load(&sb).ok());
    SetPhase(&sb, BackupPhase::kStoring);
    sb.critical.chunks_written = 3;
    ASSERT_TRUE(sb_store.Commit(sb).ok());
  }

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  EXPECT_EQ(engine.phase(), BackupPhase::kAborted);
  ASSERT_TRUE(engine.Verify().ok());
}

TEST(CommitPointDurabilityTest, StrictPipelineBackupVerifies) {
  const std::string repo = test::TempDir("strict_pipe");
  const std::string source = test::TempDir("strict_pipe_src");
  ASSERT_TRUE(test::InitV03Repo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(2 * 1024 * 1024, 33));

  BackupOptions opts{};
  opts.use_pipeline = true;
  opts.durability = DurabilityMode::kStrict;
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());
  ASSERT_TRUE(engine.Verify().ok());
}

TEST(CommitPointDurabilityTest, StrictInterruptedStoringRecoversAndVerifies) {
  const std::string repo = test::TempDir("strict_interrupt");
  const std::string source = test::TempDir("strict_interrupt_src");
  ASSERT_TRUE(test::InitV03Repo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(512 * 1024, 44));

  BackupOptions opts{};
  opts.use_pipeline = true;
  opts.durability = DurabilityMode::kStrict;
  {
    BackupEngine engine(repo);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());
  }

  {
    BackupSuperBlockStore sb_store(repo + "/superblock.bin");
    BackupSuperBlock sb{};
    ASSERT_TRUE(sb_store.Load(&sb).ok());
    SetPhase(&sb, BackupPhase::kStoring);
    sb.critical.chunks_written = 5;
    ASSERT_TRUE(sb_store.Commit(sb).ok());
  }

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  EXPECT_EQ(engine.phase(), BackupPhase::kAborted);
  ASSERT_TRUE(engine.Verify().ok());
}

TEST(CommitPointDurabilityTest, TruncatesCorruptChunkTailOnOpen) {
  const std::string repo = test::TempDir("balanced_truncate");
  const std::string source = test::TempDir("balanced_truncate_src");
  ASSERT_TRUE(test::InitV03Repo(repo).ok());
  test::WriteFile(source + "/file.bin", test::MakeSyntheticData(128 * 1024, 9));

  BackupOptions opts{};
  opts.durability = DurabilityMode::kBalanced;
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());

  const std::string chunks_path = repo + "/data/chunks";
  const uint64_t size_before = std::filesystem::file_size(chunks_path);
  {
    std::fstream io(chunks_path, std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(static_cast<bool>(io));
    io.seekp(static_cast<std::streamoff>(size_before));
    const char garbage[] = "partial-chunk-tail";
    io.write(garbage, sizeof(garbage) - 1);
    ASSERT_TRUE(static_cast<bool>(io));
  }

  ChunkStore store(chunks_path);
  store.SetUsePersistentIndex(true);
  ASSERT_TRUE(store.Open().ok());
  EXPECT_LT(store.file_size(), size_before + sizeof("partial-chunk-tail") - 1);

  BackupEngine verify_engine(repo);
  ASSERT_TRUE(verify_engine.Open().ok());
  ASSERT_TRUE(verify_engine.Verify().ok());
}

}  // namespace
}  // namespace ebbackup
