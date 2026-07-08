#include <gtest/gtest.h>

#include "ebsync/backoff.h"

TEST(BackoffTest, GrowsAndCaps) {
  EXPECT_EQ(ebsync::ComputeBackoffSeconds(1), 1);
  EXPECT_EQ(ebsync::ComputeBackoffSeconds(2), 2);
  EXPECT_EQ(ebsync::ComputeBackoffSeconds(3), 4);
  EXPECT_EQ(ebsync::ComputeBackoffSeconds(10), 300);
}
