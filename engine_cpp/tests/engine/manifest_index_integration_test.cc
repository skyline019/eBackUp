#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "ebbackup/catalog/manifest_browse_index.h"
#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(ManifestIndexIntegrationTest, BackupWritesSidecarAndPagesFromIndex) {
  const std::string repo = test::TempDir("mbi_int_repo");
  const std::string source = test::TempDir("mbi_int_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  std::error_code ec;
  std::filesystem::create_directories(source + "/alpha", ec);
  std::filesystem::create_directories(source + "/beta", ec);
  test::WriteFile(source + "/alpha/a.txt", "aaa");
  test::WriteFile(source + "/beta/b.txt", "bbb");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  BackupOptions opts{};
  opts.use_pipeline = true;
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());

  ManifestDocument doc;
  ASSERT_TRUE(engine.LoadManifest(0, &doc).ok());
  const std::string index_path =
      catalog::ManifestBrowseIndexPath(repo, doc.txn_id);
  EXPECT_TRUE(std::filesystem::exists(index_path));

  const std::string json =
      engine.ListManifestFilesPageJson(doc.txn_id, "alpha/", 0, 10);
  EXPECT_NE(json.find("\"index_source\":\"sidecar\""), std::string::npos);
  EXPECT_NE(json.find("alpha/a.txt"), std::string::npos);
}

}  // namespace
}  // namespace ebbackup
