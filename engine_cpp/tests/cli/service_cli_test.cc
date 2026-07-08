#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

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

TEST(ServiceCliTest, StatusWhenNotInstalled) {
#ifdef _WIN32
  const int rc = RunEbCommand("service status --name EbbackupDaemonNotInstalledTest");
  EXPECT_NE(rc, 0);
#else
  GTEST_SKIP() << "service CLI is Windows-only in MVP";
#endif
}

}  // namespace
}  // namespace test
}  // namespace ebbackup
