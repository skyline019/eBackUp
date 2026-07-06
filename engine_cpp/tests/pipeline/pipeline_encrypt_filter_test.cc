#include <gtest/gtest.h>

#include <fstream>
#include <set>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "test_util.h"

namespace ebbackup {
namespace {

std::set<std::string> ManifestPaths(const ManifestDocument& doc) {
  std::set<std::string> paths;
  for (const auto& file : doc.files) {
    paths.insert(file.relative_path);
  }
  return paths;
}

TEST(PipelineEncryptFilterTest, CombinedOptionsRoundTrip) {
  const std::string source = test::TempDir("combo_source");
  test::WriteFile(source + "/keep.txt", test::MakeSyntheticData(64 * 1024, 1));
  test::WriteFile(source + "/drop.tmp", "should-be-filtered");
  test::WriteFile(source + "/also.bin", test::MakeSyntheticData(32 * 1024, 2));

  const std::string repo = test::TempDir("combo_repo");
  const std::string dest = test::TempDir("combo_dest");
  ASSERT_TRUE(BackupEngine::InitRepo(repo).ok());

  BackupOptions opts{};
  opts.use_pipeline = true;
  opts.use_encryption = true;
  opts.encryption_password = "combo-pass";
  opts.filter.include_globs = {"keep.txt"};
  opts.filter.exclude_globs = {"*.tmp"};

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());

  BackupOptions verify_opts{};
  verify_opts.encryption_password = "combo-pass";
  ASSERT_TRUE(engine.Verify(verify_opts).ok());

  ManifestDocument doc;
  ASSERT_TRUE(ReadManifestAuto(repo + "/manifest", &doc).ok());
  const auto paths = ManifestPaths(doc);
  EXPECT_TRUE(paths.count("keep.txt") > 0);
  EXPECT_EQ(paths.count("drop.tmp"), 0u);
  EXPECT_EQ(paths.count("also.bin"), 0u);

  RestoreOptions restore_opts{};
  restore_opts.encryption_password = "combo-pass";
  ASSERT_TRUE(engine.Restore(dest, restore_opts).ok());

  std::ifstream in(dest + "/keep.txt", std::ios::binary);
  const std::string got((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
  EXPECT_EQ(got.size(), 64u * 1024u);
  EXPECT_FALSE(std::filesystem::exists(dest + "/drop.tmp"));
}

}  // namespace
}  // namespace ebbackup
