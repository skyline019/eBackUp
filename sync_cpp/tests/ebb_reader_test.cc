#include <gtest/gtest.h>

#include <filesystem>

#include "ebbackup/archive/eb_bundle.h"
#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

#include "ebsync/ebb_reader.h"

namespace ebbackup {
namespace {

TEST(EbbReaderTest, ReadsDeltaBundle) {
  const std::string repo = test::TempDir("ebb_reader_repo");
  const std::string source = test::TempDir("ebb_reader_source");
  const std::string bundle = test::TempDir("ebb_reader_out") + "/delta.ebb";

  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/f.txt", "hello-sync-reader");
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  const uint64_t txn_a = engine.superblock().critical.txn_id;
  test::WriteFile(source + "/f.txt", "hello-sync-reader-v2");
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental).ok());
  const uint64_t txn_b = engine.superblock().critical.txn_id;

  EbBundleDeltaOptions opts{};
  opts.base_txn_id = txn_a;
  opts.target_txn_id = txn_b;
  ASSERT_TRUE(ExportRepoDeltaToBundle(repo, bundle, opts).ok());

  ebsync::EbbBundle parsed;
  std::string err;
  ASSERT_TRUE(ebsync::ReadEbbBundle(bundle, &parsed, &err)) << err;
  EXPECT_EQ(parsed.header.target_txn_id, txn_b);
  EXPECT_FALSE(parsed.toc.empty());
}

}  // namespace
}  // namespace ebbackup
