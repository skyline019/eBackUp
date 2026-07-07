#include <gtest/gtest.h>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/store/orphan_gc.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(OrphanGcTest, DryRunDetectsUnreferencedAfterInterruptHint) {
  const std::string repo = test::TempDir("orphan_gc_repo");
  const std::string source = test::TempDir("orphan_gc_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(256 * 1024, 2));

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  ChunkStore extra(repo + "/data/chunks");
  extra.SetUseEbPack(true);
  extra.SetUsePersistentIndex(true);
  extra.SetTxnId(2);
  ASSERT_TRUE(extra.Open().ok());
  ASSERT_TRUE(extra.BeginAppendSession().ok());
  const std::string orphan_payload = "orphan-chunk-payload";
  uint8_t orphan_hash[32];
  ASSERT_TRUE(extra.Put(reinterpret_cast<const uint8_t*>(orphan_payload.data()),
                      orphan_payload.size(), orphan_hash)
                  .ok());
  ASSERT_TRUE(extra.Flush().ok());
  ASSERT_TRUE(extra.EndAppendSession().ok());

  BackupEngine engine2(repo);
  ASSERT_TRUE(engine2.Open().ok());
  OrphanGcReport report{};
  ASSERT_TRUE(engine2.GcOrphans(true, &report).ok());
  EXPECT_GE(report.orphan_count, 1u);
}

TEST(OrphanGcTest, ApplyThenVerifyStillOk) {
  const std::string repo = test::TempDir("orphan_gc_apply_repo");
  const std::string source = test::TempDir("orphan_gc_apply_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(256 * 1024, 2));

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  ChunkStore extra(repo + "/data/chunks");
  extra.SetUseEbPack(true);
  extra.SetUsePersistentIndex(true);
  extra.SetTxnId(2);
  ASSERT_TRUE(extra.Open().ok());
  ASSERT_TRUE(extra.BeginAppendSession().ok());
  const std::string orphan_payload = "orphan-chunk-apply";
  uint8_t orphan_hash[32];
  ASSERT_TRUE(extra.Put(reinterpret_cast<const uint8_t*>(orphan_payload.data()),
                      orphan_payload.size(), orphan_hash)
                  .ok());
  ASSERT_TRUE(extra.Flush().ok());
  ASSERT_TRUE(extra.EndAppendSession().ok());

  BackupEngine engine2(repo);
  ASSERT_TRUE(engine2.Open().ok());
  OrphanGcReport report{};
  ASSERT_TRUE(engine2.GcOrphans(false, &report).ok());
  EXPECT_GE(report.orphan_count, 1u);
  ASSERT_TRUE(engine2.Verify().ok());
}

}  // namespace
}  // namespace ebbackup
