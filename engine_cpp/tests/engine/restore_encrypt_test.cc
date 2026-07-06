#include <gtest/gtest.h>

#include <fstream>

#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(RestoreEncryptTest, EncryptedBackupRoundTrip) {
  const std::string repo = test::TempDir("enc_repo");
  const std::string source = test::TempDir("enc_source");
  const std::string dest = test::TempDir("enc_dest");
  ASSERT_TRUE(BackupEngine::InitRepo(repo).ok());
  test::WriteFile(source + "/secret.bin", test::MakeSyntheticData(1024 * 1024, 9));

  BackupOptions opts{};
  opts.use_encryption = true;
  opts.encryption_password = "course-password";
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());

  BackupOptions verify_opts{};
  verify_opts.encryption_password = "course-password";
  ASSERT_TRUE(engine.Verify(verify_opts).ok());

  RestoreOptions restore_opts{};
  restore_opts.encryption_password = "course-password";
  ASSERT_TRUE(engine.Restore(dest, restore_opts).ok());

  std::ifstream in(dest + "/secret.bin", std::ios::binary);
  const std::string got((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
  EXPECT_EQ(got.size(), 1024u * 1024u);
}

TEST(RestoreEncryptTest, WrongPasswordVerifyFails) {
  const std::string repo = test::TempDir("enc_bad_repo");
  const std::string source = test::TempDir("enc_bad_source");
  ASSERT_TRUE(BackupEngine::InitRepo(repo).ok());
  test::WriteFile(source + "/x.bin", "abc");

  BackupOptions opts{};
  opts.use_encryption = true;
  opts.encryption_password = "right-password";
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());

  BackupOptions bad{};
  bad.encryption_password = "wrong-password";
  EXPECT_FALSE(engine.Verify(bad).ok());
}

}  // namespace
}  // namespace ebbackup
