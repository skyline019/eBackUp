#include <gtest/gtest.h>

#include <vector>

#include <filesystem>

#include "ebbackup/audit/merkle.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/store/chunk_store.h"
#include "test_util.h"

namespace ebbackup {
namespace audit {
namespace {

TEST(MerkleTest, SingleChunk) {
  ManifestDocument doc;
  doc.txn_id = 1;
  ManifestFileEntry file;
  file.relative_path = "a.bin";
  file.size = 10;
  file.chunk_hashes_hex = {Sha256Hex(
      reinterpret_cast<const uint8_t*>("0123456789"), 10)};
  doc.files.push_back(file);

  uint8_t root[32];
  ASSERT_TRUE(ComputeMerkleRoot(doc, root).ok());
  uint8_t expected[32];
  ASSERT_TRUE(ComputeMerkleRootFromHashes(file.chunk_hashes_hex, expected).ok());
  EXPECT_EQ(std::memcmp(root, expected, 32), 0);
}

TEST(MerkleTest, SortedFileOrder) {
  ManifestDocument doc;
  ManifestFileEntry b;
  b.relative_path = "b.bin";
  b.chunk_hashes_hex = {std::string(64, 'b')};
  ManifestFileEntry a;
  a.relative_path = "a.bin";
  a.chunk_hashes_hex = {std::string(64, 'a')};
  doc.files = {b, a};

  uint8_t root1[32];
  uint8_t root2[32];
  ASSERT_TRUE(ComputeMerkleRoot(doc, root1).ok());
  doc.files = {a, b};
  ASSERT_TRUE(ComputeMerkleRoot(doc, root2).ok());
  EXPECT_EQ(std::memcmp(root1, root2, 32), 0);
}

TEST(MerkleTest, SubsetFilesRoot) {
  ManifestFileEntry a;
  a.relative_path = "a.bin";
  a.chunk_hashes_hex = {std::string(64, 'a')};
  ManifestFileEntry b;
  b.relative_path = "b.bin";
  b.chunk_hashes_hex = {std::string(64, 'b')};
  const std::vector<ManifestFileEntry> subset = {a};

  uint8_t full_root[32];
  uint8_t subset_root[32];
  ManifestDocument doc;
  doc.files = {a, b};
  ASSERT_TRUE(ComputeMerkleRoot(doc, full_root).ok());
  ASSERT_TRUE(ComputeMerkleRootForFiles(subset, subset_root).ok());
  EXPECT_NE(std::memcmp(full_root, subset_root, 32), 0);

  ManifestDocument single;
  single.files = {a};
  uint8_t single_root[32];
  ASSERT_TRUE(ComputeMerkleRoot(single, single_root).ok());
  EXPECT_EQ(std::memcmp(subset_root, single_root, 32), 0);
}

TEST(MerkleTest, VerifyRestoredFileChunksRoundTrip) {
  const std::string repo = ebbackup::test::TempDir("merkle_verify_repo");
  const std::string source = ebbackup::test::TempDir("merkle_verify_source");
  const std::string dest = ebbackup::test::TempDir("merkle_verify_dest");
  ASSERT_TRUE(ebbackup::test::InitDefaultRepo(repo).ok());
  ebbackup::test::WriteFile(source + "/file.bin",
                            ebbackup::test::MakeSyntheticData(2048, 9));

  ebbackup::BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  ASSERT_TRUE(engine.Restore(dest).ok());

  ManifestDocument doc;
  ASSERT_TRUE(ReadManifestAuto(repo + "/manifest", &doc).ok());
  const ManifestFileEntry* file = ebbackup::test::FindManifestFile(doc, "file.bin");
  ASSERT_NE(file, nullptr);
  const std::string restored =
      (std::filesystem::path(dest) / file->relative_path).string();
  EXPECT_TRUE(
      VerifyRestoredFileChunks(restored, *file, engine.chunk_store()).ok());
}

TEST(MerkleTest, VerifyRestoredFileChunksLargeFile) {
  const std::string repo = ebbackup::test::TempDir("merkle_large_repo");
  const std::string source = ebbackup::test::TempDir("merkle_large_source");
  const std::string dest = ebbackup::test::TempDir("merkle_large_dest");
  ASSERT_TRUE(ebbackup::test::InitDefaultRepo(repo).ok());
  ebbackup::test::WriteFile(source + "/large.bin",
                            ebbackup::test::MakeSyntheticData(5 * 1024 * 1024, 17));

  ebbackup::BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  ASSERT_TRUE(engine.Restore(dest).ok());

  ManifestDocument doc;
  ASSERT_TRUE(ReadManifestAuto(repo + "/manifest", &doc).ok());
  const ManifestFileEntry* file = ebbackup::test::FindManifestFile(doc, "large.bin");
  ASSERT_NE(file, nullptr);
  EXPECT_GE(file->chunk_hashes_hex.size(), 2u);
  const std::string restored =
      (std::filesystem::path(dest) / file->relative_path).string();
  EXPECT_TRUE(
      VerifyRestoredFileChunks(restored, *file, engine.chunk_store()).ok());
}

}  // namespace
}  // namespace audit
}  // namespace ebbackup
