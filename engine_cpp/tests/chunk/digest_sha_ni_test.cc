#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "ebbackup/common/digest.h"
#include "ebbackup/common/digest_sha_ni.h"
#include "ebbackup/common/digest_standard.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(DigestShaNiTest, MatchesStandardKnownVector) {
  const char* msg = "abc";
  uint8_t ref[32];
  uint8_t ni[32];
  Sha256Standard(reinterpret_cast<const uint8_t*>(msg), 3, ref);
  Sha256ShaNi(reinterpret_cast<const uint8_t*>(msg), 3, ni);
  EXPECT_EQ(std::memcmp(ref, ni, 32), 0);
}

TEST(DigestShaNiTest, MatchesStandardSynthetic) {
  const std::string data = test::MakeSyntheticData(4 * 1024 * 1024, 17);
  uint8_t ref[32];
  uint8_t ni[32];
  Sha256Standard(reinterpret_cast<const uint8_t*>(data.data()), data.size(), ref);
  Sha256ShaNi(reinterpret_cast<const uint8_t*>(data.data()), data.size(), ni);
  EXPECT_EQ(std::memcmp(ref, ni, 32), 0);
}

TEST(DigestShaNiTest, ContentHashDispatchParity) {
  const std::string data = test::MakeSyntheticData(1024 * 1024, 42);
  uint8_t a[32];
  uint8_t b[32];
  ContentHash(DigestAlgo::kStandard, reinterpret_cast<const uint8_t*>(data.data()),
              data.size(), a);
  Sha256Standard(reinterpret_cast<const uint8_t*>(data.data()), data.size(), b);
  EXPECT_EQ(std::memcmp(a, b, 32), 0);
}

}  // namespace
}  // namespace ebbackup
