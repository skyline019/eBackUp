#include <gtest/gtest.h>

#include <set>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "test_util.h"

namespace ebbackup {
namespace {

std::set<std::string> CollectChunkHashes(const ManifestDocument& doc) {
  std::set<std::string> hashes;
  for (const auto& file : doc.files) {
    for (const auto& h : file.chunk_hashes_hex) {
      hashes.insert(h);
    }
  }
  return hashes;
}

TEST(PipelineV3Test, WorkerCountsMatchSequential) {
  const std::string source = test::TempDir("pipeline_v3_source");
  test::WriteFile(source + "/a.bin", test::MakeSyntheticData(768 * 1024, 5));
  test::WriteFile(source + "/b.bin", test::MakeSyntheticData(512 * 1024, 6));
  test::WriteFile(source + "/c.bin", test::MakeSyntheticData(256 * 1024, 7));

  const std::string repo_seq = test::TempDir("pipeline_v3_seq");
  const std::string repo_w4 = test::TempDir("pipeline_v3_w4");
  const std::string repo_w8 = test::TempDir("pipeline_v3_w8");
  ASSERT_TRUE(test::InitDefaultRepo(repo_seq).ok());
  ASSERT_TRUE(test::InitDefaultRepo(repo_w4).ok());
  ASSERT_TRUE(test::InitDefaultRepo(repo_w8).ok());

  BackupOptions seq_opts{};
  seq_opts.disable_pipeline = true;

  BackupOptions pipe4{};
  pipe4.use_pipeline = true;
  pipe4.worker_count = 4;

  BackupOptions pipe8{};
  pipe8.use_pipeline = true;
  pipe8.worker_count = 8;

  {
    BackupEngine engine(repo_seq);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, seq_opts).ok());
  }
  {
    BackupEngine engine(repo_w4);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, pipe4).ok());
  }
  {
    BackupEngine engine(repo_w8);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, pipe8).ok());
  }

  ManifestDocument seq_doc;
  ManifestDocument doc4;
  ManifestDocument doc8;
  ASSERT_TRUE(ReadManifestAuto(repo_seq + "/manifest", &seq_doc).ok());
  ASSERT_TRUE(ReadManifestAuto(repo_w4 + "/manifest", &doc4).ok());
  ASSERT_TRUE(ReadManifestAuto(repo_w8 + "/manifest", &doc8).ok());

  const auto seq_hashes = CollectChunkHashes(seq_doc);
  EXPECT_EQ(CollectChunkHashes(doc4), seq_hashes);
  EXPECT_EQ(CollectChunkHashes(doc8), seq_hashes);
}

}  // namespace
}  // namespace ebbackup
