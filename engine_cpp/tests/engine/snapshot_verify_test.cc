#include <gtest/gtest.h>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/store/snapshot_store.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(SnapshotVerifyTest, VerifyHistoricalTxn) {
  const std::string repo = test::TempDir("snap_verify");
  const std::string source = test::TempDir("snap_verify_src");

  ASSERT_TRUE(test::InitV03Repo(repo).ok());
  test::WriteFile(source + "/data.txt", "verify-me");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  std::vector<SnapshotEntry> snaps;
  ASSERT_TRUE(ListSnapshots(repo, &snaps).ok());
  ASSERT_EQ(snaps.size(), 1u);
  const uint64_t txn = snaps[0].txn_id;

  test::WriteFile(source + "/data.txt", "verify-me-v2");
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental).ok());

  BackupOptions verify_opts{};
  verify_opts.snapshot_txn_id = txn;
  ASSERT_TRUE(engine.Verify(verify_opts).ok());
}

}  // namespace
}  // namespace ebbackup
