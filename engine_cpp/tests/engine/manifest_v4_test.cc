#include <gtest/gtest.h>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(ManifestV4Test, RoundtripBinaryHashes) {
  const std::string path = test::TempDir("manifest_v4") + "/manifest";
  ManifestDocument doc;
  doc.txn_id = 7;
  ManifestFileEntry file;
  file.relative_path = "docs/read me.txt";
  file.size = 100;
  file.chunk_hashes_hex = {
      "aa00000000000000000000000000000000000000000000000000000000000000",
      "bb00000000000000000000000000000000000000000000000000000000000000"};
  doc.files.push_back(file);

  ASSERT_TRUE(WriteManifestV4(path, doc).ok());
  ManifestDocument loaded;
  ASSERT_TRUE(ReadManifestAuto(path, &loaded).ok());
  ASSERT_EQ(loaded.txn_id, 7u);
  ASSERT_EQ(loaded.files.size(), 1u);
  EXPECT_EQ(loaded.files[0].relative_path, "docs/read me.txt");
  ASSERT_EQ(loaded.files[0].chunk_hashes_hex.size(), 2u);
}

TEST(ManifestV4Test, InitRepoUsesBinaryWhenRequested) {
  const std::string repo = test::TempDir("manifest_v4_init");
  RepoInitOptions opts{};
  opts.manifest_binary = true;
  opts.persistent_index = true;
  ASSERT_TRUE(BackupEngine::InitRepoEx(repo, opts).ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  EXPECT_TRUE(RepoUsesManifestBinary(engine.superblock()));
}

}  // namespace
}  // namespace ebbackup
