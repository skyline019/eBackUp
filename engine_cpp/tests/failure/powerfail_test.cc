#include <gtest/gtest.h>

#include <cstdlib>
#include <vector>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/state/superblock.h"
#include "subprocess_util.h"
#include "test_util.h"

namespace ebbackup {
namespace {

#ifndef EBTEST_CI
constexpr int kRandomPhaseTrials = 16;
#else
constexpr int kRandomPhaseTrials = 4;
#endif

TEST(PowerfailTest, DualSlotSurvivesCorruptSlotAfterBackup) {
  const std::string repo = test::TempDir("powerfail_dual_slot");
  const std::string source = test::TempDir("powerfail_dual_source");
  ASSERT_TRUE(BackupEngine::InitRepo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(256 * 1024, 4));

  {
    BackupEngine engine(repo);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source).ok());
    ASSERT_TRUE(engine.Verify().ok());
  }

  BackupSuperBlockStore store(repo + "/superblock.bin");
  ASSERT_TRUE(store.CorruptSlotForTest(0).ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.Verify().ok());
}

TEST(PowerfailTest, InterruptedPhaseRecovery) {
  const std::string repo = test::TempDir("powerfail_interrupt");
  const std::string source = test::TempDir("powerfail_interrupt_source");
  ASSERT_TRUE(BackupEngine::InitRepo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(512 * 1024, 3));

  {
    BackupEngine engine(repo);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source).ok());
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

TEST(PowerfailTest, SubprocessKillMidBackup) {
  const std::string repo = test::TempDir("powerfail_subproc");
  const std::string source = test::TempDir("powerfail_subproc_source");
  ASSERT_TRUE(BackupEngine::InitRepo(repo).ok());
  test::WriteFile(source + "/big.bin", test::MakeSyntheticData(4 * 1024 * 1024, 8));

  ASSERT_TRUE(test::RunBackupSubprocessAndKill(repo, source, 0).ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  if (engine.phase() != BackupPhase::kIdle &&
      engine.phase() != BackupPhase::kComplete) {
    ASSERT_TRUE(engine.Recover().ok());
  }
  const Status verify_st = engine.Verify();
  EXPECT_TRUE(verify_st.ok() || engine.phase() == BackupPhase::kAborted);
}

TEST(PowerfailTest, RandomPhaseInjection) {
  const std::vector<BackupPhase> phases = {BackupPhase::kChunking,
                                           BackupPhase::kStoring,
                                           BackupPhase::kCommittingMeta};
  for (int trial = 0; trial < kRandomPhaseTrials; ++trial) {
    const std::string repo = test::TempDir("powerfail_rand_" + std::to_string(trial));
    const std::string source = test::TempDir("powerfail_rand_src_" + std::to_string(trial));
    ASSERT_TRUE(BackupEngine::InitRepo(repo).ok());
    test::WriteFile(source + "/data.bin", test::MakeSyntheticData(128 * 1024, trial));

    const BackupPhase target = phases[static_cast<size_t>(trial) % phases.size()];
#ifdef _WIN32
    _putenv(("EBTEST_ABORT_AFTER=" + std::to_string(static_cast<int>(target))).c_str());
#else
    setenv("EBTEST_ABORT_AFTER", std::to_string(static_cast<int>(target)).c_str(), 1);
#endif

    {
      BackupEngine engine(repo);
      ASSERT_TRUE(engine.Open().ok());
      const Status st = engine.RunBackup(source);
      EXPECT_FALSE(st.ok());
    }

    BackupEngine engine(repo);
    ASSERT_TRUE(engine.Open().ok());
    if (engine.phase() != BackupPhase::kIdle) {
      ASSERT_TRUE(engine.Recover().ok());
    }

#ifdef _WIN32
    _putenv("EBTEST_ABORT_AFTER=");
#else
    unsetenv("EBTEST_ABORT_AFTER");
#endif
  }
}

}  // namespace
}  // namespace ebbackup
