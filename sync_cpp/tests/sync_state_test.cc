#include <filesystem>
#include <gtest/gtest.h>

#include "ebsync/sync_state.h"

TEST(SyncStateTest, RoundTrip) {
  const std::string repo = "sync_state_test_repo";
  std::error_code ec;
  std::filesystem::remove_all(repo, ec);

  ebsync::SyncState st;
  st.synced_txn = 7;
  st.pending_txn = 8;
  st.remote_type = "local_mirror";
  st.local_mirror_root = "D:\\mirror";
  st.last_ferry_target_txn = 9;
  st.s3.bucket = "b";
  st.s3.prefix = "p/";
  ASSERT_TRUE(ebsync::SaveSyncState(repo, st));

  ebsync::SyncState loaded;
  ASSERT_TRUE(ebsync::LoadSyncState(repo, &loaded));
  EXPECT_EQ(loaded.synced_txn, 7u);
  EXPECT_EQ(loaded.pending_txn, 8u);
  EXPECT_EQ(loaded.remote_type, "local_mirror");
  EXPECT_EQ(loaded.local_mirror_root, "D:\\mirror");
  EXPECT_EQ(loaded.last_ferry_target_txn, 9u);
  EXPECT_EQ(loaded.s3.bucket, "b");
  EXPECT_EQ(loaded.s3.prefix, "p/");

  std::filesystem::remove_all(repo, ec);
}
