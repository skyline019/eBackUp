#include <gtest/gtest.h>

#include "ebbackup/winmeta/vss_session.h"

namespace ebbackup {
namespace winmeta {
namespace {

TEST(VssSessionStubTest, CheckPrerequisitesNonWindowsOrStub) {
#ifndef _WIN32
  const Status st = VssSession::CheckPrerequisites();
  EXPECT_FALSE(st.ok());
  EXPECT_NE(st.message().find("Windows"), std::string::npos);
#endif
}

TEST(VssSessionStubTest, BeginRejectedWithoutWindowsImplementation) {
#ifndef _WIN32
  VssSession session;
  const Status st = session.Begin({"/tmp/source"}, VssBeginOptions{});
  EXPECT_FALSE(st.ok());
  EXPECT_FALSE(session.active());
#endif
}

TEST(VssSessionStubTest, MapPassthroughWhenInactive) {
  VssSession session;
  EXPECT_EQ(session.MapToShadow("C:/Data/file.txt"), "C:/Data/file.txt");
  EXPECT_EQ(session.MapToLogicalForReport("C:/Data/file.txt"), "C:/Data/file.txt");
}

}  // namespace
}  // namespace winmeta
}  // namespace ebbackup
