#include <gtest/gtest.h>

#include <set>

#include "chaos_util.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "subprocess_util.h"
#include "tree_util.h"
#include "test_util.h"

namespace ebbackup {
namespace {

std::set<std::string> ManifestPaths(const ManifestDocument& doc) {
  std::set<std::string> paths;
  for (const auto& f : doc.files) paths.insert(f.relative_path);
  return paths;
}

TEST(PipelineResilienceTest, PipelineMatchesSequentialNested) {
  const std::string source = test::TempDir("pipe_res_src");
  ASSERT_TRUE(test::BuildNestedTree(source, 4, 2, 8192).ok());

  const std::string repo_seq = test::TempDir("pipe_res_seq");
  const std::string repo_pipe = test::TempDir("pipe_res_pipe");
  ASSERT_TRUE(test::InitDefaultRepo(repo_seq).ok());
  ASSERT_TRUE(test::InitDefaultRepo(repo_pipe).ok());

  BackupEngine seq(repo_seq);
  BackupEngine pipe(repo_pipe);
  ASSERT_TRUE(seq.Open().ok());
  ASSERT_TRUE(pipe.Open().ok());

  BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;
  BackupOptions seq_opts{};
  seq_opts.disable_pipeline = true;
  ASSERT_TRUE(seq.RunBackup(source).ok());
  ASSERT_TRUE(pipe.RunBackup(source, BackupMode::kFull, pipe_opts).ok());

  ManifestDocument seq_doc;
  ManifestDocument pipe_doc;
  ASSERT_TRUE(ReadManifestAuto(repo_seq + "/manifest", &seq_doc).ok());
  ASSERT_TRUE(ReadManifestAuto(repo_pipe + "/manifest", &pipe_doc).ok());
  EXPECT_EQ(ManifestPaths(seq_doc), ManifestPaths(pipe_doc));
}

TEST(PipelineResilienceTest, PipelineIncrementalReuse) {
  const std::string source = test::TempDir("pipe_reuse_src");
  const std::string repo = test::TempDir("pipe_reuse_repo");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/a.bin", test::MakeSyntheticData(256 * 1024, 1));

  BackupOptions opts{};
  opts.use_pipeline = true;
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental, opts).ok());
  EXPECT_GT(engine.stats().chunks_reused + engine.stats().chunks_reused_from_cfi, 0u);
}

TEST(PipelineResilienceTest, PipelineSubprocessKillRecover) {
  const std::string repo = test::TempDir("pipe_kill_repo");
  const std::string source = test::TempDir("pipe_kill_src");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/big.bin", test::MakeSyntheticData(64 * 1024 * 1024, 3));

  BackupOptions opts{};
  opts.use_pipeline = true;
  ASSERT_TRUE(test::RunBackupSubprocessAndKill(repo, source, BackupMode::kFull, opts, 10).ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  if (engine.phase() != BackupPhase::kIdle &&
      engine.phase() != BackupPhase::kComplete) {
    ASSERT_TRUE(engine.Recover().ok());
  }
  const Status verify_st = engine.Verify();
  EXPECT_TRUE(verify_st.ok() || engine.phase() == BackupPhase::kAborted);
}

TEST(PipelineResilienceTest, PipelineBalancedDurabilityPowerfail) {
  const std::string repo = test::TempDir("pipe_bal_repo");
  const std::string source = test::TempDir("pipe_bal_src");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(512 * 1024, 4));

  BackupOptions opts{};
  opts.use_pipeline = true;
  opts.durability = DurabilityMode::kBalanced;
  {
    BackupEngine engine(repo);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());
  }
  ASSERT_TRUE(test::InjectPhase(repo, BackupPhase::kStoring, 2).ok());
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  EXPECT_EQ(engine.phase(), BackupPhase::kAborted);
  ASSERT_TRUE(engine.Verify().ok());
}

}  // namespace
}  // namespace ebbackup
