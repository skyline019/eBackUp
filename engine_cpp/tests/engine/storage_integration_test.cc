#include <gtest/gtest.h>

#include <fstream>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/store/chunk_compactor.h"
#include "ebbackup/store/chunk_store.h"
#include "ebbackup/store/repo_stats.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(StorageIntegrationTest, V03BackupGcCompactVerifyRestore) {
  const std::string repo = test::TempDir("storage_int");
  const std::string source = test::TempDir("storage_int_src");
  const std::string dest = test::TempDir("storage_int_dest");

  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/docs/readme.txt",
                  test::MakeSyntheticData(384 * 1024, 11));
  test::WriteFile(source + "/cache.tmp", "ephemeral");

  BackupOptions opts{};
  opts.compress_mode = CompressMode::kAuto;
  opts.cpu_budget_permille = 600;

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  EXPECT_TRUE(RepoUsesPersistentIndex(engine.superblock()));
  EXPECT_TRUE(RepoUsesManifestBinary(engine.superblock()));
  EXPECT_TRUE(RepoUsesEbPack(engine.superblock()));
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());

  std::ifstream manifest_in(repo + "/manifest");
  std::string header;
  ASSERT_TRUE(std::getline(manifest_in, header));
  EXPECT_EQ(header, "EBMANIFEST4");

  ChunkStore extra(repo + "/data/chunks");
  extra.SetUseEbPack(true);
  extra.SetUsePersistentIndex(true);
  extra.SetTxnId(2);
  ASSERT_TRUE(extra.Open().ok());
  ASSERT_TRUE(extra.BeginAppendSession().ok());
  const std::string orphan = test::MakeSyntheticData(64 * 1024, 77);
  uint8_t orphan_hash[32];
  ASSERT_TRUE(extra.Put(reinterpret_cast<const uint8_t*>(orphan.data()),
                      orphan.size(), orphan_hash)
                  .ok());
  ASSERT_TRUE(extra.Flush().ok());
  ASSERT_TRUE(extra.EndAppendSession().ok());

  OrphanGcReport gc_report{};
  BackupEngine gc_engine(repo);
  ASSERT_TRUE(gc_engine.Open().ok());
  ASSERT_TRUE(gc_engine.GcOrphans(true, &gc_report).ok());
  EXPECT_GE(gc_report.orphan_count, 1u);

  RepoStats before{};
  ASSERT_TRUE(ComputeRepoStats(repo, &before).ok());
  EXPECT_GT(before.ampl_ratio, 1.0);

  CompactReport compact{};
  ASSERT_TRUE(CompactChunkStore(repo, false, &compact).ok());
  EXPECT_LE(compact.ampl_ratio_after, 1.05);

  RepoStats after{};
  ASSERT_TRUE(ComputeRepoStats(repo, &after).ok());
  EXPECT_LE(after.ampl_ratio, 1.05);
  EXPECT_LE(after.orphan_bytes, kChunkHeaderV2Size);

  BackupEngine verify_engine(repo);
  ASSERT_TRUE(verify_engine.Open().ok());
  ASSERT_TRUE(verify_engine.Verify().ok());
  ASSERT_TRUE(verify_engine.Restore(dest).ok());

  EXPECT_TRUE(std::filesystem::exists(dest + "/docs/readme.txt"));
  EXPECT_TRUE(std::filesystem::exists(dest + "/cache.tmp"));
}

}  // namespace
}  // namespace ebbackup
