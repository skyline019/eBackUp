#include <gtest/gtest.h>

#include <fstream>

#include "ebbackup/common/digest.h"
#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(BackupEngineTest, EndToEndBackupVerify) {
  const std::string repo = test::TempDir("backup_e2e_repo");
  const std::string source = test::TempDir("backup_e2e_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/big.bin", test::MakeSyntheticData(10 * 1024 * 1024, 9));
  test::WriteFile(source + "/small.txt", test::MakeSyntheticData(512, 1));

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  ASSERT_TRUE(engine.Verify().ok());
  EXPECT_GE(engine.stats().files_processed, 2u);
  EXPECT_GT(engine.stats().chunks_written, 0u);
}

TEST(BackupEngineTest, VerifyRejectsBadManifestFooter) {
  const std::string repo = test::TempDir("verify_bad_footer");
  const std::string source = test::TempDir("verify_bad_footer_src");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/file.txt", "hello");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  {
    std::fstream manifest(repo + "/manifest",
                          std::ios::binary | std::ios::in | std::ios::out);
    std::string header;
    std::getline(manifest, header);
    if (header == "EBMANIFEST4") {
      manifest.seekp(-1, std::ios::end);
      manifest.put(0);
    } else {
      std::streamoff off = 0;
      manifest.seekg(0, std::ios::end);
      const std::streamoff size = manifest.tellg();
      for (off = 0; off < size; ++off) {
        manifest.seekg(off);
        char c = 0;
        manifest.read(&c, 1);
        if (c >= '0' && c <= '9') {
          manifest.seekp(off);
          manifest.put(static_cast<char>(c == '9' ? '0' : c + 1));
          break;
        }
      }
    }
  }

  BackupEngine verify_engine(repo);
  ASSERT_TRUE(verify_engine.Open().ok());
  EXPECT_EQ(verify_engine.Verify().code(), StatusCode::kCorrupt);
}

TEST(BackupEngineTest, VerifyRejectsTxnMismatch) {
  const std::string repo = test::TempDir("verify_txn_mismatch");
  const std::string source = test::TempDir("verify_txn_mismatch_src");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/file.txt", "txn-test");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  {
    BackupSuperBlockStore sb_store(repo + "/superblock.bin");
    BackupSuperBlock sb{};
    ASSERT_TRUE(sb_store.Load(&sb).ok());
    sb.critical.txn_id = sb.critical.txn_id + 99;
    ASSERT_TRUE(sb_store.Commit(sb).ok());
  }

  BackupEngine verify_engine(repo);
  ASSERT_TRUE(verify_engine.Open().ok());
  EXPECT_EQ(verify_engine.Verify().code(), StatusCode::kCorrupt);
}

}  // namespace
}  // namespace ebbackup
