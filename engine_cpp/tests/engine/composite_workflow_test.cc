#include <gtest/gtest.h>

#include "ebbackup/archive/eb_bundle.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/store/chunk_compactor.h"
#include "ebbackup/store/retention_policy.h"
#include "ebbackup/store/snapshot_store.h"
#include "tree_util.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(CompositeWorkflowTest, FullMaintenanceTimeline) {
  const std::string repo = test::TempDir("composite_repo");
  const std::string source = test::TempDir("composite_src");
  const std::string dest = test::TempDir("composite_dest");
  const std::string bundle = test::TempDir("composite") + ".ebb";
  const std::string imported = test::TempDir("composite_import_repo");
  const std::string import_dest = test::TempDir("composite_import_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/app/config.json", "{\"v\":0}");

  BackupOptions opts{};
  opts.use_pipeline = true;
  opts.compress_mode = CompressMode::kAuto;
  opts.cpu_budget_permille = 600;

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());
  for (int i = 1; i <= 2; ++i) {
    test::WriteFile(source + "/app/config.json",
                    std::string("{\"v\":") + std::to_string(i) + "}");
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental, opts).ok());
  }

  std::vector<SnapshotEntry> snaps;
  ASSERT_TRUE(engine.ListSnapshots(&snaps).ok());
  ASSERT_GE(snaps.size(), 3u);
  const uint64_t txn_mid = snaps[snaps.size() / 2].txn_id;
  const uint64_t txn_old = snaps.front().txn_id;

  RetentionPolicy policy{};
  policy.retain_min = 2;
  policy.tiers.clear();
  PruneReport prune{};
  ASSERT_TRUE(engine.PruneSnapshots(policy, false, &prune).ok());
  EXPECT_GE(prune.kept_count, 2u);

  ASSERT_TRUE(engine.GcOrphans(false, nullptr, false).ok());

  CompactReport dry{};
  ASSERT_TRUE(engine.Compact(true, &dry).ok());
  CompactReport applied{};
  ASSERT_TRUE(engine.Compact(false, &applied).ok());

  BackupEngine fresh(repo);
  ASSERT_TRUE(fresh.Open().ok());
  ASSERT_TRUE(fresh.Verify().ok());
  BackupOptions verify_old{};
  verify_old.snapshot_txn_id = txn_old;
  if (fresh.ListSnapshots(&snaps).ok()) {
    bool old_kept = false;
    for (const auto& s : snaps) {
      if (s.txn_id == txn_old) old_kept = true;
    }
    if (old_kept) {
      ASSERT_TRUE(fresh.Verify(verify_old).ok());
    }
  }

  RestoreOptions ro{};
  ro.snapshot_txn_id = txn_mid;
  ASSERT_TRUE(fresh.Restore(dest, ro).ok());
  EXPECT_TRUE(std::filesystem::exists(dest + "/app/config.json"));

  ASSERT_TRUE(ExportRepoToBundle(repo, bundle).ok());
  ASSERT_TRUE(ImportBundleToRepo(bundle, imported).ok());
  BackupEngine imported_engine(imported);
  ASSERT_TRUE(imported_engine.Open().ok());
  ASSERT_TRUE(imported_engine.Verify().ok());
  ASSERT_TRUE(imported_engine.Restore(import_dest).ok());
  EXPECT_TRUE(std::filesystem::exists(import_dest + "/app/config.json"));
}

}  // namespace
}  // namespace ebbackup
