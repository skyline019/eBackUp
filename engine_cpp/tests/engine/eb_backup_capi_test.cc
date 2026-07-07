#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>

#include "ebbackup/eb_backup.h"
#include "ebbackup/store/chunk_store.h"
#include "test_util.h"

namespace {

TEST(EbBackupCapiTest, AbiVersionAndStats) {
  EXPECT_EQ(eb_backup_abi_version(), EB_BACKUP_ABI_VERSION);
  const std::string repo = ebbackup::test::TempDir("capi_repo");
  const std::string source = ebbackup::test::TempDir("capi_source");
  ASSERT_EQ(eb_backup_init_repo(repo.c_str()), EB_OK);
  ebbackup::test::WriteFile(source + "/a.txt", "hello capi");

  EbBackupEngine* eng = eb_backup_open(repo.c_str());
  ASSERT_NE(eng, nullptr);
  ASSERT_EQ(eb_backup_run(eng, source.c_str()), EB_OK);

  EbBackupStats stats{};
  ASSERT_EQ(eb_backup_get_stats(eng, &stats), EB_OK);
  EXPECT_GE(stats.files_processed, 1u);
  EXPECT_GE(stats.chunks_written + stats.chunks_reused, 1u);
  ASSERT_EQ(eb_backup_verify(eng), EB_OK);
  eb_backup_close(eng);
}

TEST(EbBackupCapiTest, OpenExReportsError) {
  const std::string repo = ebbackup::test::TempDir("capi_bad_open");
  ebbackup::ChunkRecordHeader hdr{};
  std::memset(&hdr, 0xFF, sizeof(hdr));
  const std::string bytes(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
  ebbackup::test::WriteFile(repo + "/data/chunks", bytes);
  EbStatus err = EB_OK;
  EbBackupEngine* eng = eb_backup_open_ex(repo.c_str(), &err);
  EXPECT_EQ(eng, nullptr);
  EXPECT_NE(err, EB_OK);
}

TEST(EbBackupCapiTest, LoadFilterFileExcludesGlob) {
  const std::string repo = ebbackup::test::TempDir("capi_filter_repo");
  const std::string source = ebbackup::test::TempDir("capi_filter_source");
  const std::string filter_path = ebbackup::test::TempDir("capi_filter_cfg") + "/filter.conf";
  ASSERT_EQ(eb_backup_init_repo(repo.c_str()), EB_OK);
  ebbackup::test::WriteFile(source + "/keep.txt", "keep-me");
  ebbackup::test::WriteFile(source + "/drop.tmp", "drop-me");
  ebbackup::test::WriteFile(filter_path, "exclude_glob=*.tmp\ninclude_glob=keep.txt\n");

  EbBackupEngine* eng = eb_backup_open(repo.c_str());
  ASSERT_NE(eng, nullptr);
  ASSERT_EQ(eb_backup_load_filter_file(eng, filter_path.c_str()), EB_OK);
  ASSERT_EQ(eb_backup_run(eng, source.c_str()), EB_OK);
  ASSERT_EQ(eb_backup_verify(eng), EB_OK);

  const std::string dest = ebbackup::test::TempDir("capi_filter_dest");
  ASSERT_EQ(eb_backup_restore(eng, dest.c_str()), EB_OK);
  EXPECT_TRUE(std::filesystem::exists(dest + "/keep.txt"));
  EXPECT_FALSE(std::filesystem::exists(dest + "/drop.tmp"));
  eb_backup_close(eng);
}

TEST(EbBackupCapiTest, RestoreSkipContentVerifyFlag) {
  const std::string repo = ebbackup::test::TempDir("capi_skip_verify_repo");
  const std::string source = ebbackup::test::TempDir("capi_skip_verify_source");
  ASSERT_EQ(eb_backup_init_repo(repo.c_str()), EB_OK);
  ebbackup::test::WriteFile(source + "/keep.txt", "keep-me");

  EbBackupEngine* eng = eb_backup_open(repo.c_str());
  ASSERT_NE(eng, nullptr);
  ASSERT_EQ(eb_backup_run(eng, source.c_str()), EB_OK);

  const std::string dest = ebbackup::test::TempDir("capi_skip_verify_dest");
  ASSERT_EQ(eb_backup_restore_ex(eng, dest.c_str(),
                                 EB_RESTORE_FLAG_SKIP_CONTENT_VERIFY),
            EB_OK);
  EXPECT_TRUE(std::filesystem::exists(dest + "/keep.txt"));
  eb_backup_close(eng);
}

TEST(EbBackupCapiTest, V03InitCompactAndRepoStats) {
  const std::string repo = ebbackup::test::TempDir("capi_v03_repo");
  const std::string source = ebbackup::test::TempDir("capi_v03_source");
  const uint32_t init_flags = EB_BACKUP_INIT_LEGACY | EB_BACKUP_FLAG_COMPRESS_AUTO |
                              EB_BACKUP_FLAG_MANIFEST_BINARY;
  ASSERT_EQ(eb_backup_init_repo_ex(repo.c_str(), init_flags), EB_OK);
  ebbackup::test::WriteFile(source + "/data.bin",
                            ebbackup::test::MakeSyntheticData(256 * 1024, 7));

  EbBackupEngine* eng = eb_backup_open(repo.c_str());
  ASSERT_NE(eng, nullptr);
  ASSERT_EQ(eb_backup_run_ex(eng, source.c_str(), EB_BACKUP_FLAG_COMPRESS_AUTO),
            EB_OK);

  EbRepoStats stats{};
  ASSERT_EQ(eb_backup_repo_stats(eng, &stats), EB_OK);
  EXPECT_GT(stats.physical_bytes, 0u);
  EXPECT_DOUBLE_EQ(stats.ampl_ratio, 1.0);

  const std::string orphan = ebbackup::test::MakeSyntheticData(16 * 1024, 99);
  ebbackup::ChunkStore store(repo + "/data/chunks");
  ASSERT_TRUE(store.Open().ok());
  uint8_t hash[32];
  ASSERT_TRUE(store.Put(reinterpret_cast<const uint8_t*>(orphan.data()),
                      orphan.size(), hash)
                  .ok());

  EbRepoStats after_orphan{};
  ASSERT_EQ(eb_backup_repo_stats(eng, &after_orphan), EB_OK);
  EXPECT_GT(after_orphan.ampl_ratio, 1.0);

  EbCompactReport compact{};
  ASSERT_EQ(eb_backup_compact(eng, 0, &compact), EB_OK);
  EXPECT_LE(compact.ampl_ratio_after, 1.05);
  ASSERT_EQ(eb_backup_verify(eng), EB_OK);
  eb_backup_close(eng);
}

TEST(EbBackupCapiTest, BalancedDurabilityFlag) {
  const std::string repo = ebbackup::test::TempDir("capi_balanced_repo");
  const std::string source = ebbackup::test::TempDir("capi_balanced_source");
  ASSERT_EQ(eb_backup_init_repo_ex(repo.c_str(), 0), EB_OK);
  ebbackup::test::WriteFile(source + "/a.txt", "balanced-durability");

  EbBackupEngine* eng = eb_backup_open(repo.c_str());
  ASSERT_NE(eng, nullptr);
  ASSERT_EQ(eb_backup_run_ex(eng, source.c_str(),
                             EB_BACKUP_FLAG_BALANCED_DURABILITY),
            EB_OK);
  ASSERT_EQ(eb_backup_verify(eng), EB_OK);
  eb_backup_close(eng);
}

TEST(EbBackupCapiTest, SnapshotsListRestoreAt) {
  const std::string repo = ebbackup::test::TempDir("capi_snap_repo");
  const std::string source = ebbackup::test::TempDir("capi_snap_source");
  ASSERT_EQ(eb_backup_init_repo_ex(repo.c_str(), 0), EB_OK);
  ebbackup::test::WriteFile(source + "/a.txt", "v1");
  EbBackupEngine* eng = eb_backup_open(repo.c_str());
  ASSERT_NE(eng, nullptr);
  ASSERT_EQ(eb_backup_run(eng, source.c_str()), EB_OK);
  ebbackup::test::WriteFile(source + "/a.txt", "v2");
  ASSERT_EQ(eb_backup_run(eng, source.c_str()), EB_OK);

  EbSnapshotInfo* snaps = nullptr;
  size_t count = 0;
  ASSERT_EQ(eb_backup_list_snapshots(eng, &snaps, &count), EB_OK);
  ASSERT_GE(count, 2u);
  const uint64_t txn = snaps[0].txn_id;
  eb_backup_free_snapshots(snaps);

  const std::string dest = ebbackup::test::TempDir("capi_snap_dest");
  ASSERT_EQ(eb_backup_restore_at(eng, dest.c_str(), txn, 0), EB_OK);
  {
    std::ifstream in(dest + "/a.txt");
    std::string restored;
    std::getline(in, restored);
    EXPECT_EQ(restored, "v1");
  }

  ASSERT_EQ(eb_backup_verify_at(eng, txn), EB_OK);
  eb_backup_close(eng);
}

TEST(EbBackupCapiTest, PruneSnapshots) {
  const std::string repo = ebbackup::test::TempDir("capi_prune_repo");
  const std::string source = ebbackup::test::TempDir("capi_prune_source");
  ASSERT_EQ(eb_backup_init_repo_ex(repo.c_str(), 0), EB_OK);
  EbBackupEngine* eng = eb_backup_open(repo.c_str());
  ASSERT_NE(eng, nullptr);
  for (int i = 0; i < 5; ++i) {
    ebbackup::test::WriteFile(source + "/f.txt", "v" + std::to_string(i));
    ASSERT_EQ(eb_backup_run(eng, source.c_str()), EB_OK);
  }
  EbPruneReport report{};
  ASSERT_EQ(eb_backup_prune_snapshots(eng, "1h:24,1d:7,7d:4,30d:6", 3, 0, &report),
            EB_OK);
  EXPECT_GE(report.kept_count, 3u);
  eb_backup_close(eng);
}

}  // namespace
