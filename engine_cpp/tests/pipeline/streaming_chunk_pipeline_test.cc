#include <gtest/gtest.h>

#include <cstdlib>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/pipeline/pipeline_phase_stats.h"
#include "test_util.h"

namespace ebbackup {
namespace {

#if defined(_WIN32)
void SetEnvVar(const char* key, const char* value) {
  _putenv_s(key, value);
}
void UnsetEnvVar(const char* key) { _putenv_s(key, ""); }
#else
void SetEnvVar(const char* key, const char* value) { setenv(key, value, 1); }
void UnsetEnvVar(const char* key) { unsetenv(key); }
#endif

const ManifestFileEntry* FindManifestFile(const ManifestDocument& doc,
                                          const std::string& rel_path) {
  const ManifestFileEntry* best = nullptr;
  for (const auto& file : doc.files) {
    if (file.relative_path != rel_path) continue;
    if (!best || (!file.chunk_hashes_hex.empty() && best->chunk_hashes_hex.empty())) {
      best = &file;
    }
  }
  return best;
}

TEST(StreamingChunkPipelineTest, Streaming256MBMatchesSequentialManifest) {
  constexpr size_t kFileSize = 256 * 1024 * 1024;
  const std::string source = test::TempDir("streaming_chunk_source");
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(kFileSize, 31));

  const std::string repo_seq = test::TempDir("streaming_chunk_seq");
  const std::string repo_stream = test::TempDir("streaming_chunk_stream");
  ASSERT_TRUE(test::InitDefaultRepo(repo_seq).ok());
  ASSERT_TRUE(test::InitDefaultRepo(repo_stream).ok());

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
    BackupEngine engine(repo_stream);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, pipe_opts).ok());
    ASSERT_TRUE(engine.Verify().ok());
  }

  ManifestDocument seq_doc;
  ManifestDocument stream_doc;
  ASSERT_TRUE(ReadManifestAuto(repo_seq + "/manifest", &seq_doc).ok());
  ASSERT_TRUE(ReadManifestAuto(repo_stream + "/manifest", &stream_doc).ok());

  const ManifestFileEntry* seq_file = FindManifestFile(seq_doc, "data.bin");
  const ManifestFileEntry* stream_file = FindManifestFile(stream_doc, "data.bin");
  ASSERT_NE(seq_file, nullptr);
  ASSERT_NE(stream_file, nullptr);
  EXPECT_EQ(stream_file->chunk_hashes_hex, seq_file->chunk_hashes_hex);
  EXPECT_EQ(stream_file->size, seq_file->size);
}

TEST(StreamingChunkPipelineTest, Streaming256PhaseStatsNonZero) {
  constexpr size_t kFileSize = 256 * 1024 * 1024;
  const std::string source = test::TempDir("streaming_chunk_stats_source");
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(kFileSize, 32));

  const std::string repo = test::TempDir("streaming_chunk_stats_repo");
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
}

TEST(StreamingChunkPipelineTest, CdcFastPathMatchesStreamFeedManifest) {
  constexpr size_t kFileSize = 256 * 1024 * 1024;
  const std::string source = test::TempDir("cdc_fast_path_source");
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(kFileSize, 34));

  const std::string repo_fast = test::TempDir("cdc_fast_path_fast");
  const std::string repo_stream = test::TempDir("cdc_fast_path_stream");
  ASSERT_TRUE(test::InitDefaultRepo(repo_fast).ok());
  ASSERT_TRUE(test::InitDefaultRepo(repo_stream).ok());

  BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;

  {
    BackupEngine engine(repo_fast);
    ASSERT_TRUE(engine.Open().ok());
    SetEnvVar("EBBACKUP_CDC_HYBRID", "0");
    SetEnvVar("EBBACKUP_FORCE_STREAM_CDC", "0");
    SetEnvVar("EBBACKUP_CDC_FAST_SLICE", "1");
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, pipe_opts).ok());
  }
  {
    BackupEngine engine(repo_stream);
    ASSERT_TRUE(engine.Open().ok());
    SetEnvVar("EBBACKUP_CDC_HYBRID", "0");
    SetEnvVar("EBBACKUP_FORCE_STREAM_CDC", "1");
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, pipe_opts).ok());
    ASSERT_TRUE(engine.Verify().ok());
  }

  ManifestDocument fast_doc;
  ManifestDocument stream_doc;
  ASSERT_TRUE(ReadManifestAuto(repo_fast + "/manifest", &fast_doc).ok());
  ASSERT_TRUE(ReadManifestAuto(repo_stream + "/manifest", &stream_doc).ok());

  const ManifestFileEntry* fast_file = FindManifestFile(fast_doc, "data.bin");
  const ManifestFileEntry* stream_file = FindManifestFile(stream_doc, "data.bin");
  ASSERT_NE(fast_file, nullptr);
  ASSERT_NE(stream_file, nullptr);
  EXPECT_EQ(fast_file->chunk_hashes_hex, stream_file->chunk_hashes_hex);
  EXPECT_EQ(fast_file->size, stream_file->size);
  UnsetEnvVar("EBBACKUP_CDC_HYBRID");
  SetEnvVar("EBBACKUP_FORCE_STREAM_CDC", "0");
  SetEnvVar("EBBACKUP_CDC_FAST_SLICE", "0");
}

TEST(StreamingChunkPipelineTest, Hybrid256MBMatchesSequentialManifest) {
  constexpr size_t kFileSize = 256 * 1024 * 1024;
  const std::string source = test::TempDir("hybrid_seq_source");
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(kFileSize, 36));

  const std::string repo_seq = test::TempDir("hybrid_seq_repo");
  const std::string repo_hybrid = test::TempDir("hybrid_cdc_repo");
  ASSERT_TRUE(test::InitDefaultRepo(repo_seq).ok());
  ASSERT_TRUE(test::InitDefaultRepo(repo_hybrid).ok());

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
    BackupEngine engine(repo_hybrid);
    ASSERT_TRUE(engine.Open().ok());
    UnsetEnvVar("EBBACKUP_CDC_HYBRID");
    SetEnvVar("EBBACKUP_CDC_FAST_SLICE", "0");
    SetEnvVar("EBBACKUP_FORCE_STREAM_CDC", "0");
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, pipe_opts).ok());
    ASSERT_TRUE(engine.Verify().ok());
    const PipelinePhaseStats& ps = engine.pipeline_phase_stats();
    EXPECT_GT(ps.hybrid_cuts_ns.load(), 0u);
    EXPECT_GT(ps.hybrid_replay_ns.load(), 0u);
  }

  ManifestDocument seq_doc;
  ManifestDocument hybrid_doc;
  ASSERT_TRUE(ReadManifestAuto(repo_seq + "/manifest", &seq_doc).ok());
  ASSERT_TRUE(ReadManifestAuto(repo_hybrid + "/manifest", &hybrid_doc).ok());

  const ManifestFileEntry* seq_file = FindManifestFile(seq_doc, "data.bin");
  const ManifestFileEntry* hybrid_file = FindManifestFile(hybrid_doc, "data.bin");
  ASSERT_NE(seq_file, nullptr);
  ASSERT_NE(hybrid_file, nullptr);
  EXPECT_EQ(hybrid_file->chunk_hashes_hex, seq_file->chunk_hashes_hex);
  UnsetEnvVar("EBBACKUP_CDC_HYBRID");
}

TEST(StreamingChunkPipelineTest, Hybrid256MBMatchesStreamManifest) {
  constexpr size_t kFileSize = 256 * 1024 * 1024;
  const std::string source = test::TempDir("hybrid_stream_source");
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(kFileSize, 37));

  const std::string repo_hybrid = test::TempDir("hybrid_stream_hybrid");
  const std::string repo_stream = test::TempDir("hybrid_stream_stream");
  ASSERT_TRUE(test::InitDefaultRepo(repo_hybrid).ok());
  ASSERT_TRUE(test::InitDefaultRepo(repo_stream).ok());

  BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;

  {
    BackupEngine engine(repo_hybrid);
    ASSERT_TRUE(engine.Open().ok());
    UnsetEnvVar("EBBACKUP_CDC_HYBRID");
    SetEnvVar("EBBACKUP_CDC_FAST_SLICE", "0");
    SetEnvVar("EBBACKUP_FORCE_STREAM_CDC", "0");
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, pipe_opts).ok());
  }
  {
    BackupEngine engine(repo_stream);
    ASSERT_TRUE(engine.Open().ok());
    SetEnvVar("EBBACKUP_CDC_HYBRID", "0");
    SetEnvVar("EBBACKUP_FORCE_STREAM_CDC", "0");
    SetEnvVar("EBBACKUP_CDC_FAST_SLICE", "0");
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, pipe_opts).ok());
    ASSERT_TRUE(engine.Verify().ok());
  }

  ManifestDocument hybrid_doc;
  ManifestDocument stream_doc;
  ASSERT_TRUE(ReadManifestAuto(repo_hybrid + "/manifest", &hybrid_doc).ok());
  ASSERT_TRUE(ReadManifestAuto(repo_stream + "/manifest", &stream_doc).ok());

  const ManifestFileEntry* hybrid_file = FindManifestFile(hybrid_doc, "data.bin");
  const ManifestFileEntry* stream_file = FindManifestFile(stream_doc, "data.bin");
  ASSERT_NE(hybrid_file, nullptr);
  ASSERT_NE(stream_file, nullptr);
  EXPECT_EQ(hybrid_file->chunk_hashes_hex, stream_file->chunk_hashes_hex);
  UnsetEnvVar("EBBACKUP_CDC_HYBRID");
}

TEST(StreamingChunkPipelineTest, UsesStreamingPathNotV4) {
  constexpr size_t kFileSize = 256 * 1024 * 1024;
  const std::string source_single =
      test::TempDir("streaming_chunk_single_source");
  test::WriteFile(source_single + "/data.bin",
                  test::MakeSyntheticData(kFileSize, 33));

  const std::string repo_single = test::TempDir("streaming_chunk_single_repo");
  ASSERT_TRUE(test::InitDefaultRepo(repo_single).ok());

  BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;
  SetEnvVar("EBBACKUP_FORCE_STREAM_CDC", "0");
  SetEnvVar("EBBACKUP_CDC_FAST_SLICE", "0");

  BackupEngine single_engine(repo_single);
  ASSERT_TRUE(single_engine.Open().ok());
  ASSERT_TRUE(
      single_engine.RunBackup(source_single, BackupMode::kFull, pipe_opts).ok());

  const PipelinePhaseStats& single_ps = single_engine.pipeline_phase_stats();
  EXPECT_GT(single_ps.chunk_ns.load(), 0u);
  EXPECT_GT(single_ps.encode_ns.load(), 0u);
  EXPECT_TRUE(single_ps.stream_cdc_ns.load() > 0u ||
              single_ps.hybrid_cuts_ns.load() > 0u);

  SetEnvVar("EBBACKUP_FORCE_STREAM_CDC", "1");
  SetEnvVar("EBBACKUP_CDC_HYBRID", "0");
  const std::string source_stream =
      test::TempDir("streaming_chunk_stream_sub_source");
  test::WriteFile(source_stream + "/data.bin",
                  test::MakeSyntheticData(kFileSize, 35));
  const std::string repo_stream_sub =
      test::TempDir("streaming_chunk_stream_sub_repo");
  ASSERT_TRUE(test::InitDefaultRepo(repo_stream_sub).ok());
  BackupEngine stream_sub_engine(repo_stream_sub);
  ASSERT_TRUE(stream_sub_engine.Open().ok());
  ASSERT_TRUE(
      stream_sub_engine.RunBackup(source_stream, BackupMode::kFull, pipe_opts).ok());
  EXPECT_GT(stream_sub_engine.pipeline_phase_stats().stream_cdc_ns.load(), 0u);
  SetEnvVar("EBBACKUP_FORCE_STREAM_CDC", "0");
  SetEnvVar("EBBACKUP_CDC_FAST_SLICE", "0");

  const std::string source_multi = test::TempDir("streaming_chunk_multi_source");
  for (int i = 0; i < 8; ++i) {
    const std::string name = "file" + std::to_string(i) + ".bin";
    test::WriteFile(source_multi + "/" + name,
                    test::MakeSyntheticData(32 * 1024 * 1024, static_cast<uint8_t>(40 + i)));
  }

  const std::string repo_multi = test::TempDir("streaming_chunk_multi_repo");
  ASSERT_TRUE(test::InitDefaultRepo(repo_multi).ok());

  BackupEngine multi_engine(repo_multi);
  ASSERT_TRUE(multi_engine.Open().ok());
  ASSERT_TRUE(
      multi_engine.RunBackup(source_multi, BackupMode::kFull, pipe_opts).ok());
  ASSERT_TRUE(multi_engine.Verify().ok());

  ManifestDocument multi_doc;
  ASSERT_TRUE(ReadManifestAuto(repo_multi + "/manifest", &multi_doc).ok());
  for (int i = 0; i < 8; ++i) {
    const std::string name = "file" + std::to_string(i) + ".bin";
    EXPECT_NE(FindManifestFile(multi_doc, name), nullptr);
  }
}

}  // namespace
}  // namespace ebbackup
