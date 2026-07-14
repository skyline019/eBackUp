#include <gtest/gtest.h>

#include "ebbackup/chunk/topo_cdc_internal.h"

namespace ebbackup {
namespace {

TEST(TopoGearParityTest, KeyedGearTableGolden) {
  uint32_t gear[256]{};
  topo_cdc_internal::InitGearTable(gear, 0x12345678u);
  EXPECT_EQ(gear[0], 0x906B6FD1u);
  EXPECT_EQ(gear[1], 0x6F0BD112u);
  EXPECT_EQ(gear[255], 0xE88AFFBAu);
}

TEST(TopoGearParityTest, ExportFixtureForPython) {
  GTEST_SKIP() << "Python eval deprecated; C++ golden KeyedGearTableGolden is SSOT";
}
}  // namespace
}  // namespace ebbackup
