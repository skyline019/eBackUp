#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
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

}  // namespace
}  // namespace test
}  // namespace ebbackup
