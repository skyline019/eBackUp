#include <gtest/gtest.h>

#include "ebbackup/scan/filter_loader.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(FilterLoaderTest, LoadFilterFromFileRoundTrip) {
  const std::string path = test::TempDir("filter_loader") + "/rules.conf";
  test::WriteFile(path, "include_glob=*.txt\nexclude_glob=*.tmp\nmin_size=100\n");

  BackupFilterOptions filter{};
  ASSERT_TRUE(LoadFilterFromFile(path, &filter).ok());
  ASSERT_EQ(filter.include_globs.size(), 1u);
  ASSERT_EQ(filter.exclude_globs.size(), 1u);
  EXPECT_EQ(filter.min_size, 100u);
}

}  // namespace
}  // namespace ebbackup
