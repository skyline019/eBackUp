#include <gtest/gtest.h>

#include "ebbackup/scan/backup_filter.h"
#include "ebbackup/scan/scan_entry.h"
#include "test_util.h"

namespace ebbackup {
namespace {

ScanEntry MakeEntry(const std::string& rel, uint64_t size, int64_t mtime,
                    uint32_t uid = 1000) {
  ScanEntry e{};
  e.relative_path = rel;
  e.absolute_path = "/src/" + rel;
  e.type = FileType::kRegular;
  e.size = size;
  e.mtime_unix = mtime;
  e.uid = uid;
  return e;
}

TEST(BackupFilterTest, ExtensionFilter) {
  std::vector<ScanEntry> entries{
      MakeEntry("a.txt", 10, 100),
      MakeEntry("b.log", 10, 100),
  };
  BackupFilterOptions filter{};
  filter.extensions = {".txt"};
  ASSERT_TRUE(ApplyBackupFilter(filter, &entries).ok());
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].relative_path, "a.txt");
}

TEST(BackupFilterTest, MinMaxSizeFilter) {
  std::vector<ScanEntry> entries{
      MakeEntry("small.bin", 5, 100),
      MakeEntry("big.bin", 500, 100),
  };
  BackupFilterOptions filter{};
  filter.min_size = 10;
  filter.max_size = 400;
  ASSERT_TRUE(ApplyBackupFilter(filter, &entries).ok());
  ASSERT_EQ(entries.size(), 0u);
}

TEST(BackupFilterTest, MtimeFilter) {
  std::vector<ScanEntry> entries{
      MakeEntry("old.txt", 10, 10),
      MakeEntry("new.txt", 10, 1000),
  };
  BackupFilterOptions filter{};
  filter.mtime_after = 100;
  ASSERT_TRUE(ApplyBackupFilter(filter, &entries).ok());
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].relative_path, "new.txt");
}

TEST(BackupFilterTest, UidFilter) {
  std::vector<ScanEntry> entries{
      MakeEntry("u1.txt", 10, 100, 1000),
      MakeEntry("u2.txt", 10, 100, 2000),
  };
  BackupFilterOptions filter{};
  filter.uid_filter = 2000;
  ASSERT_TRUE(ApplyBackupFilter(filter, &entries).ok());
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].relative_path, "u2.txt");
}

TEST(BackupFilterTest, IncludeExcludePaths) {
  std::vector<ScanEntry> entries{
      MakeEntry("keep/a.txt", 10, 100),
      MakeEntry("drop/b.txt", 10, 100),
  };
  BackupFilterOptions filter{};
  filter.include_paths = {"keep"};
  filter.exclude_paths = {"drop"};
  ASSERT_TRUE(ApplyBackupFilter(filter, &entries).ok());
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].relative_path, "keep/a.txt");
}

TEST(BackupFilterTest, NameGlobFilter) {
  std::vector<ScanEntry> entries{
      MakeEntry("tmp/cache.tmp", 10, 100),
      MakeEntry("data.bin", 10, 100),
  };
  BackupFilterOptions filter{};
  filter.name_globs = {"*.tmp"};
  ASSERT_TRUE(ApplyBackupFilter(filter, &entries).ok());
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].relative_path, "data.bin");
}

TEST(BackupFilterTest, IncludeExcludeGlobFilter) {
  std::vector<ScanEntry> entries{
      MakeEntry("src/keep.cpp", 10, 100),
      MakeEntry("src/drop.tmp", 10, 100),
      MakeEntry("bin/app.exe", 10, 100),
  };
  BackupFilterOptions filter{};
  filter.include_globs = {"*.cpp", "*.exe"};
  filter.exclude_globs = {"*.tmp"};
  ASSERT_TRUE(ApplyBackupFilter(filter, &entries).ok());
  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries[0].relative_path, "src/keep.cpp");
  EXPECT_EQ(entries[1].relative_path, "bin/app.exe");
}

TEST(BackupFilterTest, FullPathGlobInclude) {
  std::vector<ScanEntry> entries{
      MakeEntry("src/a.cpp", 10, 100),
      MakeEntry("lib/src/a.cpp", 10, 100),
      MakeEntry("src/b.txt", 10, 100),
  };
  BackupFilterOptions filter{};
  filter.include_globs = {"src/*.cpp"};
  ASSERT_TRUE(ApplyBackupFilter(filter, &entries).ok());
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].relative_path, "src/a.cpp");
}

TEST(BackupFilterTest, BasenameGlobBackwardCompat) {
  std::vector<ScanEntry> entries{
      MakeEntry("foo/bar.tmp", 10, 100),
      MakeEntry("deep/nested/cache.tmp", 10, 100),
      MakeEntry("data.bin", 10, 100),
  };
  BackupFilterOptions filter{};
  filter.exclude_globs = {"*.tmp"};
  ASSERT_TRUE(ApplyBackupFilter(filter, &entries).ok());
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].relative_path, "data.bin");
}

TEST(BackupFilterTest, MtimeBeforeFilter) {
  std::vector<ScanEntry> entries{
      MakeEntry("old.txt", 10, 10),
      MakeEntry("new.txt", 10, 1000),
  };
  BackupFilterOptions filter{};
  filter.mtime_before = 500;
  ASSERT_TRUE(ApplyBackupFilter(filter, &entries).ok());
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].relative_path, "old.txt");
}

TEST(BackupFilterTest, CombinedFilters) {
  std::vector<ScanEntry> entries{
      MakeEntry("a.txt", 100, 1000, 1000),
      MakeEntry("b.txt", 100, 1000, 2000),
      MakeEntry("c.log", 100, 1000, 1000),
  };
  BackupFilterOptions filter{};
  filter.extensions = {".txt"};
  filter.uid_filter = 1000;
  filter.min_size = 50;
  ASSERT_TRUE(ApplyBackupFilter(filter, &entries).ok());
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].relative_path, "a.txt");
}

TEST(BackupFilterTest, ManifestFilterAddsAncestors) {
  std::vector<ManifestFileEntry> all;
  ManifestFileEntry dir;
  dir.relative_path = "keep";
  dir.file_type = FileType::kDirectory;
  ManifestFileEntry nested_dir;
  nested_dir.relative_path = "keep/nested";
  nested_dir.file_type = FileType::kDirectory;
  ManifestFileEntry file;
  file.relative_path = "keep/nested/file.txt";
  file.file_type = FileType::kRegular;
  file.size = 10;
  all = {dir, nested_dir, file};

  BackupFilterOptions filter{};
  filter.include_paths = {"keep/nested"};
  std::vector<ManifestFileEntry> out;
  ASSERT_TRUE(ApplyManifestFilter(filter, all, &out).ok());
  EXPECT_EQ(out.size(), 3u);
}

}  // namespace
}  // namespace ebbackup
