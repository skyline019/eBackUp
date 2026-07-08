#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "test_util.h"

#ifndef EBTEST_EB_EXE
#error "EBTEST_EB_EXE must be defined"
#endif

namespace ebbackup {
namespace test {
namespace {

TEST(SuggestExcludesCliTest, JsonOutput) {
  const std::string src = TempDir("cli_suggest_src");
  WriteFile(src + "/.git/HEAD", "ref");
  const std::string cmd =
      std::string(EBTEST_EB_EXE) + " suggest-excludes \"" + src + "\" --json";
  const int rc = std::system(cmd.c_str());
  EXPECT_EQ(rc, 0);
}

TEST(SuggestExcludesCliTest, IncludeIdeFlag) {
  const std::string src = TempDir("cli_suggest_ide");
  WriteFile(src + "/.idea/workspace.xml", "x");
  const std::string cmd = std::string(EBTEST_EB_EXE) + " suggest-excludes \"" + src +
                          "\" --json --include-ide";
  const int rc = std::system(cmd.c_str());
  EXPECT_EQ(rc, 0);
}

}  // namespace
}  // namespace test
}  // namespace ebbackup
