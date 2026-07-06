#include <gtest/gtest.h>

#include <fstream>

#include "ebbackup/daemon/backup_daemon.h"
#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

namespace ebbackup {
namespace {

std::string JsonPath(const std::string& path) {
  std::string out = path;
  for (char& c : out) {
    if (c == '\\') c = '/';
  }
  return out;
}

std::string NormalizePath(const std::string& path) {
  return JsonPath(path);
}

TEST(ScheduleJsonTest, LoadJsonConfigAndRunOnce) {
  const std::string source = test::TempDir("json_sched_source");
  const std::string repo_base = test::TempDir("json_sched_repos");
  const std::string config_path = test::TempDir("json_sched_cfg") + "/schedule.json";
  test::WriteFile(source + "/data.txt", "json-scheduled");

  const std::string json =
      "{\n"
      "  \"interval_seconds\": 1,\n"
      "  \"source\": \"" +
      JsonPath(source) + "\",\n"
               "  \"repo_base\": \"" +
      JsonPath(repo_base) + "\",\n"
                  "  \"retain\": 2,\n"
                  "  \"lz4\": true\n"
                  "}\n";
  test::WriteFile(config_path, json);

  ScheduleConfig cfg{};
  ASSERT_TRUE(LoadScheduleConfigAuto(config_path, &cfg).ok());
  EXPECT_EQ(NormalizePath(cfg.source_path), NormalizePath(source));
  EXPECT_EQ(NormalizePath(cfg.repo_base), NormalizePath(repo_base));
  EXPECT_TRUE(cfg.backup_options.use_lz4);
  ASSERT_TRUE(RunScheduledBackup(cfg, 1).ok());

  int repo_count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(repo_base)) {
    if (!entry.is_directory()) continue;
    if (entry.path().filename().string().rfind("repo-", 0) == 0) {
      ++repo_count;
      BackupEngine engine(entry.path().string());
      ASSERT_TRUE(engine.Open().ok());
      ASSERT_TRUE(engine.Verify().ok());
    }
  }
  EXPECT_EQ(repo_count, 1);
}

TEST(ScheduleJsonTest, FallsBackToKvFormat) {
  const std::string source = test::TempDir("kv_sched_source");
  const std::string repo_base = test::TempDir("kv_sched_repos");
  const std::string config_path = test::TempDir("kv_sched_cfg") + "/schedule.conf";
  test::WriteFile(source + "/kv.txt", "kv-scheduled");
  test::WriteFile(config_path,
                  "source=" + source + "\nrepo_base=" + repo_base + "\nretain=1\n");

  ScheduleConfig cfg{};
  ASSERT_TRUE(LoadScheduleConfigAuto(config_path, &cfg).ok());
  EXPECT_EQ(cfg.source_path, source);
  EXPECT_EQ(cfg.repo_base, repo_base);
}

TEST(ScheduleJsonTest, FilterGlobInJsonConfig) {
  const std::string source = test::TempDir("json_filter_source");
  const std::string repo_base = test::TempDir("json_filter_repos");
  const std::string config_path = test::TempDir("json_filter_cfg") + "/schedule.json";
  test::WriteFile(source + "/keep.txt", "kept");
  test::WriteFile(source + "/drop.tmp", "dropped");

  const std::string json =
      "{\n"
      "  \"interval_seconds\": 1,\n"
      "  \"source\": \"" +
      JsonPath(source) + "\",\n"
               "  \"repo_base\": \"" +
      JsonPath(repo_base) + "\",\n"
                  "  \"retain\": 1,\n"
                  "  \"include_glob\": \"keep.txt\",\n"
                  "  \"exclude_glob\": \"*.tmp\"\n"
                  "}\n";
  test::WriteFile(config_path, json);

  ScheduleConfig cfg{};
  ASSERT_TRUE(LoadScheduleConfigAuto(config_path, &cfg).ok());
  ASSERT_EQ(cfg.backup_options.filter.include_globs.size(), 1u);
  ASSERT_EQ(cfg.backup_options.filter.exclude_globs.size(), 1u);
  EXPECT_EQ(cfg.backup_options.filter.include_globs[0], "keep.txt");
  EXPECT_EQ(cfg.backup_options.filter.exclude_globs[0], "*.tmp");
  ASSERT_TRUE(RunScheduledBackup(cfg, 1).ok());
}

}  // namespace
}  // namespace ebbackup
