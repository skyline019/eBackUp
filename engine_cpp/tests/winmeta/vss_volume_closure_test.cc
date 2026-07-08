#include <gtest/gtest.h>

#include "ebbackup/winmeta/vss_session.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "ebbackup/winmeta/vss_volume_closure.h"
#endif

namespace ebbackup {
namespace winmeta {
namespace {

TEST(VssConsistencyModeTest, ParseAndFormat) {
  VssConsistencyMode mode{};
  EXPECT_TRUE(ParseVssConsistencyMode("crash", &mode));
  EXPECT_EQ(VssConsistencyModeToString(mode), "crash");
  EXPECT_TRUE(ParseVssConsistencyMode("app", &mode));
  EXPECT_EQ(VssConsistencyModeToString(mode), "app");
  EXPECT_TRUE(ParseVssConsistencyMode("auto", &mode));
  EXPECT_EQ(VssConsistencyModeToString(mode), "auto");
  EXPECT_FALSE(ParseVssConsistencyMode("invalid", &mode));
}

#ifdef _WIN32
TEST(VssVolumeClosureTest, ResolveVolumeForPathTemp) {
  char tmp[MAX_PATH + 1]{};
  ASSERT_GT(GetTempPathA(MAX_PATH, tmp), 0u);
  VssVolumeSpec spec{};
  ASSERT_TRUE(ResolveVolumeForPath(tmp, &spec).ok());
  EXPECT_FALSE(spec.volume_name_utf8.empty());
  EXPECT_FALSE(spec.mount_point_utf8.empty());
}

TEST(VssVolumeClosureTest, ComputeVolumeClosureDedupesRoots) {
  char tmp[MAX_PATH + 1]{};
  ASSERT_GT(GetTempPathA(MAX_PATH, tmp), 0u);
  VssVolumeClosureOptions opts{};
  std::vector<VssVolumeSpec> closure;
  ASSERT_TRUE(ComputeVolumeClosure({tmp, tmp}, opts, &closure).ok());
  ASSERT_EQ(closure.size(), 1u);
}

TEST(VssVolumeClosureTest, CrossVolumeWhenSecondDriveExists) {
  if (GetDriveTypeW(L"D:\\") != DRIVE_FIXED) {
    GTEST_SKIP() << "no fixed D: drive";
  }
  char tmp[MAX_PATH + 1]{};
  ASSERT_GT(GetTempPathA(MAX_PATH, tmp), 0u);
  VssVolumeClosureOptions opts{};
  opts.include_junction_volumes = false;
  std::vector<VssVolumeSpec> closure;
  const Status st = ComputeVolumeClosure({tmp, "D:\\"}, opts, &closure);
  ASSERT_TRUE(st.ok());
  EXPECT_GE(closure.size(), 2u);
}
#endif

}  // namespace
}  // namespace winmeta
}  // namespace ebbackup
