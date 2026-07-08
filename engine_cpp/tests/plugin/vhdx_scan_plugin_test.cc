#include <gtest/gtest.h>

#include "ebbackup/common/hook_runner.h"
#include "ebbackup/plugin/plugin_registry.h"
#include "test_util.h"

namespace ebbackup {
namespace test {
namespace {

#ifndef _WIN32
TEST(VhdxScanPluginTest, SkippedOnNonWindows) {
  plugin::PluginSession session(TempDir("vhdx_skip_src"), {"vhdx_scan"});
  std::vector<report::BackupPathIssue> issues;
  ASSERT_TRUE(session.QuiesceAll(&issues).ok());
  bool skipped = false;
  for (const auto& issue : issues) {
    if (issue.reason.find("plugin_skipped:platform:vhdx_scan") != std::string::npos) {
      skipped = true;
    }
  }
  EXPECT_TRUE(skipped);
  session.EndQuiesce();
}
#else
std::string EscapePsSingleQuoted(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '\'') out += "''";
    else out.push_back(c);
  }
  return out;
}

TEST(VhdxScanPluginTest, MountsWhenVhdxPresent) {
  const std::string src = TempDir("vhdx_src");
  const std::string vhdx = src + "/disk.vhdx";
  const std::string ps =
      "powershell -NoProfile -Command \"$ErrorActionPreference='Stop'; "
      "New-Vhd -Path '" +
      EscapePsSingleQuoted(vhdx) + "' -SizeBytes 67108864 -Dynamic\"";
  int rc = 0;
  const Status create_st = RunShellCommand(ps, &rc);
  if (!create_st.ok() || rc != 0) {
    GTEST_SKIP() << "Hyper-V New-Vhd not available";
  }

  plugin::PluginSession session(src, {"vhdx_scan"});
  std::vector<report::BackupPathIssue> issues;
  ASSERT_TRUE(session.QuiesceAll(&issues).ok());

  std::vector<std::string> extra_roots;
  session.CollectExtraScanRoots(&extra_roots);
  if (extra_roots.empty()) {
    session.EndQuiesce();
    GTEST_SKIP() << "Mount-Vhd not available on this host";
  }

  const auto fragments = session.PluginReportJsonFragments();
  ASSERT_FALSE(fragments.empty());
  EXPECT_NE(fragments[0].find("\"mounted\":1"), std::string::npos) << fragments[0];

  session.EndQuiesce();
}
#endif

}  // namespace
}  // namespace test
}  // namespace ebbackup
