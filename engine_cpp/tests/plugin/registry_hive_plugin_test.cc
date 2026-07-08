#include <gtest/gtest.h>

#include <filesystem>

#include "ebbackup/plugin/plugin_registry.h"
#include "test_util.h"

namespace ebbackup {
namespace test {
namespace {

#ifndef _WIN32
TEST(RegistryHivePluginTest, SkippedOnNonWindows) {
  plugin::PluginSession session(TempDir("registry_skip_src"), {"registry_hive"});
  std::vector<report::BackupPathIssue> issues;
  ASSERT_TRUE(session.QuiesceAll(&issues).ok());
  bool skipped = false;
  for (const auto& issue : issues) {
    if (issue.reason.find("plugin_skipped:platform:registry_hive") != std::string::npos) {
      skipped = true;
    }
  }
  EXPECT_TRUE(skipped);
  session.EndQuiesce();
}
#else
TEST(RegistryHivePluginTest, ExportsStagingDirWhenPermitted) {
  const std::string source = TempDir("registry_plugin_src");
  plugin::PluginSession session(source, {"registry_hive"});
  std::vector<report::BackupPathIssue> issues;
  ASSERT_TRUE(session.QuiesceAll(&issues).ok());
  std::vector<std::string> roots;
  session.CollectExtraScanRoots(&roots);
  session.EndQuiesce();
  if (roots.empty()) {
    GTEST_SKIP() << "registry export requires elevated privileges";
  }
  EXPECT_TRUE(std::filesystem::exists(roots[0]));
}
#endif

}  // namespace
}  // namespace test
}  // namespace ebbackup
