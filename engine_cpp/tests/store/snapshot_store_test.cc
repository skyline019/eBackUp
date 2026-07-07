#include <gtest/gtest.h>

#include <filesystem>

#include "ebbackup/engine/manifest.h"
#include "ebbackup/store/snapshot_store.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(SnapshotStoreTest, ArchiveLoadDeleteRoundtrip) {
  const std::string repo = test::TempDir("snap_store");
  std::filesystem::create_directories(repo + "/snapshots");

  ManifestDocument doc{};
  doc.txn_id = 7;
  doc.files.push_back(ManifestFileEntry{});
  doc.files.back().relative_path = "a.txt";
  doc.files.back().size = 5;
  doc.files.back().chunk_hashes_hex.push_back(std::string(64, 'a'));

  const std::string manifest_path = repo + "/manifest";
  ASSERT_TRUE(WriteManifestV4(manifest_path, doc).ok());

  uint8_t merkle[32]{};
  merkle[0] = 0xAB;
  ASSERT_TRUE(
      ArchiveSnapshot(repo, 7, manifest_path, 1700000000, 0x12345678, merkle,
                      static_cast<uint32_t>(doc.files.size()))
          .ok());

  std::vector<SnapshotEntry> entries;
  ASSERT_TRUE(ListSnapshots(repo, &entries).ok());
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].txn_id, 7u);
  EXPECT_EQ(entries[0].file_count, 1u);
  EXPECT_TRUE(std::filesystem::exists(SnapshotManifestPath(repo, 7)));

  ManifestDocument loaded{};
  ASSERT_TRUE(LoadSnapshotManifest(repo, 7, &loaded).ok());
  EXPECT_EQ(loaded.txn_id, 7u);
  EXPECT_EQ(loaded.files.size(), 1u);

  ASSERT_TRUE(DeleteSnapshot(repo, 7).ok());
  entries.clear();
  ASSERT_TRUE(ListSnapshots(repo, &entries).ok());
  EXPECT_TRUE(entries.empty());
  EXPECT_FALSE(std::filesystem::exists(SnapshotManifestPath(repo, 7)));
}

TEST(SnapshotStoreTest, IndexSaveLoadRoundtrip) {
  const std::string repo = test::TempDir("snap_idx");
  std::vector<SnapshotEntry> entries;
  SnapshotEntry a{};
  a.txn_id = 3;
  a.created_at_unix = 100;
  a.manifest_rel_path = SnapshotManifestRelPath(3);
  entries.push_back(a);
  SnapshotEntry b = a;
  b.txn_id = 5;
  b.created_at_unix = 200;
  b.manifest_rel_path = SnapshotManifestRelPath(5);
  entries.push_back(b);

  ASSERT_TRUE(SaveSnapshotIndex(repo, entries).ok());
  std::vector<SnapshotEntry> loaded;
  ASSERT_TRUE(LoadSnapshotIndex(repo, &loaded).ok());
  ASSERT_EQ(loaded.size(), 2u);
  EXPECT_EQ(loaded[0].txn_id, 3u);
  EXPECT_EQ(loaded[1].txn_id, 5u);
}

}  // namespace
}  // namespace ebbackup
