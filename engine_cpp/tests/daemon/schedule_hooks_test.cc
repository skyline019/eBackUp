#include <gtest/gtest.h>

#include <fstream>

#include "ebbackup/daemon/backup_daemon.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(ScheduleHooksTest, JsonPrePostCmdParsed) {
  const std::string config_path = test::TempDir("hook_cfg") + "/schedule.json";
  std::ofstream out(config_path);
  out << "{\n"
      << "  \"source\": \"" << (test::TempDir("hook_src")) << "\",\n"
      << "  \"repo_base\": \"" << (test::TempDir("hook_repo_base")) << "\",\n"
      << "  \"pre_backup_cmd\": \"echo pre\",\n"
      << "  \"post_backup_cmd\": \"echo post\"\n"
      << "}\n";
  out.close();

  ScheduleConfig cfg{};
  ASSERT_TRUE(LoadScheduleConfigAuto(config_path, &cfg).ok());
  EXPECT_EQ(cfg.backup_options.pre_backup_cmd, "echo pre");
  EXPECT_EQ(cfg.backup_options.post_backup_cmd, "echo post");
}

TEST(SchedulePluginsTest, IniPluginsParsed) {
  const std::string config_path = test::TempDir("plugin_cfg") + "/schedule.ini";
  std::ofstream out(config_path);
  out << "source=" << test::TempDir("plugin_src") << "\n"
      << "repo_base=" << test::TempDir("plugin_repo_base") << "\n"
      << "plugins=sqlite_checkpoint,registry_hive\n";
  out.close();

  ScheduleConfig cfg{};
  ASSERT_TRUE(LoadScheduleConfig(config_path, &cfg).ok());
  ASSERT_EQ(cfg.backup_options.plugins.size(), 2u);
  EXPECT_EQ(cfg.backup_options.plugins[0], "sqlite_checkpoint");
  EXPECT_EQ(cfg.backup_options.plugins[1], "registry_hive");
}

TEST(SchedulePluginsTest, JsonPluginsParsed) {
  const std::string config_path = test::TempDir("plugin_json_cfg") + "/schedule.json";
  std::ofstream out(config_path);
  out << "{\n"
      << "  \"source\": \"" << test::TempDir("plugin_json_src") << "\",\n"
      << "  \"repo_base\": \"" << test::TempDir("plugin_json_repo_base") << "\",\n"
      << "  \"plugins\": \"sqlite_checkpoint,vhdx_scan\"\n"
      << "}\n";
  out.close();

  ScheduleConfig cfg{};
  ASSERT_TRUE(LoadScheduleConfigAuto(config_path, &cfg).ok());
  ASSERT_EQ(cfg.backup_options.plugins.size(), 2u);
  EXPECT_EQ(cfg.backup_options.plugins[0], "sqlite_checkpoint");
  EXPECT_EQ(cfg.backup_options.plugins[1], "vhdx_scan");
}

}  // namespace
}  // namespace ebbackup
