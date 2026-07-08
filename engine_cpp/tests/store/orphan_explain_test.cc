#include <gtest/gtest.h>

#include <cstring>
#include <fstream>

#include "ebbackup/common/digest.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/store/orphan_explain.h"
#include "test_util.h"

namespace ebbackup {
namespace store {
namespace {

TEST(OrphanExplainTest, DetectsUnreferencedOrphan) {
  const std::string repo = test::TempDir("orphan_explain_repo");
  const std::string source = test::TempDir("orphan_explain_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(256 * 1024, 9));

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  ChunkStore extra(repo + "/data/chunks");
  extra.SetUseEbPack(true);
  extra.SetUsePersistentIndex(true);
  extra.SetTxnId(2);
  ASSERT_TRUE(extra.Open().ok());
  ASSERT_TRUE(extra.BeginAppendSession().ok());
  const std::string orphan_payload = "orphan-explain-payload";
  uint8_t orphan_hash[32];
  ASSERT_TRUE(extra.Put(reinterpret_cast<const uint8_t*>(orphan_payload.data()),
                      orphan_payload.size(), orphan_hash)
                  .ok());
  ASSERT_TRUE(extra.Flush().ok());
  ASSERT_TRUE(extra.EndAppendSession().ok());

  BackupEngine engine2(repo);
  ASSERT_TRUE(engine2.Open().ok());
  OrphanExplainReport report{};
  ASSERT_TRUE(BuildOrphanExplainReport(engine2, 64, &report).ok());
  EXPECT_GE(report.unreferenced_count, 1u);
  EXPECT_GE(report.total_orphans, 1u);
  EXPECT_GE(report.total_orphan_bytes, orphan_payload.size());

  const std::string json = OrphanExplainReportToJson(report);
  EXPECT_NE(json.find("\"ok\":true"), std::string::npos);
  EXPECT_NE(json.find("\"reason\":\"unreferenced\""), std::string::npos);
}

TEST(OrphanExplainTest, CountsTombstoneEntries) {
  const std::string repo = test::TempDir("orphan_explain_tomb_repo");
  const std::string source = test::TempDir("orphan_explain_tomb_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(128 * 1024, 11));

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  uint8_t hash[32]{};
  std::memset(hash, 0x42, 32);
  const std::string tomb_path = repo + "/data/tombstones";
  std::ofstream out(tomb_path, std::ios::binary | std::ios::app);
  ASSERT_TRUE(out.is_open());
  out << BytesToHex(hash, 32) << '\n';
  out.close();

  OrphanExplainReport report{};
  ASSERT_TRUE(BuildOrphanExplainReport(engine, 64, &report).ok());
  EXPECT_GE(report.tombstoned_count, 1u);
  EXPECT_GE(report.total_orphans, 1u);

  bool saw_tombstone = false;
  for (const auto& sample : report.samples) {
    if (sample.reason == OrphanReason::kTombstoned) {
      saw_tombstone = true;
      break;
    }
  }
  EXPECT_TRUE(saw_tombstone);
}

}  // namespace
}  // namespace store
}  // namespace ebbackup
