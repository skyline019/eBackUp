#include <gtest/gtest.h>

#include "ebbackup/store/retention_policy.h"
#include "ebbackup/store/snapshot_store.h"
#include "test_util.h"

namespace ebbackup {
namespace {

std::vector<SnapshotEntry> MakeEntries() {
  std::vector<SnapshotEntry> entries;
  int64_t t = 1'700'000'000;
  for (int i = 0; i < 10; ++i) {
    SnapshotEntry e{};
    e.txn_id = static_cast<uint64_t>(i + 1);
    e.created_at_unix = t + static_cast<int64_t>(i) * 3600;
    entries.push_back(e);
  }
  return entries;
}

TEST(RetentionPolicyTest, DefaultGfsKeepsLatestAndMin) {
  const auto entries = MakeEntries();
  RetentionPolicy policy = DefaultRetentionPolicy();
  policy.retain_min = 3;
  std::unordered_set<uint64_t> keep;
  ComputeKeepSet(entries, policy, &keep);
  EXPECT_TRUE(keep.count(10) > 0);
  EXPECT_GE(keep.size(), 3u);
}

TEST(RetentionPolicyTest, ParseRetentionTiers) {
  RetentionPolicy policy{};
  ASSERT_TRUE(ParseRetentionTiers("3600:24,86400:7", &policy).ok());
  ASSERT_EQ(policy.tiers.size(), 2u);
  EXPECT_EQ(policy.tiers[0].bucket_seconds, 3600);
  EXPECT_EQ(policy.tiers[0].keep_count, 24);
}

TEST(RetentionPolicyTest, PruneDryRunReportsCandidates) {
  const std::string repo = test::TempDir("retention_prune");
  std::vector<SnapshotEntry> entries = MakeEntries();
  ASSERT_TRUE(SaveSnapshotIndex(repo, entries).ok());

  RetentionPolicy policy{};
  policy.retain_min = 2;
  policy.tiers.clear();
  PruneReport report{};
  ASSERT_TRUE(PruneSnapshots(repo, policy, true, &report).ok());
  EXPECT_GE(report.pruned_count, 1u);
  EXPECT_EQ(report.kept_count, 2u);

  std::vector<SnapshotEntry> after;
  ASSERT_TRUE(ListSnapshots(repo, &after).ok());
  EXPECT_EQ(after.size(), entries.size());
}

}  // namespace
}  // namespace ebbackup
