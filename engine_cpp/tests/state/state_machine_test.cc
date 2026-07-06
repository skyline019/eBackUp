#include <gtest/gtest.h>

#include "ebbackup/state/backup_phase.h"

namespace ebbackup {
namespace {

TEST(StateMachineTest, FullBackupTransitionChain) {
  BackupPhase phase = BackupPhase::kIdle;
  phase = NextPhase(phase, BackupEvent::kScanFile);
  EXPECT_EQ(phase, BackupPhase::kScanning);
  phase = NextPhase(phase, BackupEvent::kChunkFile);
  EXPECT_EQ(phase, BackupPhase::kChunking);
  phase = NextPhase(phase, BackupEvent::kStoreChunk);
  EXPECT_EQ(phase, BackupPhase::kStoring);
  phase = NextPhase(phase, BackupEvent::kCommitManifest);
  EXPECT_EQ(phase, BackupPhase::kCommittingMeta);
  phase = NextPhase(phase, BackupEvent::kAppendAudit);
  EXPECT_EQ(phase, BackupPhase::kAuditing);
  phase = NextPhase(phase, BackupEvent::kComplete);
  EXPECT_EQ(phase, BackupPhase::kIdle);
}

TEST(StateMachineTest, InvalidTransitionAborts) {
  BackupPhase phase = BackupPhase::kIdle;
  EXPECT_EQ(NextPhase(phase, BackupEvent::kStoreChunk), BackupPhase::kAborted);
  EXPECT_EQ(NextPhase(BackupPhase::kScanning, BackupEvent::kComplete),
            BackupPhase::kAborted);
}

}  // namespace
}  // namespace ebbackup
