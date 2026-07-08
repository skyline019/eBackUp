#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "test_util.h"

#ifndef EBTEST_EB_EXE
#error "EBTEST_EB_EXE must be defined"
#endif

namespace ebbackup {
namespace test {
namespace {

int RunEbCommand(const std::string& args) {
  const std::string cmd = std::string(EBTEST_EB_EXE) + " " + args;
  return std::system(cmd.c_str());
}

std::string QuotePath(const std::string& path) {
  return "\"" + path + "\"";
}

TEST(RestoreCliTest, RestoreCliFilterOk) {
  const std::string repo = TempDir("cli_filter_repo");
  const std::string source = TempDir("cli_filter_source");
  const std::string dest = TempDir("cli_filter_dest");
  WriteFile(source + "/keep/data.txt", "keep-me");
  WriteFile(source + "/drop/other.txt", "drop-me");

  ASSERT_EQ(RunEbCommand("init " + QuotePath(repo)), 0);
  ASSERT_EQ(RunEbCommand("backup " + QuotePath(repo) + " " + QuotePath(source)), 0);
  ASSERT_EQ(RunEbCommand("restore " + QuotePath(repo) + " " + QuotePath(dest) +
                        " --include keep"),
            0);
  EXPECT_TRUE(std::filesystem::exists(dest + "/keep/data.txt"));
  EXPECT_FALSE(std::filesystem::exists(dest + "/drop/other.txt"));
}

TEST(RestoreCliTest, RestoreCliSkipVerify) {
  const std::string repo = TempDir("cli_skip_repo");
  const std::string source = TempDir("cli_skip_source");
  const std::string dest = TempDir("cli_skip_dest");
  WriteFile(source + "/keep/data.txt", "keep-me");

  ASSERT_EQ(RunEbCommand("init " + QuotePath(repo)), 0);
  ASSERT_EQ(RunEbCommand("backup " + QuotePath(repo) + " " + QuotePath(source)), 0);
  ASSERT_EQ(RunEbCommand("restore " + QuotePath(repo) + " " + QuotePath(dest) +
                        " --include keep --skip-content-verify"),
            0);
  EXPECT_TRUE(std::filesystem::exists(dest + "/keep/data.txt"));
}

TEST(RestoreCliTest, RestoreCliVerifyContent) {
  const std::string repo = TempDir("cli_verify_repo");
  const std::string source = TempDir("cli_verify_source");
  const std::string dest = TempDir("cli_verify_dest");
  WriteFile(source + "/all.txt", test::MakeSyntheticData(4096, 21));

  ASSERT_EQ(RunEbCommand("init " + QuotePath(repo)), 0);
  ASSERT_EQ(RunEbCommand("backup " + QuotePath(repo) + " " + QuotePath(source)), 0);
  ASSERT_EQ(RunEbCommand("restore " + QuotePath(repo) + " " + QuotePath(dest) +
                        " --verify-content"),
            0);
  EXPECT_TRUE(std::filesystem::exists(dest + "/all.txt"));
}

TEST(RestoreCliTest, InPlaceApplyJson) {
  const std::string repo = TempDir("cli_inplace_apply_repo");
  const std::string source = TempDir("cli_inplace_apply_source");
  WriteFile(source + "/data.txt", "cli-original");

  ASSERT_EQ(RunEbCommand("init " + QuotePath(repo)), 0);
  ASSERT_EQ(RunEbCommand("backup " + QuotePath(repo) + " " + QuotePath(source)), 0);
  WriteFile(source + "/data.txt", "cli-changed");
  ASSERT_EQ(RunEbCommand("restore " + QuotePath(repo) + " " + QuotePath(source) +
                        " --in-place"),
            0);
  std::ifstream in(source + "/data.txt");
  std::string content;
  std::getline(in, content);
  EXPECT_EQ(content, "cli-original");
}

TEST(RestoreCliTest, InPlacePreviewJson) {
  const std::string repo = TempDir("cli_inplace_repo");
  const std::string source = TempDir("cli_inplace_source");
  WriteFile(source + "/data.txt", "cli-preview");

  ASSERT_EQ(RunEbCommand("init " + QuotePath(repo)), 0);
  ASSERT_EQ(RunEbCommand("backup " + QuotePath(repo) + " " + QuotePath(source)), 0);
  WriteFile(source + "/data.txt", "changed");
  ASSERT_EQ(RunEbCommand("restore " + QuotePath(repo) + " " + QuotePath(source) +
                        " --in-place --preview"),
            0);
}

TEST(RestoreCliTest, InPlaceOrphanDelete) {
  const std::string repo = TempDir("cli_inplace_orphan_repo");
  const std::string source = TempDir("cli_inplace_orphan_source");
  WriteFile(source + "/keep.txt", "keep");

  ASSERT_EQ(RunEbCommand("init " + QuotePath(repo)), 0);
  ASSERT_EQ(RunEbCommand("backup " + QuotePath(repo) + " " + QuotePath(source)), 0);
  WriteFile(source + "/extra.txt", "orphan");
  ASSERT_EQ(RunEbCommand("restore " + QuotePath(repo) + " " + QuotePath(source) +
                        " --in-place --in-place-orphans delete --in-place-conflict overwrite"),
            0);
  EXPECT_FALSE(std::filesystem::exists(source + "/extra.txt"));
  EXPECT_TRUE(std::filesystem::exists(source + "/keep.txt"));
}

TEST(RestoreCliTest, InPlaceOverwriteConflict) {
  const std::string repo = TempDir("cli_inplace_over_repo");
  const std::string source = TempDir("cli_inplace_over_source");
  WriteFile(source + "/slot.txt", "file-content");

  ASSERT_EQ(RunEbCommand("init " + QuotePath(repo)), 0);
  ASSERT_EQ(RunEbCommand("backup " + QuotePath(repo) + " " + QuotePath(source)), 0);
  std::filesystem::remove(source + "/slot.txt");
  std::filesystem::create_directories(source + "/slot.txt");
  ASSERT_EQ(RunEbCommand("restore " + QuotePath(repo) + " " + QuotePath(source) +
                        " --in-place --in-place-conflict overwrite"),
            0);
  EXPECT_TRUE(std::filesystem::is_regular_file(source + "/slot.txt"));
}

TEST(RestoreCliTest, InPlaceDryRun) {
  const std::string repo = TempDir("cli_inplace_dry_repo");
  const std::string source = TempDir("cli_inplace_dry_source");
  WriteFile(source + "/data.txt", "original");

  ASSERT_EQ(RunEbCommand("init " + QuotePath(repo)), 0);
  ASSERT_EQ(RunEbCommand("backup " + QuotePath(repo) + " " + QuotePath(source)), 0);
  WriteFile(source + "/data.txt", "changed");
  ASSERT_EQ(RunEbCommand("restore " + QuotePath(repo) + " " + QuotePath(source) +
                        " --in-place --dry-run"),
            0);
  std::ifstream in(source + "/data.txt");
  std::string content;
  std::getline(in, content);
  EXPECT_EQ(content, "changed");
}

TEST(RestoreCliTest, InPlaceBaseAtThreeWay) {
  const std::string repo = TempDir("cli_inplace_base_repo");
  const std::string source = TempDir("cli_inplace_base_source");
  WriteFile(source + "/a.txt", "v1");

  ASSERT_EQ(RunEbCommand("init " + QuotePath(repo)), 0);
  ASSERT_EQ(RunEbCommand("backup " + QuotePath(repo) + " " + QuotePath(source)), 0);
  WriteFile(source + "/a.txt", "v2");
  ASSERT_EQ(RunEbCommand("backup " + QuotePath(repo) + " " + QuotePath(source) +
                        " --incremental"),
            0);
  WriteFile(source + "/a.txt", "v3");
  ASSERT_EQ(RunEbCommand("restore " + QuotePath(repo) + " " + QuotePath(source) +
                        " --in-place --preview --base-at 1"),
            0);
}

TEST(RestoreCliTest, SymlinkRemapSmoke) {
  const std::string repo = TempDir("cli_symlink_remap_repo");
  const std::string source = TempDir("cli_symlink_remap_source");
  const std::string dest = TempDir("cli_symlink_remap_dest");
  WriteFile(source + "/target.txt", "target-data");
  std::error_code ec;
  std::filesystem::create_symlink("target.txt", source + "/link.txt", ec);
  if (ec) GTEST_SKIP() << "symlink not supported: " << ec.message();

  ASSERT_EQ(RunEbCommand("init " + QuotePath(repo)), 0);
  ASSERT_EQ(RunEbCommand("backup " + QuotePath(repo) + " " + QuotePath(source)), 0);
  ASSERT_EQ(RunEbCommand("restore " + QuotePath(repo) + " " + QuotePath(dest) +
                        " --symlink-remap-from target --symlink-remap-to remapped"),
            0);
  EXPECT_TRUE(std::filesystem::exists(dest + "/link.txt"));
}

}  // namespace
}  // namespace test
}  // namespace ebbackup
