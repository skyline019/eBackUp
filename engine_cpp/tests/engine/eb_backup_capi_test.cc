#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>

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

}  // namespace
