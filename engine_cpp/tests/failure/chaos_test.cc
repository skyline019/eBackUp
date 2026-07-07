#include <gtest/gtest.h>

#include <fstream>
#include <random>
#include <vector>

#include "chaos_util.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/state/backup_phase.h"
#include "ebbackup/store/snapshot_store.h"
#include "test_util.h"

namespace ebbackup {
namespace {

constexpr int kChaosTrials = 8;

TEST(ChaosTest, RandomPhaseThenRecover) {
  const std::vector<BackupPhase> phases = {
      BackupPhase::kScanning, BackupPhase::kChunking, BackupPhase::kStoring,
      BackupPhase::kCommittingMeta,
  };
  std::mt19937 gen(42);
  for (int trial = 0; trial < kChaosTrials; ++trial) {
    const std::string repo = test::TempDir("chaos_phase_" + std::to_string(trial));
    const std::string source = test::TempDir("chaos_phase_src_" + std::to_string(trial));
    ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
    test::WriteFile(source + "/data.bin", test::MakeSyntheticData(128 * 1024, trial));

    const BackupPhase target = phases[static_cast<size_t>(trial) % phases.size()];
    {
      test::ScopedAbortAfterPhase guard(target);
      BackupEngine engine(repo);
      ASSERT_TRUE(engine.Open().ok());
      EXPECT_FALSE(engine.RunBackup(source).ok());
    }

    BackupEngine engine(repo);
    ASSERT_TRUE(engine.Open().ok());
    if (engine.phase() != BackupPhase::kIdle) {
      ASSERT_TRUE(engine.Recover().ok());
    }
  }
}

TEST(ChaosTest, RandomChunkBitFlipThenVerify) {
  const std::string repo = test::TempDir("chaos_flip_repo");
  const std::string source = test::TempDir("chaos_flip_src");
  ASSERT_TRUE(test::InitV03Repo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(512 * 1024, 11));

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  ASSERT_TRUE(engine.Verify().ok());

  ASSERT_TRUE(test::CorruptChunkPayloadByte(repo, 42).ok());
  BackupEngine verify_engine(repo);
  ASSERT_TRUE(verify_engine.Open().ok());
  EXPECT_EQ(verify_engine.Verify().code(), StatusCode::kCorrupt);
}

TEST(ChaosTest, InterleavedBackupVerify) {
  const std::string repo = test::TempDir("chaos_interleave_repo");
  const std::string source = test::TempDir("chaos_interleave_src");
  const std::string dest = test::TempDir("chaos_interleave_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/payload.bin", test::MakeSyntheticData(64 * 1024, 12));

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  for (int i = 0; i < 4; ++i) {
    test::WriteFile(source + "/payload.bin",
                    test::MakeSyntheticData(64 * 1024, static_cast<uint8_t>(12 + i)));
    const BackupMode mode =
        i == 0 ? BackupMode::kFull : BackupMode::kIncremental;
    ASSERT_TRUE(engine.RunBackup(source, mode).ok());
    ASSERT_TRUE(engine.Verify().ok());
  }
  ASSERT_TRUE(engine.Restore(dest).ok());
  std::ifstream in(dest + "/payload.bin", std::ios::binary);
  const std::string restored((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
  EXPECT_EQ(restored.size(), 64u * 1024u);
}

TEST(ChaosTest, ManifestTailTruncationRecover) {
  const std::string repo = test::TempDir("chaos_trunc_repo");
  const std::string source = test::TempDir("chaos_trunc_src");
  const std::string dest = test::TempDir("chaos_trunc_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/v1.txt", "version-one");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  std::vector<SnapshotEntry> snaps;
  ASSERT_TRUE(ListSnapshots(repo, &snaps).ok());
  ASSERT_EQ(snaps.size(), 1u);
  const uint64_t txn = snaps[0].txn_id;

  test::WriteFile(source + "/v1.txt", "version-two");
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental).ok());

  const std::string manifest = repo + "/manifest";
  const uint64_t size = std::filesystem::file_size(manifest);
  ASSERT_TRUE(test::TruncateFile(manifest, size > 4 ? size - 4 : 0).ok());

  BackupEngine verify_engine(repo);
  ASSERT_TRUE(verify_engine.Open().ok());
  EXPECT_FALSE(verify_engine.Verify().ok());

  RestoreOptions opts{};
  opts.snapshot_txn_id = txn;
  ASSERT_TRUE(engine.Restore(dest, opts).ok());
  std::ifstream in(dest + "/v1.txt");
  std::string content;
  std::getline(in, content);
  EXPECT_EQ(content, "version-one");
}

}  // namespace
}  // namespace ebbackup
