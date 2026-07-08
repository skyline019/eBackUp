#include <gtest/gtest.h>

#include "ebbackup/catalog/path_index.h"
#include "ebbackup/engine/manifest.h"
#include "test_util.h"

namespace ebbackup {
namespace catalog {
namespace {

ManifestDocument MakeDoc(uint64_t txn, const std::string& path, uint64_t size) {
  ManifestDocument doc;
  doc.txn_id = txn;
  ManifestFileEntry entry;
  entry.relative_path = path;
  entry.size = size;
  entry.chunk_hashes_hex.push_back(std::string(64, 'a'));
  doc.files.push_back(std::move(entry));
  return doc;
}

TEST(PathIndexTest, AppendAndQueryHistory) {
  const std::string repo = test::TempDir("path_idx_repo");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  const ManifestDocument doc1 = MakeDoc(1, "docs/readme.txt", 100);
  const ManifestDocument doc2 = MakeDoc(2, "docs/readme.txt", 200);
  ASSERT_TRUE(AppendManifestToPathIndex(repo, doc1).ok());
  ASSERT_TRUE(AppendManifestToPathIndex(repo, doc2).ok());

  std::vector<PathIndexRecord> history;
  ASSERT_TRUE(QueryPathHistory(repo, "docs/readme.txt", &history).ok());
  ASSERT_EQ(history.size(), 2u);
  EXPECT_EQ(history[0].txn_id, 1u);
  EXPECT_EQ(history[1].txn_id, 2u);
  EXPECT_EQ(history[1].size, 200u);
}

TEST(PathIndexTest, ManifestPagePrefix) {
  ManifestDocument doc;
  doc.txn_id = 3;
  ManifestFileEntry a;
  a.relative_path = "src/main.cc";
  a.size = 10;
  ManifestFileEntry b;
  b.relative_path = "src/util.cc";
  b.size = 20;
  ManifestFileEntry c;
  c.relative_path = "README.md";
  c.size = 5;
  doc.files = {a, b, c};

  ManifestFilePage page;
  ASSERT_TRUE(ListManifestFilesPage(doc, "src/", 0, 10, &page).ok());
  EXPECT_EQ(page.total, 2u);
  EXPECT_EQ(page.files.size(), 2u);
}

}  // namespace
}  // namespace catalog
}  // namespace ebbackup
