#include <gtest/gtest.h>

#include <vector>

#include "ebbackup/codec/zstd_codec.h"

namespace ebbackup {
namespace {

TEST(ZstdCodecTest, RoundTrip) {
  std::vector<uint8_t> data(16384);
  for (size_t i = 0; i < data.size(); ++i) {
    data[i] = static_cast<uint8_t>((i * 17) & 0xFF);
  }
  ZstdEncodeResult encoded{};
  ASSERT_TRUE(ZstdCompress(data.data(), data.size(), 1, &encoded).ok());
  ASSERT_TRUE(encoded.compressed);
  std::vector<uint8_t> decoded;
  ASSERT_TRUE(
      ZstdDecompress(encoded.payload.data(), encoded.payload.size(),
                     encoded.uncompressed_size, &decoded)
          .ok());
  EXPECT_EQ(decoded, data);
}

}  // namespace
}  // namespace ebbackup
