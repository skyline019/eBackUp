#include <gtest/gtest.h>

#include <string>
#include <vector>

#ifdef _WIN32
#include "ebbackup/winmeta/vss_shadow_storage.h"
#endif

namespace ebbackup {
namespace {

#ifdef _WIN32
TEST(VssShadowStorageTest, QueryDoesNotCrash) {
  std::vector<winmeta::VssShadowStorageInfo> entries;
  const Status st = winmeta::QueryShadowStorage(&entries);
  EXPECT_TRUE(st.ok());
  const std::string json = winmeta::FormatShadowStorageStatusJson(entries);
  EXPECT_FALSE(json.empty());
  EXPECT_NE(json.find("\"entries\""), std::string::npos);
}
#endif

}  // namespace
}  // namespace ebbackup
