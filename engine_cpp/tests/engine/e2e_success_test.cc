#include <gtest/gtest.h>

#include <fstream>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/store/snapshot_store.h"
#include "tree_util.h"
#include "test_util.h"

#ifndef EBTEST_EB_EXE
#error "EBTEST_EB_EXE must be defined"
#endif

namespace ebbackup {
namespace test {
namespace {

std::string QuotePath(const std::string& path) {
  return "\"" + path + "\"";
}

int RunEb(const std::string& args) {
  return std::system((std::string(EBTEST_EB_EXE) + " " + args).c_str());
}

TEST(E2eSuccessTest, FullIncrementalChainVerifyRestore) {
  const std::string repo = TempDir("e2e_chain_repo");
  const std::string source = TempDir("e2e_chain_src");
  const std::string dest = TempDir("e2e_chain_dest");
  ASSERT_TRUE(InitDefaultRepo(repo).ok());
  ASSERT_TRUE(BuildNestedTree(source, 3, 2, 4096).ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull).ok());
  test::WriteFile(source + "/d0_0/d1_0/leaf.bin",
                  MakeSyntheticData(4096, 99));
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental).ok());
  test::WriteFile(source + "/d0_0/d1_1/leaf.bin",
                  MakeSyntheticData(4096, 100));
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental).ok());
  ASSERT_TRUE(engine.Verify().ok());
  ASSERT_TRUE(engine.Restore(dest).ok());
  ASSERT_TRUE(CompareDirectoryTrees(source, dest).ok());
}

TEST(E2eSuccessTest, SnapshotTimelineRestoreAllTxns) {
  const std::string repo = TempDir("e2e_snap_repo");
  const std::string source = TempDir("e2e_snap_src");
  ASSERT_TRUE(InitDefaultRepo(repo).ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  test::WriteFile(source + "/version.txt", "v1");
  ASSERT_TRUE(engine.RunBackup(source).ok());
  std::vector<SnapshotEntry> snaps;
  ASSERT_TRUE(ListSnapshots(repo, &snaps).ok());
  ASSERT_EQ(snaps.size(), 1u);
  const uint64_t txn1 = snaps[0].txn_id;

  test::WriteFile(source + "/version.txt", "v2");
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental).ok());
  snaps.clear();
  ASSERT_TRUE(ListSnapshots(repo, &snaps).ok());
  ASSERT_EQ(snaps.size(), 2u);
  const uint64_t txn2 = snaps.back().txn_id;

  const std::string dest1 = TempDir("e2e_snap_dest1");
  const std::string dest2 = TempDir("e2e_snap_dest2");
  RestoreOptions r1{};
  r1.snapshot_txn_id = txn1;
  RestoreOptions r2{};
  r2.snapshot_txn_id = txn2;
  ASSERT_TRUE(engine.Restore(dest1, r1).ok());
  ASSERT_TRUE(engine.Restore(dest2, r2).ok());
  std::ifstream in1(dest1 + "/version.txt");
  std::ifstream in2(dest2 + "/version.txt");
  std::string s1;
  std::string s2;
  std::getline(in1, s1);
  std::getline(in2, s2);
  EXPECT_EQ(s1, "v1");
  EXPECT_EQ(s2, "v2");
}

TEST(E2eSuccessTest, PipelineAutoCompressSuccess) {
  const std::string repo = TempDir("e2e_pipe_repo");
  const std::string source = TempDir("e2e_pipe_src");
  const std::string dest = TempDir("e2e_pipe_dest");
  ASSERT_TRUE(InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/data.txt", test::MakeSyntheticData(128 * 1024, 5));

  BackupOptions opts{};
  opts.use_pipeline = true;
  opts.compress_mode = CompressMode::kAuto;
  opts.cpu_budget_permille = 600;
  opts.durability = DurabilityMode::kBalanced;

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());
  ASSERT_TRUE(engine.Verify().ok());
  ASSERT_TRUE(engine.Restore(dest).ok());
  ASSERT_TRUE(CompareDirectoryTrees(source, dest).ok());
}

TEST(E2eSuccessTest, EncryptedIncrementalSuccess) {
  const std::string repo = TempDir("e2e_enc_repo");
  const std::string source = TempDir("e2e_enc_src");
  const std::string dest = TempDir("e2e_enc_dest");
  ASSERT_TRUE(InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/secret.txt", "secret-v1");

  BackupOptions opts{};
  opts.use_encryption = true;
  opts.encryption_password = "good-pass";

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());
  test::WriteFile(source + "/secret.txt", "secret-v2");
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental, opts).ok());

  RestoreOptions bad{};
  bad.encryption_password = "wrong-pass";
  EXPECT_FALSE(engine.Restore(dest, bad).ok());

  RestoreOptions good{};
  good.encryption_password = "good-pass";
  ASSERT_TRUE(engine.Restore(dest, good).ok());
  std::ifstream in(dest + "/secret.txt");
  std::string got;
  std::getline(in, got);
  EXPECT_EQ(got, "secret-v2");
}

TEST(E2eSuccessTest, CliBackupRestoreRoundTrip) {
  const std::string repo = TempDir("e2e_cli_repo");
  const std::string source = TempDir("e2e_cli_src");
  const std::string dest = TempDir("e2e_cli_dest");
  test::WriteFile(source + "/cli.txt", "cli-roundtrip");

  ASSERT_EQ(RunEb("init " + QuotePath(repo)), 0);
  ASSERT_EQ(RunEb("backup " + QuotePath(repo) + " " + QuotePath(source)), 0);
  ASSERT_EQ(RunEb("verify " + QuotePath(repo)), 0);
  ASSERT_EQ(RunEb("restore " + QuotePath(repo) + " " + QuotePath(dest)), 0);
  std::ifstream in(dest + "/cli.txt");
  std::string got;
  std::getline(in, got);
  EXPECT_EQ(got, "cli-roundtrip");
}

}  // namespace
}  // namespace test
}  // namespace ebbackup
