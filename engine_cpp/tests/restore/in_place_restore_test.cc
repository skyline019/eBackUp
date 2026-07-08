#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/restore/in_place_restore.h"
#include "test_util.h"

namespace ebbackup {
namespace {

restore::InPlacePreviewEntry* FindEntry(restore::InPlacePreviewReport* report,
                                const std::string& path) {
  for (auto& e : report->entries) {
    if (e.path == path) return &e;
  }
  return nullptr;
}

TEST(InPlaceRestoreTest, UnchangedAfterBackup) {
  const std::string repo = test::TempDir("inplace_repo");
  const std::string source = test::TempDir("inplace_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/data.txt", "hello-world");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  const uint64_t txn = engine.superblock().critical.txn_id;

  restore::InPlacePreviewReport report{};
  RestoreOptions opts{};
  restore::InPlacePreviewOptions preview_opts{};
  ASSERT_TRUE(
      engine.PreviewInPlaceRestore(txn, source, opts, preview_opts, &report).ok());
  auto* entry = FindEntry(&report, "data.txt");
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->action, "unchanged");
  EXPECT_GE(report.summary.unchanged_count, 1u);
}

TEST(InPlaceRestoreTest, ModifyAfterEdit) {
  const std::string repo = test::TempDir("inplace_mod_repo");
  const std::string source = test::TempDir("inplace_mod_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/data.txt", "original");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  const uint64_t txn = engine.superblock().critical.txn_id;

  test::WriteFile(source + "/data.txt", "modified-content");

  restore::InPlacePreviewReport report{};
  RestoreOptions opts{};
  restore::InPlacePreviewOptions preview_opts{};
  ASSERT_TRUE(
      engine.PreviewInPlaceRestore(txn, source, opts, preview_opts, &report).ok());
  auto* entry = FindEntry(&report, "data.txt");
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->action, "modify");
  EXPECT_GE(report.summary.modify_count, 1u);
  EXPECT_GT(report.summary.bytes_to_write, 0u);
}

TEST(InPlaceRestoreTest, AddAfterDelete) {
  const std::string repo = test::TempDir("inplace_add_repo");
  const std::string source = test::TempDir("inplace_add_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/gone.txt", "will-delete");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  const uint64_t txn = engine.superblock().critical.txn_id;

  std::filesystem::remove(source + "/gone.txt");

  restore::InPlacePreviewReport report{};
  RestoreOptions opts{};
  restore::InPlacePreviewOptions preview_opts{};
  ASSERT_TRUE(
      engine.PreviewInPlaceRestore(txn, source, opts, preview_opts, &report).ok());
  auto* entry = FindEntry(&report, "gone.txt");
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->action, "add");
  EXPECT_GE(report.summary.add_count, 1u);
}

TEST(InPlaceRestoreTest, TypeConflict) {
  const std::string repo = test::TempDir("inplace_conflict_repo");
  const std::string source = test::TempDir("inplace_conflict_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/slot.txt", "file-content");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  const uint64_t txn = engine.superblock().critical.txn_id;

  std::filesystem::remove(source + "/slot.txt");
  std::filesystem::create_directories(source + "/slot.txt");

  restore::InPlacePreviewReport report{};
  RestoreOptions opts{};
  restore::InPlacePreviewOptions preview_opts{};
  ASSERT_TRUE(
      engine.PreviewInPlaceRestore(txn, source, opts, preview_opts, &report).ok());
  auto* entry = FindEntry(&report, "slot.txt");
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->action, "conflict");
  EXPECT_EQ(entry->reason, "type_mismatch");
}

TEST(InPlaceRestoreTest, FilterIncludePaths) {
  const std::string repo = test::TempDir("inplace_filter_repo");
  const std::string source = test::TempDir("inplace_filter_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/keep/a.txt", "keep-me");
  test::WriteFile(source + "/drop/b.txt", "drop-me");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  const uint64_t txn = engine.superblock().critical.txn_id;

  restore::InPlacePreviewReport report{};
  RestoreOptions opts{};
  opts.filter.include_paths = {"keep"};
  restore::InPlacePreviewOptions preview_opts{};
  ASSERT_TRUE(
      engine.PreviewInPlaceRestore(txn, source, opts, preview_opts, &report).ok());
  EXPECT_NE(FindEntry(&report, "keep/a.txt"), nullptr);
  EXPECT_EQ(FindEntry(&report, "drop/b.txt"), nullptr);
}

TEST(InPlaceRestoreTest, ApplyModifyRestoresContent) {
  const std::string repo = test::TempDir("inplace_apply_repo");
  const std::string source = test::TempDir("inplace_apply_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/data.txt", "original");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  const uint64_t txn = engine.superblock().critical.txn_id;

  test::WriteFile(source + "/data.txt", "modified-content");

  restore::InPlaceApplyOptions apply_opts{};
  restore::InPlaceApplyReport report{};
  RestoreOptions opts{};
  restore::InPlacePreviewOptions preview_opts{};
  ASSERT_TRUE(
      engine.ApplyInPlaceRestore(txn, source, opts, preview_opts, apply_opts, &report).ok());
  EXPECT_GE(report.summary.applied_count, 1u);
  EXPECT_GE(report.summary.modify_count, 1u);

  std::ifstream in(source + "/data.txt");
  std::string content;
  std::getline(in, content);
  EXPECT_EQ(content, "original");
}

TEST(InPlaceRestoreTest, ApplyConflictFailAborts) {
  const std::string repo = test::TempDir("inplace_fail_repo");
  const std::string source = test::TempDir("inplace_fail_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/slot.txt", "file-content");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  const uint64_t txn = engine.superblock().critical.txn_id;

  std::filesystem::remove(source + "/slot.txt");
  std::filesystem::create_directories(source + "/slot.txt");

  restore::InPlaceApplyOptions apply_opts{};
  apply_opts.conflict = restore::InPlaceConflictPolicy::kFail;
  restore::InPlaceApplyReport report{};
  RestoreOptions opts{};
  restore::InPlacePreviewOptions preview_opts{};
  const Status st = engine.ApplyInPlaceRestore(txn, source, opts, preview_opts, apply_opts, &report);
  EXPECT_FALSE(st.ok());
}

TEST(InPlaceRestoreTest, ApplyJsonRoundTrip) {
  restore::InPlaceApplyReport report{};
  report.txn_id = 7;
  report.target_root = "/data";
  report.summary.applied_count = 2;
  report.summary.skipped_count = 1;
  report.entries.push_back({"a.txt", "add", ""});
  const std::string json = restore::InPlaceApplyReportToJson(report);
  EXPECT_NE(json.find("\"applied_count\":2"), std::string::npos);
  EXPECT_NE(json.find("\"skipped_count\":1"), std::string::npos);
}

TEST(InPlaceRestoreTest, JsonRoundTrip) {
  restore::InPlacePreviewReport report{};
  report.txn_id = 42;
  report.target_root = "C:/data";
  report.summary.add_count = 1;
  report.summary.modify_count = 2;
  report.entries.push_back({"a.txt", "add", ""});
  report.entries.push_back({"b.txt", "modify", ""});
  const std::string json = restore::InPlacePreviewReportToJson(report);
  EXPECT_NE(json.find("\"txn_id\":42"), std::string::npos);
  EXPECT_NE(json.find("\"add_count\":1"), std::string::npos);
  EXPECT_NE(json.find("\"action\":\"modify\""), std::string::npos);
}

TEST(InPlaceRestoreTest, OrphanPreviewAndDelete) {
  const std::string repo = test::TempDir("inplace_orphan_repo");
  const std::string source = test::TempDir("inplace_orphan_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/tracked.txt", "tracked");
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  const uint64_t txn = engine.superblock().critical.txn_id;
  test::WriteFile(source + "/orphan.txt", "extra");

  RestoreOptions opts{};
  restore::InPlacePreviewOptions preview_opts{};
  restore::InPlacePreviewReport preview{};
  ASSERT_TRUE(engine.PreviewInPlaceRestore(txn, source, opts, preview_opts, &preview).ok());
  EXPECT_GE(preview.summary.orphan_count, 1u);

  restore::InPlaceApplyOptions apply_opts{};
  apply_opts.orphan = restore::InPlaceOrphanPolicy::kDelete;
  restore::InPlaceApplyReport report{};
  ASSERT_TRUE(
      engine.ApplyInPlaceRestore(txn, source, opts, preview_opts, apply_opts, &report).ok());
  EXPECT_GE(report.summary.orphan_deleted_count, 1u);
  EXPECT_FALSE(std::filesystem::exists(source + "/orphan.txt"));
  EXPECT_TRUE(std::filesystem::exists(source + "/tracked.txt"));
}

TEST(InPlaceRestoreTest, ThreeWayBothChanged) {
  const std::string repo = test::TempDir("inplace_3way_repo");
  const std::string source = test::TempDir("inplace_3way_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/data.txt", "base-content");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull).ok());
  const uint64_t base_txn = engine.superblock().critical.txn_id;

  test::WriteFile(source + "/data.txt", "target-content");
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental).ok());
  const uint64_t target_txn = engine.superblock().critical.txn_id;

  test::WriteFile(source + "/data.txt", "live-content");

  RestoreOptions opts{};
  restore::InPlacePreviewOptions preview_opts{};
  preview_opts.base_txn_id = base_txn;
  restore::InPlacePreviewReport report{};
  ASSERT_TRUE(
      engine.PreviewInPlaceRestore(target_txn, source, opts, preview_opts, &report).ok());
  EXPECT_TRUE(report.three_way);
  EXPECT_EQ(report.base_txn_id, base_txn);
  auto* entry = FindEntry(&report, "data.txt");
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->action, "both_changed");
  EXPECT_EQ(entry->reason, "both_changed");
  EXPECT_EQ(entry->base_action, "changed");
  EXPECT_EQ(entry->live_state, "diverged");
  EXPECT_GE(report.summary.both_changed_count, 1u);
}

TEST(InPlaceRestoreTest, ThreeWayModifyWhenLiveMatchesBase) {
  const std::string repo = test::TempDir("inplace_3way_mod_repo");
  const std::string source = test::TempDir("inplace_3way_mod_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/data.txt", "base-content");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull).ok());
  const uint64_t base_txn = engine.superblock().critical.txn_id;

  test::WriteFile(source + "/data.txt", "target-content");
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental).ok());
  const uint64_t target_txn = engine.superblock().critical.txn_id;

  test::WriteFile(source + "/data.txt", "base-content");

  RestoreOptions opts{};
  restore::InPlacePreviewOptions preview_opts{};
  preview_opts.base_txn_id = base_txn;
  restore::InPlacePreviewReport report{};
  ASSERT_TRUE(
      engine.PreviewInPlaceRestore(target_txn, source, opts, preview_opts, &report).ok());
  auto* entry = FindEntry(&report, "data.txt");
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->action, "modify");
  EXPECT_EQ(entry->base_action, "changed");
  EXPECT_EQ(entry->live_state, "matches_base");
}

TEST(InPlaceRestoreTest, OverwriteConflictRestores) {
  const std::string repo = test::TempDir("inplace_over_repo");
  const std::string source = test::TempDir("inplace_over_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/slot.txt", "file-content");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  const uint64_t txn = engine.superblock().critical.txn_id;

  std::filesystem::remove(source + "/slot.txt");
  std::filesystem::create_directories(source + "/slot.txt");

  restore::InPlaceApplyOptions apply_opts{};
  apply_opts.conflict = restore::InPlaceConflictPolicy::kOverwrite;
  restore::InPlaceApplyReport report{};
  RestoreOptions opts{};
  restore::InPlacePreviewOptions preview_opts{};
  ASSERT_TRUE(
      engine.ApplyInPlaceRestore(txn, source, opts, preview_opts, apply_opts, &report).ok());
  EXPECT_GE(report.summary.overwritten_count, 1u);
  EXPECT_TRUE(std::filesystem::is_regular_file(source + "/slot.txt"));
}

TEST(InPlaceRestoreTest, DryRunDoesNotWrite) {
  const std::string repo = test::TempDir("inplace_dry_repo");
  const std::string source = test::TempDir("inplace_dry_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/data.txt", "original");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  const uint64_t txn = engine.superblock().critical.txn_id;

  test::WriteFile(source + "/data.txt", "modified-content");

  restore::InPlaceApplyOptions apply_opts{};
  apply_opts.dry_run = true;
  restore::InPlaceApplyReport report{};
  RestoreOptions opts{};
  restore::InPlacePreviewOptions preview_opts{};
  ASSERT_TRUE(
      engine.ApplyInPlaceRestore(txn, source, opts, preview_opts, apply_opts, &report).ok());
  EXPECT_TRUE(report.dry_run);
  EXPECT_EQ(report.summary.applied_count, 0u);

  std::ifstream in(source + "/data.txt");
  std::string content;
  std::getline(in, content);
  EXPECT_EQ(content, "modified-content");
}

TEST(InPlaceRestoreTest, NoBaseFallsBackToTwoWay) {
  const std::string repo = test::TempDir("inplace_2way_repo");
  const std::string source = test::TempDir("inplace_2way_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/data.txt", "only-snap");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  const uint64_t txn = engine.superblock().critical.txn_id;

  test::WriteFile(source + "/data.txt", "changed-live");

  RestoreOptions opts{};
  restore::InPlacePreviewOptions preview_opts{};
  preview_opts.use_three_way = false;
  restore::InPlacePreviewReport report{};
  ASSERT_TRUE(
      engine.PreviewInPlaceRestore(txn, source, opts, preview_opts, &report).ok());
  EXPECT_FALSE(report.three_way);
  auto* entry = FindEntry(&report, "data.txt");
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->action, "modify");
  EXPECT_TRUE(entry->base_action.empty());
}

}  // namespace
}  // namespace ebbackup
