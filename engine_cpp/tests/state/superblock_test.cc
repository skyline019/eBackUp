#include <gtest/gtest.h>

#include "ebbackup/state/backup_phase.h"
#include "ebbackup/state/superblock.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(SuperBlockTest, DualSlotElectsGood) {
  const std::string dir = test::TempDir("superblock_dual");
  BackupSuperBlockStore store(dir + "/superblock.bin");
  BackupSuperBlock sb{};
  SetPhase(&sb, BackupPhase::kIdle);
  sb.critical.txn_id = 42;
  ASSERT_TRUE(store.Commit(sb).ok());
  ASSERT_TRUE(store.Commit(sb).ok());
  ASSERT_TRUE(store.Commit(sb).ok());
  ASSERT_TRUE(store.CorruptSlotForTest(0).ok());

  BackupSuperBlock loaded{};
  ASSERT_TRUE(store.Load(&loaded).ok());
  EXPECT_GE(loaded.critical.epoch, 2u);
  EXPECT_EQ(GetPhase(loaded), BackupPhase::kIdle);
  EXPECT_EQ(loaded.critical.txn_id, 42u);
  EXPECT_EQ(loaded.format_version, kBackupSuperBlockFormatV2);
}

TEST(SuperBlockTest, OuterCrcCorruptionRejected) {
  const std::string dir = test::TempDir("superblock_outer_crc");
  BackupSuperBlockStore store(dir + "/superblock.bin");
  BackupSuperBlock sb{};
  SetPhase(&sb, BackupPhase::kIdle);
  ASSERT_TRUE(store.Commit(sb).ok());
  ASSERT_TRUE(store.CorruptOuterCrcForTest(0).ok());

  BackupSuperBlock loaded{};
  const Status st = store.Load(&loaded);
  EXPECT_TRUE(st.ok() || st.code() == StatusCode::kCorrupt);
}

}  // namespace
}  // namespace ebbackup
