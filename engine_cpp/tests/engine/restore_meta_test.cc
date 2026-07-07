#include <gtest/gtest.h>

#include <fstream>

#ifndef _WIN32
#include <sys/stat.h>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(RestoreMetaTest, MtimeAndModeRoundTrip) {
  const std::string repo = test::TempDir("meta_repo");
  const std::string source = test::TempDir("meta_source");
  const std::string dest = test::TempDir("meta_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/meta.txt", "meta-payload");
#ifndef _WIN32
  chmod((source + "/meta.txt").c_str(), 0640);
  struct stat src_st {};
  ASSERT_EQ(stat((source + "/meta.txt").c_str(), &src_st), 0);
#endif

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  ASSERT_TRUE(engine.Verify().ok());
  ASSERT_TRUE(engine.Restore(dest).ok());

  std::ifstream restored(dest + "/meta.txt", std::ios::binary);
  std::string got((std::istreambuf_iterator<char>(restored)),
                  std::istreambuf_iterator<char>());
  EXPECT_EQ(got, "meta-payload");

#ifndef _WIN32
  struct stat dst_st {};
  ASSERT_EQ(stat((dest + "/meta.txt").c_str(), &dst_st), 0);
  EXPECT_EQ(dst_st.st_mode & 0777, src_st.st_mode & 0777);
  EXPECT_EQ(dst_st.st_mtime, src_st.st_mtime);
#else
  DWORD attrs = GetFileAttributesA((dest + "/meta.txt").c_str());
  ASSERT_NE(attrs, INVALID_FILE_ATTRIBUTES);
  EXPECT_EQ(attrs & FILE_ATTRIBUTE_READONLY, 0u);
#endif
}

}  // namespace
}  // namespace ebbackup
