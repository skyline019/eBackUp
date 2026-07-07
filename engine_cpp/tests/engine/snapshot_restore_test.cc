#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/store/snapshot_store.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(SnapshotRestoreTest, RestoreDifferentTxnStates) {
  const std::string repo = test::TempDir("snap_restore");
  const std::string source = test::TempDir("snap_restore_src");
  const std::string dest1 = test::TempDir("snap_restore_dest1");
  const std::string dest2 = test::TempDir("snap_restore_dest2");

  ASSERT_TRUE(test::InitV03Repo(repo).ok());
  test::WriteFile(source + "/version.txt", "v1");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  std::vector<SnapshotEntry> snaps;
  ASSERT_TRUE(ListSnapshots(repo, &snaps).ok());
  ASSERT_EQ(snaps.size(), 1u);
  const uint64_t txn1 = snaps[0].txn_id;

  test::WriteFile(source + "/version.txt", "v2-updated");
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental).ok());

  snaps.clear();
  ASSERT_TRUE(ListSnapshots(repo, &snaps).ok());
  ASSERT_EQ(snaps.size(), 2u);
  const uint64_t txn2 = snaps.back().txn_id;

  RestoreOptions opts1{};
  opts1.snapshot_txn_id = txn1;
  ASSERT_TRUE(engine.Restore(dest1, opts1).ok());
  {
    std::ifstream in(dest1 + "/version.txt");
    std::string content;
    std::getline(in, content);
    EXPECT_EQ(content, "v1");
  }

  RestoreOptions opts2{};
  opts2.snapshot_txn_id = txn2;
  ASSERT_TRUE(engine.Restore(dest2, opts2).ok());
  {
    std::ifstream in(dest2 + "/version.txt");
    std::string content;
    std::getline(in, content);
    EXPECT_EQ(content, "v2-updated");
  }
}

}  // namespace
}  // namespace ebbackup
