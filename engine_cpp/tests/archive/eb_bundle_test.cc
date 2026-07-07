#include <gtest/gtest.h>

#include <fstream>

#include "ebbackup/archive/eb_bundle.h"
#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(EbBundleTest, ExportImportVerifyRestore) {
  const std::string repo = test::TempDir("bundle_repo");
  const std::string imported = test::TempDir("bundle_imported");
  const std::string source = test::TempDir("bundle_source");
  const std::string dest = test::TempDir("bundle_dest");
  const std::string bundle = test::TempDir("bundle_out") + "/backup.ebb";
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/bundle.txt", "bundle-data");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  ASSERT_TRUE(engine.Verify().ok());
  ASSERT_TRUE(ExportRepoToBundle(repo, bundle).ok());

  std::filesystem::remove_all(repo);
  ASSERT_TRUE(ImportBundleToRepo(bundle, imported).ok());

  BackupEngine imported_engine(imported);
  ASSERT_TRUE(imported_engine.Open().ok());
  ASSERT_TRUE(imported_engine.Verify().ok());
  ASSERT_TRUE(imported_engine.Restore(dest).ok());

  std::ifstream in(dest + "/bundle.txt");
  std::string got((std::istreambuf_iterator<char>(in)),
                  std::istreambuf_iterator<char>());
  EXPECT_EQ(got, "bundle-data");
}

}  // namespace
}  // namespace ebbackup
