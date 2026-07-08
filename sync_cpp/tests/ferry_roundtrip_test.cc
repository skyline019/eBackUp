#include <gtest/gtest.h>

#include <filesystem>

#include "ebbackup/archive/eb_bundle.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebsync/sync_agent.h"
#include "test_util.h"

TEST(FerryRoundtripTest, ExportImportVerify) {
  const std::string repo = ebbackup::test::TempDir("ferry_src");
  const std::string source = ebbackup::test::TempDir("ferry_source");
  const std::string ferry_dir = ebbackup::test::TempDir("ferry_out");
  const std::string imported = ebbackup::test::TempDir("ferry_imported");
  const std::string full_bundle = ebbackup::test::TempDir("ferry_full") + "/base.ebb";

  ASSERT_TRUE(ebbackup::test::InitDefaultRepo(repo).ok());
  ebbackup::test::WriteFile(source + "/a.txt", "aaa");
  ebbackup::BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  const uint64_t txn_a = engine.superblock().critical.txn_id;
  ASSERT_TRUE(ebbackup::ExportRepoToBundle(repo, full_bundle).ok());

  ebbackup::test::WriteFile(source + "/b.txt", "bbb");
  ASSERT_TRUE(engine.RunBackup(source, ebbackup::BackupMode::kIncremental).ok());
  const uint64_t txn_b = engine.superblock().critical.txn_id;
  ASSERT_GT(txn_b, txn_a);

  ASSERT_TRUE(ebsync::InitSyncRepoFerry(repo));
  ebsync::FerryExportOptions opts{};
  opts.base_txn = txn_a;
  opts.target_txn = txn_b;
  std::string summary;
  ASSERT_TRUE(ebsync::FerryExport(repo, ferry_dir, opts, &summary)) << summary;

  const std::string delta_path =
      ferry_dir + "/delta_" + std::to_string(txn_a) + "_" + std::to_string(txn_b) + ".ebb";
  ASSERT_TRUE(std::filesystem::exists(delta_path));

  std::string import_summary;
  ASSERT_TRUE(ebsync::FerryImport(full_bundle, delta_path, imported, &import_summary))
      << import_summary;

  ebbackup::BackupEngine imported_engine(imported);
  ASSERT_TRUE(imported_engine.Open().ok());
  ASSERT_TRUE(imported_engine.Verify().ok());

  ebsync::SyncState state;
  ASSERT_TRUE(ebsync::LoadSyncState(repo, &state));
  EXPECT_EQ(state.last_ferry_target_txn, txn_b);
  EXPECT_FALSE(ebsync::ShouldBlockMaintenance(repo, nullptr));
}
