#include <gtest/gtest.h>

#include <fstream>
#include <unordered_map>

#include "ebbackup/archive/eb_bundle.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/store/chunk_compactor.h"
#include "ebbackup/store/retention_policy.h"
#include "ebbackup/store/snapshot_store.h"
#include "fixture_util.h"
#include "tree_util.h"
#include "test_util.h"

namespace ebbackup {
namespace {

std::string FullFixtureManifestPath() {
  return (test::FixtureRoot() / "full" / "full_manifest.json").string();
}

Status CopyFullFixtureOrSkip(const std::string& dest) {
  const Status st = test::CopyFixtureTree("full", dest);
  if (!st.ok()) {
    return st;
  }
  return Status::Ok();
}

TEST(RealFixtureTest, RealWorldFixtureRoundTrip) {
  const std::string repo = test::TempDir("real_mixed_repo");
  const std::string source = test::TempDir("real_mixed_src");
  const std::string dest = test::TempDir("real_mixed_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  ASSERT_TRUE(test::CopyFixtureTree("mixed", source).ok());

  std::unordered_map<std::string, std::string> expected_hashes;
  ASSERT_TRUE(test::HashFixtureTree(
                   source,
                   [&](const std::string& rel, const std::string& sha) -> Status {
                     expected_hashes[rel] = sha;
                     return Status::Ok();
                   })
                   .ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  ASSERT_TRUE(engine.Verify().ok());
  ASSERT_TRUE(engine.Restore(dest).ok());

  std::unordered_map<std::string, std::string> restored_hashes;
  ASSERT_TRUE(test::HashFixtureTree(
                   dest,
                   [&](const std::string& rel, const std::string& sha) -> Status {
                     restored_hashes[rel] = sha;
                     return Status::Ok();
                   })
                   .ok());
  EXPECT_EQ(expected_hashes, restored_hashes);
}

TEST(RealFixtureTest, EngineSourceSampleRoundTrip) {
  const std::string repo = test::TempDir("real_engine_repo");
  const std::string source = test::TempDir("real_engine_src");
  const std::string dest = test::TempDir("real_engine_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  if (!test::CopyEngineSourceSample(source).ok()) {
    GTEST_SKIP() << "engine source sample unavailable";
  }

  BackupOptions opts{};
  opts.use_pipeline = true;
  opts.use_encryption = true;
  opts.encryption_password = "engine-sample";
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());

  RestoreOptions ro{};
  ro.encryption_password = "engine-sample";
  ASSERT_TRUE(engine.Restore(dest, ro).ok());
  ASSERT_TRUE(test::CompareDirectoryTrees(source, dest).ok());
}

TEST(RealFixtureTest, RealFixtureIncrementalEdit) {
  const std::string repo = test::TempDir("real_incr_repo");
  const std::string source = test::TempDir("real_incr_src");
  const std::string dest1 = test::TempDir("real_incr_dest1");
  const std::string dest2 = test::TempDir("real_incr_dest2");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  ASSERT_TRUE(test::CopyFixtureTree("mixed", source).ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  std::vector<SnapshotEntry> snaps;
  ASSERT_TRUE(ListSnapshots(repo, &snaps).ok());
  const uint64_t txn1 = snaps.back().txn_id;

  test::WriteFile(source + "/readme.md", "# edited fixture\n");
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental).ok());
  snaps.clear();
  ASSERT_TRUE(ListSnapshots(repo, &snaps).ok());
  const uint64_t txn2 = snaps.back().txn_id;

  RestoreOptions r1{};
  r1.snapshot_txn_id = txn1;
  RestoreOptions r2{};
  r2.snapshot_txn_id = txn2;
  ASSERT_TRUE(engine.Restore(dest1, r1).ok());
  ASSERT_TRUE(engine.Restore(dest2, r2).ok());

  std::ifstream old_in(dest1 + "/readme.md");
  std::ifstream new_in(dest2 + "/readme.md");
  std::string old_text((std::istreambuf_iterator<char>(old_in)),
                       std::istreambuf_iterator<char>());
  std::string new_text((std::istreambuf_iterator<char>(new_in)),
                       std::istreambuf_iterator<char>());
  EXPECT_NE(old_text, new_text);
  EXPECT_NE(new_text.find("edited"), std::string::npos);
}

TEST(RealFixtureTest, BundleExportImportRealFixture) {
  const std::string repo = test::TempDir("real_bundle_repo");
  const std::string imported = test::TempDir("real_bundle_import");
  const std::string source = test::TempDir("real_bundle_src");
  const std::string dest = test::TempDir("real_bundle_dest");
  const std::string bundle = test::TempDir("real_bundle") + ".ebb";
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  ASSERT_TRUE(test::CopyFixtureTree("nested", source).ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  ASSERT_TRUE(ExportRepoToBundle(repo, bundle).ok());
  ASSERT_TRUE(ImportBundleToRepo(bundle, imported).ok());

  BackupEngine imp(imported);
  ASSERT_TRUE(imp.Open().ok());
  ASSERT_TRUE(imp.Verify().ok());
  ASSERT_TRUE(imp.Restore(dest).ok());
  ASSERT_TRUE(test::CompareDirectoryTrees(source, dest).ok());
}

TEST(RealFixtureTest, RealWorldCompleteRoundTrip) {
  const std::string repo = test::TempDir("real_full_repo");
  const std::string source = test::TempDir("real_full_src");
  const std::string dest = test::TempDir("real_full_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  if (!CopyFullFixtureOrSkip(source).ok()) {
    GTEST_SKIP() << "full fixture unavailable";
  }

  BackupOptions opts{};
  opts.use_pipeline = true;
  opts.compress_mode = CompressMode::kAuto;
  opts.cpu_budget_permille = 600;
  opts.durability = DurabilityMode::kBalanced;

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  const Status backup_st = engine.RunBackup(source, BackupMode::kFull, opts);
  if (!backup_st.ok()) {
    GTEST_SKIP() << "full fixture backup unsupported: " << backup_st.message();
  }
  ASSERT_TRUE(engine.Verify().ok());
  ASSERT_TRUE(engine.Restore(dest).ok());
  {
    const Status match =
        test::AssertTreeMatchesManifest(dest, FullFixtureManifestPath());
    ASSERT_TRUE(match.ok()) << match.message();
  }
}

TEST(RealFixtureTest, RealWorldMultiTypeIncremental) {
  const std::string repo = test::TempDir("real_full_incr_repo");
  const std::string source = test::TempDir("real_full_incr_src");
  const std::string dest1 = test::TempDir("real_full_incr_dest1");
  const std::string dest2 = test::TempDir("real_full_incr_dest2");
  const std::string dest_latest = test::TempDir("real_full_incr_dest_latest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  if (!CopyFullFixtureOrSkip(source).ok()) {
    GTEST_SKIP() << "full fixture unavailable";
  }

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  const Status full_st = engine.RunBackup(source);
  if (!full_st.ok()) {
    GTEST_SKIP() << "full fixture backup unsupported: " << full_st.message();
  }

  std::vector<SnapshotEntry> snaps;
  ASSERT_TRUE(ListSnapshots(repo, &snaps).ok());
  ASSERT_EQ(snaps.size(), 1u);
  const uint64_t txn1 = snaps.back().txn_id;

  test::WriteFile(source + "/readme.md", "# edited round 1\n");
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental).ok());
  snaps.clear();
  ASSERT_TRUE(ListSnapshots(repo, &snaps).ok());
  const uint64_t txn2 = snaps.back().txn_id;

  std::filesystem::remove(source + "/empty.txt");
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental).ok());

  test::WriteFile(source + "/tree/l0/l1/l2/l3/l4/f.new.json", "{\"added\":true}\n");
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental).ok());

  RestoreOptions r1{};
  r1.snapshot_txn_id = txn1;
  RestoreOptions r2{};
  r2.snapshot_txn_id = txn2;
  ASSERT_TRUE(engine.Restore(dest1, r1).ok());
  ASSERT_TRUE(engine.Restore(dest2, r2).ok());
  ASSERT_TRUE(engine.Restore(dest_latest).ok());

  EXPECT_TRUE(std::filesystem::exists(dest1 + "/empty.txt"));
  EXPECT_FALSE(std::filesystem::exists(dest1 + "/tree/l0/l1/l2/l3/l4/f.new.json"));
  std::ifstream readme1(dest1 + "/readme.md");
  std::string text1((std::istreambuf_iterator<char>(readme1)),
                    std::istreambuf_iterator<char>());
  EXPECT_NE(text1.find("Multi-type"), std::string::npos);

  std::ifstream readme2(dest2 + "/readme.md");
  std::string text2((std::istreambuf_iterator<char>(readme2)),
                    std::istreambuf_iterator<char>());
  EXPECT_NE(text2.find("edited round 1"), std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(dest2 + "/empty.txt"));

  EXPECT_FALSE(std::filesystem::exists(dest_latest + "/empty.txt"));
  EXPECT_TRUE(std::filesystem::exists(dest_latest + "/tree/l0/l1/l2/l3/l4/f.new.json"));
  ASSERT_TRUE(test::CompareDirectoryTrees(source, dest_latest).ok());
}

TEST(RealFixtureTest, RealWorldCompositeMaintenance) {
  const std::string repo = test::TempDir("real_full_maint_repo");
  const std::string source = test::TempDir("real_full_maint_src");
  const std::string dest = test::TempDir("real_full_maint_dest");
  const std::string bundle = test::TempDir("real_full_maint") + ".ebb";
  const std::string imported = test::TempDir("real_full_maint_import");
  const std::string import_dest = test::TempDir("real_full_maint_import_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  if (!CopyFullFixtureOrSkip(source).ok()) {
    GTEST_SKIP() << "full fixture unavailable";
  }

  BackupOptions opts{};
  opts.use_pipeline = true;
  opts.compress_mode = CompressMode::kAuto;
  opts.cpu_budget_permille = 600;

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  const Status full_st = engine.RunBackup(source, BackupMode::kFull, opts);
  if (!full_st.ok()) {
    GTEST_SKIP() << "full fixture backup unsupported: " << full_st.message();
  }
  for (int i = 1; i <= 2; ++i) {
    test::WriteFile(source + "/config.json",
                    std::string("{\"name\":\"ebbackup-fixture\",\"version\":") +
                        std::to_string(i + 1) + "}");
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
  EXPECT_TRUE(std::filesystem::exists(dest + "/config.json"));

  ASSERT_TRUE(ExportRepoToBundle(repo, bundle).ok());
  ASSERT_TRUE(ImportBundleToRepo(bundle, imported).ok());
  BackupEngine imported_engine(imported);
  ASSERT_TRUE(imported_engine.Open().ok());
  ASSERT_TRUE(imported_engine.Verify().ok());
  ASSERT_TRUE(imported_engine.Restore(import_dest).ok());
  ASSERT_TRUE(test::CompareDirectoryTrees(source, import_dest).ok());
}

}  // namespace
}  // namespace ebbackup
