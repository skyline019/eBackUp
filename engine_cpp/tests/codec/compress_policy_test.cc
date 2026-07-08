#include <gtest/gtest.h>

#include "ebbackup/codec/compress_policy.h"
#include "ebbackup/codec/content_class.h"

namespace ebbackup {
namespace {

TEST(CompressPolicyTest, FastTierDefaults) {
  const TierZstdParams params = ResolveTierParams(CompressTier::kFast, 0);
  EXPECT_EQ(params.fast_class_level, 1);
  EXPECT_EQ(params.slow_class_level, 1);
  EXPECT_FALSE(params.enable_ldm);
  EXPECT_TRUE(params.prefer_lz4_for_fast);
}

TEST(CompressPolicyTest, BalancedTierUsesHigherLevels) {
  const TierZstdParams params = ResolveTierParams(CompressTier::kBalanced, 0);
  EXPECT_EQ(params.fast_class_level, 3);
  EXPECT_EQ(params.slow_class_level, 6);
  EXPECT_TRUE(params.enable_ldm);
  EXPECT_TRUE(ShouldEnableLdm(256u * 1024u, params));
  EXPECT_FALSE(ShouldEnableLdm(32u * 1024u, params));
}

TEST(CompressPolicyTest, LevelOverrideAppliesToBothClasses) {
  const TierZstdParams params = ResolveTierParams(CompressTier::kBalanced, 9);
  EXPECT_EQ(ResolveZstdLevel(ContentDataClass::kFastCompressible, params), 9);
  EXPECT_EQ(ResolveZstdLevel(ContentDataClass::kSlowCompressible, params), 9);
}

TEST(CompressPolicyTest, CpuBudgetScalesWithLevel) {
  EXPECT_GT(CpuBudgetZstdCost(256u * 1024u, 6),
              CpuBudgetZstdCost(256u * 1024u, 1));
}

}  // namespace
}  // namespace ebbackup
