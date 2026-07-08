#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/engine/restore_engine.h"
#include "ebbackup/winmeta/win_meta.h"
#include "test_util.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace ebbackup {
namespace {

TEST(SpecialFilesTest, DirectoryTreeRestore) {
  const std::string repo = test::TempDir("special_dir_repo");
  const std::string source = test::TempDir("special_dir_source");
  const std::string dest = test::TempDir("special_dir_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  std::error_code ec;
  std::filesystem::create_directories(source + "/nested/empty", ec);
  test::WriteFile(source + "/nested/file.txt", "inside");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  ASSERT_TRUE(engine.Verify().ok());
  ASSERT_TRUE(engine.Restore(dest).ok());

  EXPECT_TRUE(std::filesystem::is_directory(dest + "/nested/empty"));
  EXPECT_TRUE(std::filesystem::is_regular_file(dest + "/nested/file.txt"));
  std::ifstream in(dest + "/nested/file.txt");
  std::string got((std::istreambuf_iterator<char>(in)),
                  std::istreambuf_iterator<char>());
  EXPECT_EQ(got, "inside");
}

TEST(SpecialFilesTest, SymlinkRestore) {
  const std::string repo = test::TempDir("special_link_repo");
  const std::string source = test::TempDir("special_link_source");
  const std::string dest = test::TempDir("special_link_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/target.txt", "target-data");
  std::error_code ec;
  std::filesystem::create_symlink("target.txt", source + "/link.txt", ec);
  if (ec) GTEST_SKIP() << "symlink not supported: " << ec.message();

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  ASSERT_TRUE(engine.Verify().ok());
  ASSERT_TRUE(engine.Restore(dest).ok());

  EXPECT_TRUE(std::filesystem::is_symlink(dest + "/link.txt"));
  EXPECT_TRUE(std::filesystem::is_regular_file(dest + "/target.txt"));
}

#ifdef _WIN32
TEST(SpecialFilesTest, JunctionRestore) {
  const std::string repo = test::TempDir("special_junc_repo");
  const std::string source = test::TempDir("special_junc_source");
  const std::string dest = test::TempDir("special_junc_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  std::error_code ec;
  std::filesystem::create_directories(source + "/real_dir", ec);
  test::WriteFile(source + "/real_dir/data.txt", "junction-target");

  const std::string junction = source + "/junction";
  const std::string cmd =
      "cmd /c mklink /J \"" + junction + "\" \"" + source + "\\real_dir\" >nul 2>&1";
  if (std::system(cmd.c_str()) != 0) {
    GTEST_SKIP() << "junction creation not supported";
  }

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  ASSERT_TRUE(engine.Verify().ok());
  ASSERT_TRUE(engine.Restore(dest).ok());

  EXPECT_TRUE(std::filesystem::exists(dest + "/real_dir/data.txt"));
  std::ifstream in(dest + "/real_dir/data.txt");
  std::string got((std::istreambuf_iterator<char>(in)),
                  std::istreambuf_iterator<char>());
  EXPECT_EQ(got, "junction-target");
}

TEST(SpecialFilesTest, JunctionRecreateRestore) {
  const std::string repo = test::TempDir("special_junc_rec_repo");
  const std::string source = test::TempDir("special_junc_rec_source");
  const std::string dest = test::TempDir("special_junc_rec_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  std::error_code ec;
  std::filesystem::create_directories(source + "/real_dir", ec);
  test::WriteFile(source + "/real_dir/data.txt", "junction-target");

  const std::string junction = source + "/junction";
  const std::string cmd =
      "cmd /c mklink /J \"" + junction + "\" \"" + source + "\\real_dir\" >nul 2>&1";
  if (std::system(cmd.c_str()) != 0) {
    GTEST_SKIP() << "junction creation not supported";
  }

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  ManifestDocument doc;
  ASSERT_TRUE(engine.LoadManifest(0, &doc).ok());
  bool has_reparse = false;
  for (const auto& file : doc.files) {
    if (file.reparse_tag != 0) {
      has_reparse = true;
      EXPECT_FALSE(file.reparse_target.empty());
      EXPECT_NE(file.relative_path, "real_dir");
    }
  }
  ASSERT_TRUE(has_reparse) << "reparse directory missing from manifest";

  RestoreOptions recreate_opts{};
  recreate_opts.reparse_policy.mode =
      winmeta::ReparseRestorePolicy::Mode::kRecreate;
  recreate_opts.verify_subset_merkle = false;
  recreate_opts.verify_restored_content = false;
  ASSERT_TRUE(engine.Restore(dest, recreate_opts).ok());

  std::string junction_rel;
  for (const auto& file : doc.files) {
    if (file.reparse_tag != 0) {
      junction_rel = file.relative_path;
      break;
    }
  }
  ASSERT_FALSE(junction_rel.empty());
  const std::string junction_dest = dest + "/" + junction_rel;

  const DWORD attrs = GetFileAttributesA(junction_dest.c_str());
  ASSERT_NE(attrs, INVALID_FILE_ATTRIBUTES);
  EXPECT_TRUE(attrs & FILE_ATTRIBUTE_REPARSE_POINT);
  EXPECT_TRUE(std::filesystem::exists(dest + "/real_dir/data.txt"));

  std::ifstream via_junction(junction_dest + "/data.txt");
  std::string got((std::istreambuf_iterator<char>(via_junction)),
                  std::istreambuf_iterator<char>());
  EXPECT_EQ(got, "junction-target");
}
#endif

}  // namespace
}  // namespace ebbackup
