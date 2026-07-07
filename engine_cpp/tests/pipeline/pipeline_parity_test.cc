#include <gtest/gtest.h>

#include <fstream>
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

TEST(PipelineParityTest, SequentialMatchesPipeline) {
  const std::string source = test::TempDir("pipeline_parity_source");
  test::WriteFile(source + "/a.bin", test::MakeSyntheticData(512 * 1024, 1));
  test::WriteFile(source + "/b.bin", test::MakeSyntheticData(256 * 1024, 2));

  const std::string repo_seq = test::TempDir("pipeline_parity_seq");
  const std::string repo_pipe = test::TempDir("pipeline_parity_pipe");
  ASSERT_TRUE(test::InitDefaultRepo(repo_seq).ok());
  ASSERT_TRUE(test::InitDefaultRepo(repo_pipe).ok());

  BackupOptions seq_opts{};
  seq_opts.disable_pipeline = true;
  BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;

  {
    BackupEngine engine(repo_seq);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, seq_opts).ok());
  }
  {
    BackupEngine engine(repo_pipe);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, pipe_opts).ok());
  }

  ManifestDocument seq_doc;
  ManifestDocument pipe_doc;
  ASSERT_TRUE(ReadManifestAuto(repo_seq + "/manifest", &seq_doc).ok());
  ASSERT_TRUE(ReadManifestAuto(repo_pipe + "/manifest", &pipe_doc).ok());
  EXPECT_EQ(CollectChunkHashes(seq_doc), CollectChunkHashes(pipe_doc));
}

TEST(PipelineParityTest, PipelineLz4MatchesSequentialLz4) {
  const std::string source = test::TempDir("pipeline_lz4_source");
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(1024 * 1024, 9));

  const std::string repo_seq = test::TempDir("pipeline_lz4_seq");
  const std::string repo_pipe = test::TempDir("pipeline_lz4_pipe");
  ASSERT_TRUE(test::InitDefaultRepo(repo_seq).ok());
  ASSERT_TRUE(test::InitDefaultRepo(repo_pipe).ok());

  BackupOptions seq_opts{};
  seq_opts.use_lz4 = true;
  seq_opts.disable_pipeline = true;
  BackupOptions pipe_opts{};
  pipe_opts.use_lz4 = true;
  pipe_opts.use_pipeline = true;

  {
    BackupEngine engine(repo_seq);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, seq_opts).ok());
  }
  {
    BackupEngine engine(repo_pipe);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, pipe_opts).ok());
  }

  ManifestDocument seq_doc;
  ManifestDocument pipe_doc;
  ASSERT_TRUE(ReadManifestAuto(repo_seq + "/manifest", &seq_doc).ok());
  ASSERT_TRUE(ReadManifestAuto(repo_pipe + "/manifest", &pipe_doc).ok());
  EXPECT_EQ(CollectChunkHashes(seq_doc), CollectChunkHashes(pipe_doc));
}

}  // namespace
}  // namespace ebbackup
