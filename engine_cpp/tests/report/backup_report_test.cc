#include <gtest/gtest.h>

#include "ebbackup/report/backup_report.h"
#include "test_util.h"

namespace ebbackup {
namespace report {
namespace {

TEST(BackupReportTest, PopulateIssueCounts) {
  BackupReport report{};
  report.issues = {
      {"a.txt", "locked"},
      {"b.txt", "permission_denied"},
      {"c.txt", "unreadable"},
      {"junction", "reparse_junction"},
      {"", "hook_failed:pre:1"},
      {"d.txt", "depth_exceeded"},
  };
  PopulateReportIssueCounts(&report);
  EXPECT_EQ(report.locked, 1u);
  EXPECT_EQ(report.permission_denied, 1u);
  EXPECT_EQ(report.reparse_junction, 1u);
  EXPECT_EQ(report.hook_failed, 1u);
  EXPECT_EQ(report.skipped, 4u);
}

TEST(BackupReportTest, PopulatePluginIssueCounts) {
  BackupReport report{};
  report.issues = {
      {"", "plugin_skipped:platform:vhdx_scan"},
      {"", "plugin_unknown:custom"},
      {"", "plugin_quiesce_failed:sqlite_checkpoint:busy"},
  };
  PopulateReportIssueCounts(&report);
  EXPECT_EQ(report.plugin_skipped, 2u);
  EXPECT_EQ(report.plugin_failed, 1u);
  EXPECT_EQ(report.skipped, 3u);
}

TEST(BackupReportTest, RoundTripJsonWithPlugins) {
  BackupReport report{};
  report.txn_id = 55;
  report.backed_up = 2;
  report.plugins = {
      R"({"id":"sqlite_checkpoint","checkpointed":1,"failed":0})",
      R"({"id":"registry_hive","exported":2,"failed":0})",
  };
  report.issues.push_back({"", "plugin_skipped:platform:vhdx_scan"});
  PopulateReportIssueCounts(&report);

  const std::string json = BackupReportToJson(report);
  BackupReport loaded{};
  ASSERT_TRUE(ParseBackupReportJson(json, &loaded).ok());
  ASSERT_EQ(loaded.plugins.size(), 2u);
  EXPECT_NE(loaded.plugins[0].find("sqlite_checkpoint"), std::string::npos);
  EXPECT_NE(loaded.plugins[1].find("registry_hive"), std::string::npos);
  EXPECT_EQ(loaded.plugin_skipped, 1u);
}

TEST(BackupReportTest, RoundTripJsonWithExtendedCounts) {
  BackupReport report{};
  report.txn_id = 99;
  report.backed_up = 5;
  report.skipped = 3;
  report.locked = 1;
  report.permission_denied = 0;
  report.reparse_junction = 1;
  report.hook_failed = 1;
  report.issues.push_back({"j", "reparse_junction"});
  report.issues.push_back({"", "hook_failed:post:2"});

  const std::string json = BackupReportToJson(report);
  BackupReport loaded{};
  ASSERT_TRUE(ParseBackupReportJson(json, &loaded).ok());
  EXPECT_EQ(loaded.reparse_junction, 1u);
  EXPECT_EQ(loaded.hook_failed, 1u);
}

TEST(BackupReportTest, RoundTripJson) {
  BackupReport report{};
  report.txn_id = 42;
  report.backed_up = 10;
  report.skipped = 2;
  report.locked = 1;
  report.permission_denied = 1;
  report.chunks_written = 5;
  report.chunks_reused = 3;
  report.bytes_processed = 4096;
  report.reuse_pct = 37.5;
  report.issues.push_back({"path/with space.txt", "locked"});
  report.issues.push_back({"nested\\dir", "permission_denied"});

  const std::string json = BackupReportToJson(report);
  BackupReport loaded{};
  ASSERT_TRUE(ParseBackupReportJson(json, &loaded).ok());
  EXPECT_EQ(loaded.txn_id, 42u);
  EXPECT_EQ(loaded.backed_up, 10u);
  EXPECT_EQ(loaded.skipped, 2u);
  EXPECT_EQ(loaded.locked, 1u);
  EXPECT_EQ(loaded.permission_denied, 1u);
  EXPECT_EQ(loaded.issues.size(), 2u);
  EXPECT_EQ(loaded.issues[0].path, "path/with space.txt");
  EXPECT_EQ(loaded.issues[0].reason, "locked");
}

TEST(BackupReportTest, WriteAndLoadFromRepo) {
  const std::string repo = test::TempDir("report_repo");
  BackupReport report{};
  report.txn_id = 7;
  report.backed_up = 3;
  report.issues.push_back({"missing.txt", "unreadable"});
  ASSERT_TRUE(WriteBackupReport(repo, report).ok());

  BackupReport loaded{};
  ASSERT_TRUE(LoadBackupReport(repo, 7, &loaded).ok());
  EXPECT_EQ(loaded.txn_id, 7u);
  EXPECT_EQ(loaded.issues.size(), 1u);
  EXPECT_EQ(loaded.issues[0].reason, "unreadable");
}

}  // namespace
}  // namespace report
}  // namespace ebbackup
