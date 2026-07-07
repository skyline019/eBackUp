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
#else
void SetEnvVar(const char* key, const char* value) { setenv(key, value, 1); }
#endif

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

  ASSERT_EQ(seq_doc.files.size(), 1u);
  ASSERT_EQ(stream_doc.files.size(), 1u);
  EXPECT_EQ(stream_doc.files[0].chunk_hashes_hex,
            seq_doc.files[0].chunk_hashes_hex);
  EXPECT_EQ(stream_doc.files[0].size, seq_doc.files[0].size);
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
    SetEnvVar("EBBACKUP_FORCE_STREAM_CDC", "0");
    SetEnvVar("EBBACKUP_CDC_FAST_SLICE", "1");
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, pipe_opts).ok());
  }
  {
    BackupEngine engine(repo_stream);
    ASSERT_TRUE(engine.Open().ok());
    SetEnvVar("EBBACKUP_FORCE_STREAM_CDC", "1");
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, pipe_opts).ok());
    ASSERT_TRUE(engine.Verify().ok());
  }

  ManifestDocument fast_doc;
  ManifestDocument stream_doc;
  ASSERT_TRUE(ReadManifestAuto(repo_fast + "/manifest", &fast_doc).ok());
  ASSERT_TRUE(ReadManifestAuto(repo_stream + "/manifest", &stream_doc).ok());

  ASSERT_EQ(fast_doc.files.size(), 1u);
  ASSERT_EQ(stream_doc.files.size(), 1u);
  EXPECT_EQ(fast_doc.files[0].chunk_hashes_hex,
            stream_doc.files[0].chunk_hashes_hex);
  EXPECT_EQ(fast_doc.files[0].size, stream_doc.files[0].size);
  SetEnvVar("EBBACKUP_FORCE_STREAM_CDC", "0");
  SetEnvVar("EBBACKUP_CDC_FAST_SLICE", "0");
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
  EXPECT_GT(single_ps.stream_cdc_ns.load(), 0u);

  SetEnvVar("EBBACKUP_FORCE_STREAM_CDC", "1");
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
  EXPECT_EQ(multi_doc.files.size(), 8u);
}

}  // namespace
}  // namespace ebbackup
