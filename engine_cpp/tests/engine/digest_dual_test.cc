#include <gtest/gtest.h>

#include <cstring>
#include <fstream>

#include "ebbackup/common/digest.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/state/superblock.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(DigestDualRepoTest, LegacyRepoRoundTrip) {
  const std::string repo = test::TempDir("dual_legacy_repo");
  const std::string source = test::TempDir("dual_legacy_source");
  const std::string dest = test::TempDir("dual_legacy_dest");
  ASSERT_TRUE(test::InitLegacyRepo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(256 * 1024, 3));

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  EXPECT_EQ(engine.digest_algo(), DigestAlgo::kLegacy);
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull).ok());
  ASSERT_TRUE(engine.Verify().ok());
  ASSERT_TRUE(engine.Restore(dest).ok());

  std::ifstream in(dest + "/data.bin", std::ios::binary);
  std::string got((std::istreambuf_iterator<char>(in)),
                  std::istreambuf_iterator<char>());
  EXPECT_EQ(got.size(), 256u * 1024);
}

TEST(DigestDualRepoTest, StandardRepoRoundTrip) {
  const std::string repo = test::TempDir("dual_standard_repo");
  const std::string source = test::TempDir("dual_standard_source");
  const std::string dest = test::TempDir("dual_standard_dest");
  ASSERT_TRUE(test::InitStandardRepo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(256 * 1024, 4));

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  EXPECT_EQ(engine.digest_algo(), DigestAlgo::kStandard);
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull).ok());
  ASSERT_TRUE(engine.Verify().ok());
  ASSERT_TRUE(engine.Restore(dest).ok());
}

TEST(DigestDualRepoTest, StandardDigestFlagSurvivesBackup) {
  const std::string repo = test::TempDir("dual_flag_repo");
  const std::string source = test::TempDir("dual_flag_source");
  ASSERT_TRUE(test::InitStandardRepo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(64 * 1024, 5));

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull).ok());

  BackupSuperBlockStore sb_store(repo + "/superblock.bin");
  BackupSuperBlock sb{};
  ASSERT_TRUE(sb_store.Load(&sb).ok());
  EXPECT_TRUE(RepoUsesStandardDigest(sb));

  BackupEngine reopened(repo);
  ASSERT_TRUE(reopened.Open().ok());
  EXPECT_EQ(reopened.digest_algo(), DigestAlgo::kStandard);
  EXPECT_TRUE(reopened.Verify().ok());
}

TEST(DigestDualEncryptTest, LegacyEncryptedRoundTrip) {
  const std::string repo = test::TempDir("dual_enc_legacy_repo");
  const std::string source = test::TempDir("dual_enc_legacy_source");
  const std::string dest = test::TempDir("dual_enc_legacy_dest");
  ASSERT_TRUE(test::InitLegacyRepo(repo).ok());
  test::WriteFile(source + "/secret.bin", test::MakeSyntheticData(64 * 1024, 6));

  BackupOptions opts{};
  opts.use_encryption = true;
  opts.encryption_password = "legacy-pass";
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());
  ASSERT_TRUE(engine.Verify(opts).ok());

  RestoreOptions ropts{};
  ropts.encryption_password = "legacy-pass";
  ASSERT_TRUE(engine.Restore(dest, ropts).ok());
}

TEST(DigestDualEncryptTest, StandardEncryptedRoundTrip) {
  const std::string repo = test::TempDir("dual_enc_standard_repo");
  const std::string source = test::TempDir("dual_enc_standard_source");
  const std::string dest = test::TempDir("dual_enc_standard_dest");
  ASSERT_TRUE(test::InitStandardRepo(repo).ok());
  test::WriteFile(source + "/secret.bin", test::MakeSyntheticData(64 * 1024, 7));

  BackupOptions opts{};
  opts.use_encryption = true;
  opts.encryption_password = "standard-pass";
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());
  ASSERT_TRUE(engine.Verify(opts).ok());

  RestoreOptions ropts{};
  ropts.encryption_password = "standard-pass";
  ASSERT_TRUE(engine.Restore(dest, ropts).ok());
}

TEST(DigestDualEncryptTest, DigestFlagMismatchUsesWrongKdfPath) {
  const std::string repo = test::TempDir("dual_enc_wrong_repo");
  const std::string source = test::TempDir("dual_enc_wrong_source");
  const std::string dest = test::TempDir("dual_enc_wrong_dest");
  ASSERT_TRUE(test::InitStandardRepo(repo).ok());
  test::WriteFile(source + "/secret.bin", test::MakeSyntheticData(32 * 1024, 8));

  BackupOptions opts{};
  opts.use_encryption = true;
  opts.encryption_password = "same-pass";
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());

  BackupSuperBlockStore sb_store(repo + "/superblock.bin");
  BackupSuperBlock sb{};
  ASSERT_TRUE(sb_store.Load(&sb).ok());
  sb.ext.backup_features &= ~kBackupFeatureDigestStandard;
  ASSERT_TRUE(sb_store.Commit(sb).ok());

  BackupEngine legacy_reader(repo);
  ASSERT_TRUE(legacy_reader.Open().ok());
  EXPECT_EQ(legacy_reader.digest_algo(), DigestAlgo::kLegacy);
  RestoreOptions ropts{};
  ropts.encryption_password = "same-pass";
  ASSERT_TRUE(legacy_reader.Restore(dest, ropts).ok());
}

}  // namespace
}  // namespace ebbackup
