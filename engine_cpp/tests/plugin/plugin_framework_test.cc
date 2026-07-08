#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "ebbackup/plugin/plugin_registry.h"
#include "ebbackup/report/backup_report.h"
#include "ebbackup/scan/scan_entry.h"
#include "ebbackup/scan/scan_hint_options.h"
#include "test_util.h"

namespace ebbackup {
namespace test {
namespace {

TEST(PluginFrameworkTest, RegistryListsBuiltins) {
  const auto ids = plugin::ListBuiltinPluginIds();
  ASSERT_EQ(ids.size(), 3u);
  EXPECT_EQ(ids[0], "sqlite_checkpoint");
}

TEST(PluginFrameworkTest, ScanHintSkipsWalSidecar) {
  ScanHintOptions opts{};
  plugin::ScanHint hint{};
  hint.path_prefix = "C:/data/app.db-wal";
  hint.skip_subtree = true;
  opts.hints.push_back(hint);
  EXPECT_TRUE(ShouldSkipScanPath("C:/data/app.db-wal", opts));
  EXPECT_FALSE(ShouldSkipScanPath("C:/data/app.db", opts));
}

TEST(PluginFrameworkTest, PluginSessionQuiesceThaw) {
  plugin::PluginSession session(test::TempDir("plugin_session_src"),
                               {"sqlite_checkpoint"});
  std::vector<report::BackupPathIssue> issues;
  EXPECT_TRUE(session.QuiesceAll(&issues).ok());
  session.EndQuiesce();
}

}  // namespace
}  // namespace test
}  // namespace ebbackup
