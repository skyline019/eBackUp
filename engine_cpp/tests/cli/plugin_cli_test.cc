#include <gtest/gtest.h>

#include "ebbackup/plugin/plugin_registry.h"

#ifndef EBTEST_EB_EXE
#error "EBTEST_EB_EXE must be defined"
#endif

namespace ebbackup {
namespace test {
namespace {

TEST(PluginCliTest, ListBuiltinPlugins) {
  const int rc = std::system((std::string(EBTEST_EB_EXE) + " plugin list").c_str());
  EXPECT_EQ(rc, 0);
}

}  // namespace
}  // namespace test
}  // namespace ebbackup
