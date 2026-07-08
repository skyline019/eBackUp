#include <gtest/gtest.h>

#include <filesystem>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/report/backup_report.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(PipelineSkipIssuesTest, UnreadableFileDoesNotAbortJob) {
  const std::string repo = test::TempDir("pipe_skip_repo");
  const std::string source = test::TempDir("pipe_skip_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  std::error_code ec;
  std::filesystem::create_directories(source + "/good", ec);
  test::WriteFile(source + "/good/file.txt", "good-data");

#ifndef _WIN32
  std::filesystem::create_directories(source + "/secret", ec);
  test::WriteFile(source + "/secret/hidden.txt", "hidden");
  std::filesystem::permissions(source + "/secret", std::filesystem::perms::none, ec);
  if (ec) GTEST_SKIP() << "chmod not supported";
#endif

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  BackupOptions opts{};
  opts.use_pipeline = true;
  const Status st = engine.RunBackup(source, BackupMode::kFull, opts);
  ASSERT_TRUE(st.ok()) << st.message();

  ManifestDocument doc;
  ASSERT_TRUE(engine.LoadManifest(0, &doc).ok());
  report::BackupReport br{};
  ASSERT_TRUE(report::LoadBackupReport(repo, doc.txn_id, &br).ok());
  EXPECT_GE(br.backed_up, 1u);
}

}  // namespace
}  // namespace ebbackup
