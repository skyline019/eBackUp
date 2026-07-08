#include <gtest/gtest.h>

#include <set>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/pipeline/pipeline_phase_stats.h"
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

TEST(PipelineV4ChunkParityTest, LargeFileMatchesSequential) {
  constexpr size_t kFileSize = 4 * 1024 * 1024;
  const std::string source = test::TempDir("pipeline_v4_source");
  test::WriteFile(source + "/large.bin", test::MakeSyntheticData(kFileSize, 21));

  const std::string repo_seq = test::TempDir("pipeline_v4_seq");
  const std::string repo_v4 = test::TempDir("pipeline_v4_pipe");
  ASSERT_TRUE(test::InitDefaultRepo(repo_seq).ok());
  ASSERT_TRUE(test::InitDefaultRepo(repo_v4).ok());

  BackupOptions seq_opts{};
  seq_opts.disable_pipeline = true;

  BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;
  pipe_opts.worker_count = 4;

  {
    BackupEngine engine(repo_seq);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, seq_opts).ok());
  }
  {
    BackupEngine engine(repo_v4);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, pipe_opts).ok());
  }

  ManifestDocument seq_doc;
  ManifestDocument pipe_doc;
  ASSERT_TRUE(ReadManifestAuto(repo_seq + "/manifest", &seq_doc).ok());
  ASSERT_TRUE(ReadManifestAuto(repo_v4 + "/manifest", &pipe_doc).ok());

  const ManifestFileEntry* seq_file = test::FindManifestFile(seq_doc, "large.bin");
  const ManifestFileEntry* pipe_file = test::FindManifestFile(pipe_doc, "large.bin");
  ASSERT_NE(seq_file, nullptr);
  ASSERT_NE(pipe_file, nullptr);
  EXPECT_EQ(pipe_file->relative_path, seq_file->relative_path);
  EXPECT_EQ(pipe_file->size, seq_file->size);
  EXPECT_EQ(pipe_file->chunk_hashes_hex, seq_file->chunk_hashes_hex);
  EXPECT_EQ(CollectChunkHashes(pipe_doc), CollectChunkHashes(seq_doc));
}

TEST(PipelineV4ChunkParityTest, StreamingFileVerify) {
  constexpr size_t kFileSize = 32 * 1024 * 1024;
  const std::string source = test::TempDir("pipeline_v4_stream_source");
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(kFileSize, 22));

  const std::string repo = test::TempDir("pipeline_v4_stream_repo");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, pipe_opts).ok());
  ASSERT_TRUE(engine.Verify().ok());
}

TEST(PipelineV4ChunkParityTest, Streaming256MBMatchesSequential) {
  constexpr size_t kFileSize = 256 * 1024 * 1024;
  const std::string source = test::TempDir("pipeline_v4_stream256_source");
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(kFileSize, 23));

  const std::string repo_seq = test::TempDir("pipeline_v4_stream256_seq");
  const std::string repo_v4 = test::TempDir("pipeline_v4_stream256_pipe");
  ASSERT_TRUE(test::InitDefaultRepo(repo_seq).ok());
  ASSERT_TRUE(test::InitDefaultRepo(repo_v4).ok());

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
    BackupEngine engine(repo_v4);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, pipe_opts).ok());
    ASSERT_TRUE(engine.Verify().ok());
  }

  ManifestDocument seq_doc;
  ManifestDocument pipe_doc;
  ASSERT_TRUE(ReadManifestAuto(repo_seq + "/manifest", &seq_doc).ok());
  ASSERT_TRUE(ReadManifestAuto(repo_v4 + "/manifest", &pipe_doc).ok());

  const ManifestFileEntry* seq_file = test::FindManifestFile(seq_doc, "data.bin");
  const ManifestFileEntry* pipe_file = test::FindManifestFile(pipe_doc, "data.bin");
  ASSERT_NE(seq_file, nullptr);
  ASSERT_NE(pipe_file, nullptr);
  EXPECT_EQ(pipe_file->chunk_hashes_hex, seq_file->chunk_hashes_hex);
  EXPECT_EQ(pipe_file->size, seq_file->size);
}

TEST(Pipeline256UsesStreamingPathTest, PhaseStatsNonZero) {
  constexpr size_t kFileSize = 256 * 1024 * 1024;
  const std::string source = test::TempDir("pipeline_256_streaming_source");
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(kFileSize, 26));

  const std::string repo = test::TempDir("pipeline_256_streaming_repo");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, pipe_opts).ok());

  const PipelinePhaseStats& ps = engine.pipeline_phase_stats();
  EXPECT_GT(ps.read_ns.load(), 0u);
  EXPECT_GT(ps.chunk_ns.load(), 0u);
  EXPECT_GT(ps.encode_ns.load(), 0u);
  EXPECT_TRUE(ps.stream_cdc_ns.load() > 0u || ps.hybrid_cuts_ns.load() > 0u);
}

TEST(PipelineFinalizeRaceTest, StreamingMultiStoreVerify) {
  constexpr size_t kFileSize = 32 * 1024 * 1024;
  const std::string source = test::TempDir("pipeline_finalize_race_source");
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(kFileSize, 24));

  const std::string repo = test::TempDir("pipeline_finalize_race_repo");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;
  pipe_opts.store_shard_count = 16;

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, pipe_opts).ok());
  ASSERT_TRUE(engine.Verify().ok());
}

TEST(PipelineV4MultiFileTest, EightFilesMatchSequential) {
  const std::string source = test::TempDir("pipeline_v4_multi_source");
  for (int i = 0; i < 8; ++i) {
    const std::string name = "f" + std::to_string(i) + ".bin";
    test::WriteFile(source + "/" + name,
                    test::MakeSyntheticData(256 * 1024, static_cast<uint8_t>(30 + i)));
  }

  const std::string repo_seq = test::TempDir("pipeline_v4_multi_seq");
  const std::string repo_v4 = test::TempDir("pipeline_v4_multi_pipe");
  ASSERT_TRUE(test::InitDefaultRepo(repo_seq).ok());
  ASSERT_TRUE(test::InitDefaultRepo(repo_v4).ok());

  BackupOptions seq_opts{};
  seq_opts.disable_pipeline = true;

  BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;
  pipe_opts.worker_count = 4;

  {
    BackupEngine engine(repo_seq);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, seq_opts).ok());
  }
  {
    BackupEngine engine(repo_v4);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, pipe_opts).ok());
  }

  ManifestDocument seq_doc;
  ManifestDocument pipe_doc;
  ASSERT_TRUE(ReadManifestAuto(repo_seq + "/manifest", &seq_doc).ok());
  ASSERT_TRUE(ReadManifestAuto(repo_v4 + "/manifest", &pipe_doc).ok());

  EXPECT_EQ(CollectChunkHashes(pipe_doc), CollectChunkHashes(seq_doc));
  EXPECT_EQ(pipe_doc.files.size(), seq_doc.files.size());
}

TEST(PipelineV4MultiFileTest, MoreThanQueueDepthDefaultWorkers) {
  const std::string source = test::TempDir("pipeline_v4_queue_source");
  for (int i = 0; i < 40; ++i) {
    const std::string name = "f" + std::to_string(i) + ".bin";
    test::WriteFile(source + "/" + name,
                    test::MakeSyntheticData(4096, static_cast<uint8_t>(40 + i)));
  }

  const std::string repo = test::TempDir("pipeline_v4_queue_repo");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, pipe_opts).ok());

  ManifestDocument doc;
  ASSERT_TRUE(ReadManifestAuto(repo + "/manifest", &doc).ok());
  EXPECT_GE(doc.files.size(), 40u);
}

TEST(PipelineV4MultiFileTest, MixedSmallAndLargeFileTest) {
  const std::string source = test::TempDir("pipeline_v4_mixed_source");
  for (int i = 0; i < 8; ++i) {
    const std::string name = "small" + std::to_string(i) + ".bin";
    test::WriteFile(source + "/" + name,
                    test::MakeSyntheticData(256 * 1024, static_cast<uint8_t>(50 + i)));
  }
  test::WriteFile(source + "/large.bin",
                  test::MakeSyntheticData(128 * 1024 * 1024, 58));

  const std::string repo_seq = test::TempDir("pipeline_v4_mixed_seq");
  const std::string repo_pipe = test::TempDir("pipeline_v4_mixed_pipe");
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
    ASSERT_TRUE(engine.Verify().ok());
  }

  ManifestDocument seq_doc;
  ManifestDocument pipe_doc;
  ASSERT_TRUE(ReadManifestAuto(repo_seq + "/manifest", &seq_doc).ok());
  ASSERT_TRUE(ReadManifestAuto(repo_pipe + "/manifest", &pipe_doc).ok());

  EXPECT_EQ(CollectChunkHashes(pipe_doc), CollectChunkHashes(seq_doc));
  EXPECT_EQ(pipe_doc.files.size(), seq_doc.files.size());
}

}  // namespace
}  // namespace ebbackup
