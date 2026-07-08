#include <gtest/gtest.h>

#include "ebbackup/audit/merkle_proof.h"
#include "ebbackup/catalog/manifest_diff.h"
#include "ebbackup/engine/manifest.h"

namespace ebbackup {
namespace catalog {
namespace {

ManifestFileEntry Entry(const std::string& path, uint64_t size, char hash_char) {
  ManifestFileEntry e;
  e.relative_path = path;
  e.size = size;
  e.chunk_hashes_hex.push_back(std::string(64, hash_char));
  return e;
}

TEST(ManifestDiffTest, DetectsAddedRemovedModifiedWithProof) {
  ManifestDocument a;
  a.txn_id = 1;
  a.files = {Entry("keep.txt", 10, 'a'), Entry("old.txt", 5, 'b')};

  ManifestDocument b;
  b.txn_id = 2;
  b.files = {Entry("keep.txt", 10, 'a'), Entry("new.txt", 8, 'c'),
             Entry("old.txt", 5, 'd')};

  SnapshotDiffResult diff;
  ASSERT_TRUE(DiffManifestDocuments(a, b, DigestAlgo::kStandard, &diff).ok());
  ASSERT_EQ(diff.added.size(), 1u);
  EXPECT_EQ(diff.added[0].path, "new.txt");
  EXPECT_FALSE(diff.added[0].subset_merkle_hex.empty());
  // Single-chunk file: proof steps may be empty (leaf == root).
  ASSERT_EQ(diff.modified.size(), 1u);
  EXPECT_EQ(diff.modified[0].path, "old.txt");
  EXPECT_FALSE(diff.diff_merkle_root_hex.empty());
  EXPECT_FALSE(diff.diff_merkle_proof.empty());

  const std::string leaf = std::string(64, 'c');
  EXPECT_TRUE(audit::VerifyMerkleProof(
                  leaf, diff.added[0].subset_merkle_hex,
                  diff.added[0].merkle_proof, DigestAlgo::kStandard)
                  .ok());
  const std::string mod_leaf = std::string(64, 'd');
  EXPECT_TRUE(audit::VerifyMerkleProof(
                  mod_leaf, diff.modified[0].subset_merkle_hex,
                  diff.modified[0].merkle_proof, DigestAlgo::kStandard)
                  .ok());
}

}  // namespace
}  // namespace catalog
}  // namespace ebbackup
