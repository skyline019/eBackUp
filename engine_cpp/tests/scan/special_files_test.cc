#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

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
#endif

}  // namespace
}  // namespace ebbackup
