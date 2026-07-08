#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include "ebbackup/catalog/manifest_browse_index.h"
#include "ebbackup/engine/manifest.h"
#include "test_util.h"

namespace ebbackup {
namespace catalog {
namespace {

std::vector<ManifestBrowseRecord> MakeSyntheticRecords(size_t count,
                                                      const std::string& prefix) {
  std::vector<ManifestBrowseRecord> records;
  records.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    ManifestBrowseRecord rec;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s%06zu.txt", prefix.c_str(), i);
    rec.relative_path = buf;
    rec.size = i;
    rec.file_type = FileType::kRegular;
    rec.mtime_unix = static_cast<int64_t>(i);
    rec.chunk_count = static_cast<uint32_t>(i % 7);
    records.push_back(std::move(rec));
  }
  return records;
}

TEST(ManifestBrowseIndexTest, EmptyPrefixListsAll) {
  const std::string repo = test::TempDir("mbi_empty_prefix_repo");
  const auto records = MakeSyntheticRecords(12, "src/");
  ASSERT_TRUE(WriteManifestBrowseIndex(repo, 5, records).ok());

  ManifestFilePage page;
  ASSERT_TRUE(QueryManifestBrowsePage(repo, 5, "", 0, 50, &page).ok());
  EXPECT_EQ(page.total, 12u);
  EXPECT_EQ(page.files.size(), 12u);
}

TEST(ManifestBrowseIndexTest, PrefixPaging10k) {
  const std::string repo = test::TempDir("mbi_10k_repo");
  const auto records = MakeSyntheticRecords(10000, "src/");
  ASSERT_TRUE(WriteManifestBrowseIndex(repo, 42, records).ok());

  ManifestFilePage page;
  ASSERT_TRUE(QueryManifestBrowsePage(repo, 42, "src/000", 0, 10, &page).ok());
  EXPECT_EQ(page.txn_id, 42u);
  EXPECT_EQ(page.total, 1000u);
  EXPECT_EQ(page.files.size(), 10u);
  EXPECT_EQ(page.files.front().relative_path, "src/000000.txt");
}

TEST(ManifestBrowseIndexTest, OffsetLimit100k) {
  const std::string repo = test::TempDir("mbi_100k_repo");
  const auto records = MakeSyntheticRecords(100000, "data/");
  ASSERT_TRUE(WriteManifestBrowseIndex(repo, 7, records).ok());

  ManifestFilePage page;
  ASSERT_TRUE(QueryManifestBrowsePage(repo, 7, "data/", 50000, 25, &page).ok());
  EXPECT_EQ(page.total, 100000u);
  EXPECT_EQ(page.offset, 50000u);
  EXPECT_EQ(page.files.size(), 25u);
  EXPECT_EQ(page.files.front().relative_path, "data/050000.txt");
}

TEST(ManifestBrowseIndexTest, StreamRebuildMatchesWrite) {
  const std::string repo = test::TempDir("mbi_stream_repo");
  ManifestDocument doc;
  doc.txn_id = 99;
  for (int i = 0; i < 100; ++i) {
    ManifestFileEntry f;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "f/%03d.dat", i);
    f.relative_path = buf;
    f.size = static_cast<uint64_t>(i);
    f.mtime_unix = i;
    f.chunk_hashes_hex = {
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"};
    doc.files.push_back(std::move(f));
  }
  const std::string manifest_path = repo + "/manifest.bin";
  ASSERT_TRUE(WriteManifestV4(manifest_path, doc).ok());

  const std::string index_path = ManifestBrowseIndexPath(repo, doc.txn_id);
  ASSERT_TRUE(BuildManifestBrowseIndexFromFile(manifest_path, doc.txn_id, index_path).ok());

  ManifestFilePage page;
  ASSERT_TRUE(QueryManifestBrowsePage(repo, doc.txn_id, "f/01", 0, 5, &page).ok());
  EXPECT_EQ(page.total, 10u);
  EXPECT_EQ(page.files.size(), 5u);
  EXPECT_EQ(page.files.front().browse_chunk_count, 2u);
}

TEST(ManifestBrowseIndexTest, Browse100kPageUnder50ms) {
  const std::string repo = test::TempDir("mbi_perf_repo");
  const auto records = MakeSyntheticRecords(100000, "perf/");
  ASSERT_TRUE(WriteManifestBrowseIndex(repo, 1, records).ok());

  const auto t0 = std::chrono::steady_clock::now();
  ManifestFilePage page;
  ASSERT_TRUE(QueryManifestBrowsePage(repo, 1, "perf/050", 100, 50, &page).ok());
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
  EXPECT_EQ(page.files.size(), 50u);
  EXPECT_LT(ms, 50);
}

}  // namespace
}  // namespace catalog
}  // namespace ebbackup
