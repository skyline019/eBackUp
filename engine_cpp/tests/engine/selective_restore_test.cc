#include <gtest/gtest.h>

#include <fstream>

#include "ebbackup/audit/merkle.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/engine/restore_path_remap.h"
#include "ebbackup/scan/backup_filter.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(SelectiveRestoreTest, IncludePathRestoresSubtree) {
  const std::string repo = test::TempDir("sel_repo");
  const std::string source = test::TempDir("sel_source");
  const std::string dest = test::TempDir("sel_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/keep/nested.txt", "keep-data");
  test::WriteFile(source + "/drop/other.txt", "drop-data");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  ASSERT_TRUE(engine.Verify().ok());

  RestoreOptions opts{};
  opts.filter.include_paths = {"keep"};
  ASSERT_TRUE(engine.Restore(dest, opts).ok());

  EXPECT_TRUE(std::filesystem::exists(dest + "/keep/nested.txt"));
  EXPECT_FALSE(std::filesystem::exists(dest + "/drop/other.txt"));
  std::ifstream in(dest + "/keep/nested.txt");
  std::string got((std::istreambuf_iterator<char>(in)),
                  std::istreambuf_iterator<char>());
  EXPECT_EQ(got, "keep-data");

  EXPECT_TRUE(engine.has_restore_acceptance());
  const std::string report_json = engine.ExportRestoreReportJson();
  EXPECT_NE(report_json.find("ok"), std::string::npos);
  EXPECT_NE(report_json.find("merkle"), std::string::npos);
}

TEST(SelectiveRestoreTest, ExcludeGlobSkipsTmp) {
  const std::string repo = test::TempDir("sel_glob_repo");
  const std::string source = test::TempDir("sel_glob_source");
  const std::string dest = test::TempDir("sel_glob_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(4096, 1));
  test::WriteFile(source + "/cache.tmp", "tmp-data");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  RestoreOptions opts{};
  opts.filter.exclude_globs = {"*.tmp"};
  ASSERT_TRUE(engine.Restore(dest, opts).ok());

  EXPECT_TRUE(std::filesystem::exists(dest + "/data.bin"));
  EXPECT_FALSE(std::filesystem::exists(dest + "/cache.tmp"));
}

TEST(SelectiveRestoreTest, FullPathGlobInclude) {
  const std::string repo = test::TempDir("sel_pathglob_repo");
  const std::string source = test::TempDir("sel_pathglob_source");
  const std::string dest = test::TempDir("sel_pathglob_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/src/a.cpp", "cpp-data");
  test::WriteFile(source + "/lib/src/a.cpp", "nested-cpp");
  test::WriteFile(source + "/src/b.txt", "txt-data");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  RestoreOptions opts{};
  opts.filter.include_globs = {"src/*.cpp"};
  ASSERT_TRUE(engine.Restore(dest, opts).ok());

  EXPECT_TRUE(std::filesystem::exists(dest + "/src/a.cpp"));
  EXPECT_FALSE(std::filesystem::exists(dest + "/lib/src/a.cpp"));
  EXPECT_FALSE(std::filesystem::exists(dest + "/src/b.txt"));
}

TEST(SelectiveRestoreTest, PostRestoreContentMerkleMatches) {
  const std::string repo = test::TempDir("sel_merkle_repo");
  const std::string source = test::TempDir("sel_merkle_source");
  const std::string dest = test::TempDir("sel_merkle_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/keep/data.bin", test::MakeSyntheticData(8192, 4));
  test::WriteFile(source + "/drop/other.bin", test::MakeSyntheticData(4096, 5));

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  RestoreOptions opts{};
  opts.filter.include_paths = {"keep"};
  opts.verify_subset_merkle = true;
  ASSERT_TRUE(engine.Restore(dest, opts).ok());
  EXPECT_TRUE(std::filesystem::exists(dest + "/keep/data.bin"));
}

TEST(SelectiveRestoreTest, TamperedRestoreFailsContentMerkle) {
  const std::string repo = test::TempDir("sel_tamper_repo");
  const std::string source = test::TempDir("sel_tamper_source");
  const std::string dest = test::TempDir("sel_tamper_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/keep/data.bin", test::MakeSyntheticData(4096, 6));
  test::WriteFile(source + "/drop/other.bin", "other");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  RestoreOptions opts{};
  opts.filter.include_paths = {"keep"};
  opts.verify_subset_merkle = true;
  ASSERT_TRUE(engine.Restore(dest, opts).ok());

  std::fstream tamper(dest + "/keep/data.bin",
                      std::ios::in | std::ios::out | std::ios::binary);
  ASSERT_TRUE(tamper.good());
  char byte = 0;
  tamper.read(&byte, 1);
  tamper.seekp(0);
  tamper.put(static_cast<char>(byte ^ 0x5A));
  tamper.close();

  ManifestDocument doc;
  ASSERT_TRUE(ReadManifestAuto(repo + "/manifest", &doc).ok());
  std::vector<ManifestFileEntry> files = doc.files;
  BackupFilterOptions filter{};
  filter.include_paths = {"keep"};
  std::vector<ManifestFileEntry> filtered;
  ASSERT_TRUE(ApplyManifestFilter(filter, files, &filtered).ok());
  uint8_t root[32]{};
  EXPECT_FALSE(audit::ComputeMerkleRootFromRestoredFiles(
                   dest, filtered, engine.chunk_store(), root)
                   .ok());
}

TEST(SelectiveRestoreTest, FullRestoreVerifyContentOptIn) {
  const std::string repo = test::TempDir("sel_full_verify_repo");
  const std::string source = test::TempDir("sel_full_verify_source");
  const std::string dest = test::TempDir("sel_full_verify_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(8192, 20));

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  RestoreOptions opts{};
  opts.verify_restored_content = true;
  ASSERT_TRUE(engine.Restore(dest, opts).ok());
  EXPECT_TRUE(std::filesystem::exists(dest + "/data.bin"));
}

TEST(SelectiveRestoreTest, FullRestoreSkipVerifyByDefault) {
  const std::string repo = test::TempDir("sel_full_skip_repo");
  const std::string source = test::TempDir("sel_full_skip_source");
  const std::string dest = test::TempDir("sel_full_skip_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/quick.txt", "fast-restore");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  RestoreOptions opts{};
  ASSERT_TRUE(engine.Restore(dest, opts).ok());
  EXPECT_TRUE(std::filesystem::exists(dest + "/quick.txt"));
}

TEST(SelectiveRestoreTest, EncryptedSelectiveRestore) {
  const std::string repo = test::TempDir("sel_enc_repo");
  const std::string source = test::TempDir("sel_enc_source");
  const std::string dest = test::TempDir("sel_enc_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/secret.txt", test::MakeSyntheticData(8192, 3));
  test::WriteFile(source + "/public.txt", "public");

  BackupOptions backup_opts{};
  backup_opts.use_encryption = true;
  backup_opts.encryption_password = "sel-pass";
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, backup_opts).ok());

  RestoreOptions restore_opts{};
  restore_opts.encryption_password = "sel-pass";
  restore_opts.filter.include_globs = {"secret.txt"};
  ASSERT_TRUE(engine.Restore(dest, restore_opts).ok());

  EXPECT_TRUE(std::filesystem::exists(dest + "/secret.txt"));
  EXPECT_FALSE(std::filesystem::exists(dest + "/public.txt"));
  std::ifstream in(dest + "/secret.txt", std::ios::binary);
  const std::string got((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
  EXPECT_EQ(got.size(), 8192u);
}

TEST(SelectiveRestoreTest, StripPrefixWithMerkle) {
  const std::string repo = test::TempDir("sel_strip_repo");
  const std::string source = test::TempDir("sel_strip_source");
  const std::string dest = test::TempDir("sel_strip_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/keep/nested.txt", "strip-data");
  test::WriteFile(source + "/drop/other.txt", "drop-data");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  RestoreOptions opts{};
  opts.filter.include_paths = {"keep"};
  opts.path_remap.mode = RestoreLayoutMode::kStripPrefix;
  opts.path_remap.strip_prefix = "keep";
  ASSERT_TRUE(engine.Restore(dest, opts).ok());

  EXPECT_TRUE(std::filesystem::exists(dest + "/nested.txt"));
  EXPECT_FALSE(std::filesystem::exists(dest + "/drop/other.txt"));
  std::ifstream in(dest + "/nested.txt");
  std::string got((std::istreambuf_iterator<char>(in)),
                  std::istreambuf_iterator<char>());
  EXPECT_EQ(got, "strip-data");
}

}  // namespace
}  // namespace ebbackup
