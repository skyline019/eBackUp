#include <gtest/gtest.h>

#include <filesystem>

#include "ebbackup/archive/eb_bundle.h"
#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

namespace ebbackup {
namespace {

void CopyRepoTree(const std::string& from, const std::string& to) {
  std::error_code ec;
  std::filesystem::create_directories(to, ec);
  ASSERT_EQ(ec.value(), 0) << ec.message();
  std::filesystem::copy(from, to,
                        std::filesystem::copy_options::recursive |
                            std::filesystem::copy_options::overwrite_existing,
                        ec);
  ASSERT_EQ(ec.value(), 0) << ec.message();
}

TEST(EbBundleDeltaTest, DeltaExportImportVerifyRestore) {
  const std::string repo = test::TempDir("delta_repo");
  const std::string source = test::TempDir("delta_source");
  const std::string imported = test::TempDir("delta_imported");
  const std::string dest = test::TempDir("delta_dest");
  const std::string full_bundle = test::TempDir("delta_full") + "/base.ebb";
  const std::string delta_bundle = test::TempDir("delta_out") + "/delta.ebb";

  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  std::string data = test::MakeSyntheticData(8 * 1024 * 1024, 1);
  test::WriteFile(source + "/data.bin", data);

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  const uint64_t txn_a = engine.superblock().critical.txn_id;

  ASSERT_TRUE(ExportRepoToBundle(repo, full_bundle).ok());

  data[5 * 1024 * 1024] ^= 0x01;
  test::WriteFile(source + "/data.bin", data);
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental).ok());
  const uint64_t txn_b = engine.superblock().critical.txn_id;
  ASSERT_GT(txn_b, txn_a);

  const std::string target_full_bundle =
      test::TempDir("delta_target_full") + "/target.ebb";
  ASSERT_TRUE(ExportRepoToBundle(repo, target_full_bundle).ok());

  EbBundleDeltaOptions opts{};
  opts.base_txn_id = txn_a;
  EbBundleDeltaStats stats{};
  const Status export_st = ExportRepoDeltaToBundle(repo, delta_bundle, opts, &stats);
  ASSERT_TRUE(export_st.ok()) << export_st.message();
  EXPECT_EQ(stats.base_txn_id, txn_a);
  EXPECT_EQ(stats.target_txn_id, txn_b);
  EXPECT_GT(stats.delta_chunk_count, 0u);

  const auto target_full_size = std::filesystem::file_size(target_full_bundle);
  const auto delta_size = std::filesystem::file_size(delta_bundle);
  EXPECT_LT(delta_size, target_full_size);

  const Status import_st =
      ImportBundleDeltaToRepo(full_bundle, delta_bundle, imported);
  ASSERT_TRUE(import_st.ok()) << import_st.message();

  BackupEngine imported_engine(imported);
  ASSERT_TRUE(imported_engine.Open().ok());
  ASSERT_TRUE(imported_engine.Verify().ok());
  ASSERT_TRUE(imported_engine.Restore(dest).ok());
  EXPECT_TRUE(std::filesystem::exists(dest + "/data.bin"));
}

TEST(EbBundleDeltaTest, ApplyDeltaToExistingRepo) {
  const std::string repo = test::TempDir("delta_apply_repo");
  const std::string clone = test::TempDir("delta_apply_clone");
  const std::string source = test::TempDir("delta_apply_source");
  const std::string delta_bundle = test::TempDir("delta_apply_out") + "/delta.ebb";

  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/a.txt", "aaa");
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  const uint64_t txn_a = engine.superblock().critical.txn_id;

  test::WriteFile(source + "/b.txt", "bbb");
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental).ok());

  EbBundleDeltaOptions opts{};
  opts.base_txn_id = txn_a;
  const Status export_st = ExportRepoDeltaToBundle(repo, delta_bundle, opts);
  ASSERT_TRUE(export_st.ok()) << export_st.message();

  CopyRepoTree(repo, clone);
  ASSERT_TRUE(ApplyDeltaBundleToRepo(delta_bundle, clone).ok());

  BackupEngine clone_engine(clone);
  ASSERT_TRUE(clone_engine.Open().ok());
  ASSERT_TRUE(clone_engine.Verify().ok());
}

TEST(EbBundleDeltaTest, RejectsMissingBaseTxn) {
  const std::string repo = test::TempDir("delta_bad_repo");
  const std::string bundle = test::TempDir("delta_bad_out") + "/delta.ebb";
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  EbBundleDeltaOptions opts{};
  EXPECT_FALSE(ExportRepoDeltaToBundle(repo, bundle, opts).ok());
}

}  // namespace
}  // namespace ebbackup
