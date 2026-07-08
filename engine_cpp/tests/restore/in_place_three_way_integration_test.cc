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

TEST(InPlaceThreeWayIntegrationTest, IncrementalChainBothChanged) {
  const std::string repo = test::TempDir("inplace_chain_repo");
  const std::string source = test::TempDir("inplace_chain_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/hello.txt", "v1");
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull).ok());
  const uint64_t txn1 = engine.superblock().critical.txn_id;

  test::WriteFile(source + "/hello.txt", "v2-target");
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental).ok());
  const uint64_t txn2 = engine.superblock().critical.txn_id;

  test::WriteFile(source + "/hello.txt", "v3-live");

  RestoreOptions opts{};
  restore::InPlacePreviewOptions preview_opts{};
  restore::InPlacePreviewReport preview{};
  ASSERT_TRUE(
      engine.PreviewInPlaceRestore(txn2, source, opts, preview_opts, &preview).ok());
  EXPECT_TRUE(preview.three_way);
  EXPECT_EQ(preview.base_txn_id, txn1);

  auto* entry = FindEntry(&preview, "hello.txt");
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->action, "both_changed");

  restore::InPlaceApplyOptions apply_opts{};
  apply_opts.conflict = restore::InPlaceConflictPolicy::kOverwrite;
  restore::InPlaceApplyReport applied{};
  ASSERT_TRUE(
      engine.ApplyInPlaceRestore(txn2, source, opts, preview_opts, apply_opts, &applied).ok());
  EXPECT_GE(applied.summary.overwritten_count, 1u);

  std::ifstream in(source + "/hello.txt");
  std::string content;
  std::getline(in, content);
  EXPECT_EQ(content, "v2-target");
}

}  // namespace
}  // namespace ebbackup
