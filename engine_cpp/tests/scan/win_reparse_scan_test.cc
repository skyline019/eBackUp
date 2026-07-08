#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/scan/scan_entry.h"
#include "test_util.h"

namespace ebbackup {
namespace {

#ifdef _WIN32
TEST(WinReparseScanTest, JunctionDoesNotDuplicateSubtree) {
  const std::string source = test::TempDir("reparse_scan_source");
  std::error_code ec;
  std::filesystem::create_directories(source + "/real_dir", ec);
  test::WriteFile(source + "/real_dir/data.txt", "junction-target");

  const std::string junction = source + "/junction";
  const std::string cmd =
      "cmd /c mklink /J \"" + junction + "\" \"" + source + "\\real_dir\" >nul 2>&1";
  if (std::system(cmd.c_str()) != 0) {
    GTEST_SKIP() << "junction creation not supported";
  }

  ScanResult result;
  ASSERT_TRUE(ScanDirectory(source, &result).ok());

  int data_count = 0;
  bool saw_junction_issue = false;
  for (const auto& entry : result.entries) {
    if (entry.relative_path == "real_dir/data.txt") ++data_count;
    if (entry.relative_path == "junction/data.txt") ++data_count;
  }
  EXPECT_EQ(data_count, 1);
  for (const auto& issue : result.issues) {
    if (issue.reason == "reparse_junction") saw_junction_issue = true;
  }
  EXPECT_TRUE(saw_junction_issue);
}

TEST(WinReparseScanTest, BackupManifestCapturesReparseTarget) {
  const std::string repo = test::TempDir("reparse_manifest_repo");
  const std::string source = test::TempDir("reparse_manifest_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  std::error_code ec;
  std::filesystem::create_directories(source + "/real_dir", ec);
  test::WriteFile(source + "/real_dir/data.txt", "junction-target");

  const std::string junction = source + "/junction";
  const std::string cmd =
      "cmd /c mklink /J \"" + junction + "\" \"" + source + "\\real_dir\" >nul 2>&1";
  if (std::system(cmd.c_str()) != 0) {
    GTEST_SKIP() << "junction creation not supported";
  }

  ScanResult scan;
  ASSERT_TRUE(ScanDirectory(source, &scan).ok());
  bool saw_scan_junction = false;
  std::string junction_rel_path;
  for (const auto& entry : scan.entries) {
    if (entry.absolute_path.find("\\junction") != std::string::npos ||
        entry.absolute_path.find("/junction") != std::string::npos) {
      saw_scan_junction = true;
      junction_rel_path = entry.relative_path;
      EXPECT_EQ(entry.type, FileType::kDirectory);
      EXPECT_NE(entry.reparse_tag, 0u);
    }
  }
  ASSERT_TRUE(saw_scan_junction) << "junction missing from scan entries";
  ASSERT_FALSE(junction_rel_path.empty());
  for (const auto& entry : scan.entries) {
    if (entry.relative_path == "real_dir") {
      EXPECT_EQ(entry.reparse_tag, 0u);
    }
    if (entry.relative_path == "junction") {
      EXPECT_NE(entry.reparse_tag, 0u);
      EXPECT_FALSE(entry.reparse_target.empty());
    }
  }

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  ManifestDocument doc;
  ASSERT_TRUE(engine.LoadManifest(0, &doc).ok());
  bool saw_junction_entry = false;
  for (const auto& file : doc.files) {
    if (file.relative_path == junction_rel_path ||
        (file.reparse_tag != 0 && file.file_type == FileType::kDirectory)) {
      saw_junction_entry = true;
      EXPECT_NE(file.reparse_tag, 0u);
      EXPECT_FALSE(file.reparse_target.empty());
    }
  }
  EXPECT_TRUE(saw_junction_entry);
}
#endif

}  // namespace
}  // namespace ebbackup
