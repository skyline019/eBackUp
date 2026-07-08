#include <gtest/gtest.h>

#include <filesystem>

#include "ebbackup/engine/backup_engine.h"
#include "ebsync/sync_agent.h"
#include "test_util.h"

TEST(LocalPushRoundtripTest, PushPullVerify) {
  const std::string repo = ebbackup::test::TempDir("push_src");
  const std::string source = ebbackup::test::TempDir("push_source");
  const std::string mirror = ebbackup::test::TempDir("push_mirror");
  const std::string pulled = ebbackup::test::TempDir("push_pulled");

  ASSERT_TRUE(ebbackup::test::InitDefaultRepo(repo).ok());
  ebbackup::test::WriteFile(source + "/data.bin", ebbackup::test::MakeSyntheticData(64 * 1024, 3));
  ebbackup::BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  ASSERT_TRUE(ebsync::InitSyncRepoLocal(repo, mirror));
  ebsync::PushOptions push_opts;
  push_opts.once = true;
  const ebsync::PushReport rep = ebsync::PushSync(repo, push_opts);
  ASSERT_TRUE(rep.ok) << rep.error;
  EXPECT_GT(rep.synced_txn, 0u);

  std::string summary;
  ASSERT_TRUE(ebsync::PullRemote(repo, pulled, 0, &summary)) << summary;

  ebbackup::BackupEngine pulled_engine(pulled);
  ASSERT_TRUE(pulled_engine.Open().ok());
  ASSERT_TRUE(pulled_engine.Verify().ok());
  EXPECT_FALSE(ebsync::ShouldBlockMaintenance(repo, nullptr));
}
