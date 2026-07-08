#include <gtest/gtest.h>

#include <filesystem>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/report/backup_report.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(BackupReportIntegrationTest, PipelineBackupWritesReport) {
  const std::string repo = test::TempDir("report_int_repo");
  const std::string source = test::TempDir("report_int_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  std::error_code ec;
  std::filesystem::create_directories(source + "/open", ec);
  test::WriteFile(source + "/open/readable.txt", "ok-content");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  BackupOptions opts{};
  opts.use_pipeline = true;
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());

  ManifestDocument doc;
  ASSERT_TRUE(engine.LoadManifest(0, &doc).ok());
  report::BackupReport br{};
  ASSERT_TRUE(report::LoadBackupReport(repo, doc.txn_id, &br).ok());
  EXPECT_EQ(br.txn_id, doc.txn_id);
  EXPECT_GE(br.backed_up, 1u);
}

TEST(BackupReportIntegrationTest, DeepTreeStillCompletesWithReport) {
  const std::string repo = test::TempDir("report_depth_repo");
  const std::string source = test::TempDir("report_depth_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  std::error_code ec;
  std::string path = source;
  for (int i = 0; i < 66; ++i) {
    path += "/a";
    std::filesystem::create_directories(path, ec);
    if (ec) GTEST_SKIP() << "cannot create deep tree";
  }
  test::WriteFile(path + "/leaf.txt", "deep");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  BackupOptions opts{};
  opts.use_pipeline = true;
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());

  ManifestDocument doc;
  ASSERT_TRUE(engine.LoadManifest(0, &doc).ok());
  report::BackupReport br{};
  ASSERT_TRUE(report::LoadBackupReport(repo, doc.txn_id, &br).ok());
  bool saw_depth = false;
  for (const auto& issue : br.issues) {
    if (issue.reason == "depth_exceeded") saw_depth = true;
  }
  EXPECT_TRUE(saw_depth);
}

}  // namespace
}  // namespace ebbackup
