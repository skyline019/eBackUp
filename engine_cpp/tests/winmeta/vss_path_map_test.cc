#include <gtest/gtest.h>

#include "ebbackup/winmeta/vss_session.h"

namespace ebbackup {
namespace winmeta {
namespace {

TEST(VssPathMapTest, MapLogicalToShadow) {
  const std::vector<VssVolumeMap> maps = {
      {"C:/", "//?/GLOBALROOT/Device/HarddiskVolumeShadowCopy1/"},
  };
  EXPECT_EQ(MapPathWithVolumeMaps("C:/Data/app.db", maps, true),
            "//?/GLOBALROOT/Device/HarddiskVolumeShadowCopy1/Data/app.db");
  EXPECT_EQ(MapPathWithVolumeMaps("C:/", maps, true),
            "//?/GLOBALROOT/Device/HarddiskVolumeShadowCopy1/");
}

TEST(VssPathMapTest, MapShadowToLogical) {
  const std::vector<VssVolumeMap> maps = {
      {"C:/", "//?/GLOBALROOT/Device/HarddiskVolumeShadowCopy2/"},
  };
  EXPECT_EQ(
      MapPathWithVolumeMaps(
          "//?/GLOBALROOT/Device/HarddiskVolumeShadowCopy2/Users/x.txt", maps,
          false),
      "C:/Users/x.txt");
}

TEST(VssPathMapTest, LongestPrefixWins) {
  const std::vector<VssVolumeMap> maps = {
      {"D:/", "//?/GLOBALROOT/Device/HarddiskVolumeShadowCopy1/"},
      {"D:/Projects/", "//?/GLOBALROOT/Device/HarddiskVolumeShadowCopy1/Projects/"},
  };
  EXPECT_EQ(MapPathWithVolumeMaps("D:/Projects/foo/bar.txt", maps, true),
            "//?/GLOBALROOT/Device/HarddiskVolumeShadowCopy1/Projects/foo/"
            "bar.txt");
}

TEST(VssPathMapTest, UnmappedPathPassthrough) {
  const std::vector<VssVolumeMap> maps = {
      {"C:/", "//?/GLOBALROOT/Device/HarddiskVolumeShadowCopy1/"},
  };
  EXPECT_EQ(MapPathWithVolumeMaps("E:/other.txt", maps, true), "E:/other.txt");
}

}  // namespace
}  // namespace winmeta
}  // namespace ebbackup
