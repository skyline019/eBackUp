#include <gtest/gtest.h>

#include <fstream>

#include "ebbackup/archive/eb_bundle.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/store/retention_policy.h"
#include "ebbackup/store/snapshot_store.h"
#include "fixture_util.h"
#include "tree_util.h"
#include "test_util.h"

namespace ebbackup {
namespace {

std::string MediaFixtureManifestPath() {
  return (test::FixtureRoot() / "media" / "media_manifest.json").string();
}

bool MediaFixtureAvailable() {
  const auto root = test::FixtureRoot() / "media";
  return std::filesystem::exists(root / "video" / "sample.mp4") &&
         std::filesystem::exists(root / "archives" / "sample.zip") &&
         std::filesystem::exists(root / "binaries" / "sample.exe") &&
         std::filesystem::exists(MediaFixtureManifestPath());
}

bool FileHasPrefix(const std::string& path, const char* prefix, size_t len) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::string head(len, '\0');
  in.read(head.data(), static_cast<std::streamsize>(len));
  return in.gcount() >= static_cast<std::streamsize>(len) &&
         head.compare(0, len, prefix, len) == 0;
}

void CopyFileBytes(const std::string& from, const std::string& to) {
  std::ifstream in(from, std::ios::binary);
  std::ofstream out(to, std::ios::binary);
  out << in.rdbuf();
}

TEST(MediaFixtureTest, RealMediaTypeAgnosticRoundTrip) {
  if (!MediaFixtureAvailable()) {
    GTEST_SKIP() << "media fixture missing; run fetch_media_fixtures.ps1";
  }

  const std::string repo = test::TempDir("media_roundtrip_repo");
  const std::string source = test::TempDir("media_roundtrip_src");
  const std::string dest = test::TempDir("media_roundtrip_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  ASSERT_TRUE(test::CopyFixtureTree("media", source).ok());

  BackupOptions opts{};
  opts.use_pipeline = true;
  opts.compress_mode = CompressMode::kAuto;
  opts.cpu_budget_permille = 600;
  opts.durability = DurabilityMode::kBalanced;

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());
  ASSERT_TRUE(engine.Verify().ok());
  ASSERT_TRUE(engine.Restore(dest).ok());

  const Status match =
      test::AssertTreeMatchesManifest(dest, MediaFixtureManifestPath());
  ASSERT_TRUE(match.ok()) << match.message();

  EXPECT_EQ(std::filesystem::file_size(source + "/video/sample.mp4"),
            std::filesystem::file_size(dest + "/video/sample.mp4"));
  EXPECT_EQ(std::filesystem::file_size(source + "/images/earth.jpg"),
            std::filesystem::file_size(dest + "/images/earth.jpg"));
  EXPECT_EQ(std::filesystem::file_size(source + "/archives/sample.zip"),
            std::filesystem::file_size(dest + "/archives/sample.zip"));
  EXPECT_EQ(std::filesystem::file_size(source + "/binaries/sample.exe"),
            std::filesystem::file_size(dest + "/binaries/sample.exe"));
  EXPECT_TRUE(FileHasPrefix(dest + "/archives/sample.zip", "PK", 2));
  EXPECT_TRUE(FileHasPrefix(dest + "/binaries/sample.exe", "MZ", 2));
  EXPECT_TRUE(FileHasPrefix(dest + "/nested/l0/l1/payload.tar.gz", "\x1f\x8b", 2));
}

TEST(MediaFixtureTest, RealArchiveAndExeRoundTrip) {
  if (!MediaFixtureAvailable()) {
    GTEST_SKIP() << "media fixture missing; run fetch_media_fixtures.ps1";
  }

  const std::string repo = test::TempDir("media_archive_repo");
  const std::string source = test::TempDir("media_archive_src");
  const std::string dest = test::TempDir("media_archive_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  ASSERT_TRUE(test::CopyFixtureTree("media", source).ok());

  BackupOptions opts{};
  opts.use_pipeline = true;
  opts.compress_mode = CompressMode::kAuto;
  opts.cpu_budget_permille = 600;

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());
  ASSERT_TRUE(engine.Verify().ok());
  ASSERT_TRUE(engine.Restore(dest).ok());
  ASSERT_TRUE(test::CompareDirectoryTrees(source, dest).ok());

  const std::string zip = dest + "/archives/sample.zip";
  const std::string exe = dest + "/binaries/sample.exe";
  const std::string tgz = dest + "/nested/l0/l1/payload.tar.gz";
  EXPECT_TRUE(FileHasPrefix(zip, "PK", 2));
  EXPECT_TRUE(FileHasPrefix(exe, "MZ", 2));
  EXPECT_TRUE(FileHasPrefix(tgz, "\x1f\x8b", 2));
  EXPECT_GT(std::filesystem::file_size(exe), 100000u);
}

TEST(MediaFixtureTest, RealMediaRecursiveComposite) {
  if (!MediaFixtureAvailable()) {
    GTEST_SKIP() << "media fixture missing; run fetch_media_fixtures.ps1";
  }

  const std::string repo = test::TempDir("media_composite_repo");
  const std::string source = test::TempDir("media_composite_src");
  const std::string dest = test::TempDir("media_composite_dest");
  const std::string bundle = test::TempDir("media_composite") + ".ebb";
  const std::string imported = test::TempDir("media_composite_import");
  const std::string import_dest = test::TempDir("media_composite_import_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  ASSERT_TRUE(test::CopyFixtureTree("media", source).ok());

  BackupOptions opts{};
  opts.use_pipeline = true;
  opts.compress_mode = CompressMode::kAuto;
  opts.cpu_budget_permille = 600;

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());

  test::WriteFile(source + "/meta.json",
                  "{\"fixture\":\"media\",\"version\":2,\"formats\":[\"jpg\",\"webp\",\"mp4\",\"gif\",\"zip\",\"tar.gz\",\"exe\"]}\n");
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental, opts).ok());

  if (std::filesystem::exists(source + "/images/demo_alt.webp")) {
    CopyFileBytes(source + "/images/demo_alt.webp", source + "/images/demo.webp");
  }
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental, opts).ok());

  test::WriteFile(source + "/meta.json",
                  "{\"fixture\":\"media\",\"version\":3,\"formats\":[\"jpg\",\"webp\",\"mp4\",\"gif\",\"zip\",\"tar.gz\",\"exe\"]}\n");
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental, opts).ok());

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
  EXPECT_TRUE(std::filesystem::exists(dest + "/video/sample.mp4"));

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
