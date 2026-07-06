#include <gtest/gtest.h>

#include "ebbackup/codec/lz4_codec.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(Lz4CodecTest, RoundTrip) {
  const std::string data = test::MakeSyntheticData(8192, 7);
  Lz4EncodeResult encoded{};
  ASSERT_TRUE(Lz4Compress(reinterpret_cast<const uint8_t*>(data.data()),
                          data.size(), &encoded)
                  .ok());
  EXPECT_TRUE(encoded.compressed);
  std::vector<uint8_t> decoded;
  ASSERT_TRUE(Lz4Decompress(encoded.payload.data(), encoded.payload.size(),
                              encoded.uncompressed_size, &decoded)
                  .ok());
  EXPECT_EQ(decoded.size(), data.size());
  EXPECT_EQ(decoded, std::vector<uint8_t>(data.begin(), data.end()));
}

TEST(Lz4CodecTest, ExpandSkipStoresRaw) {
  const std::string tiny = "tiny";
  Lz4EncodeResult encoded{};
  ASSERT_TRUE(Lz4Compress(reinterpret_cast<const uint8_t*>(tiny.data()),
                          tiny.size(), &encoded)
                  .ok());
  EXPECT_FALSE(encoded.compressed);
  EXPECT_EQ(encoded.payload.size(), tiny.size());
}

TEST(Lz4CodecTest, CorruptPayloadFails) {
  const std::string data = test::MakeSyntheticData(4096, 2);
  Lz4EncodeResult encoded{};
  ASSERT_TRUE(Lz4Compress(reinterpret_cast<const uint8_t*>(data.data()),
                          data.size(), &encoded)
                  .ok());
  ASSERT_TRUE(encoded.compressed);
  encoded.payload[0] ^= 0xFF;
  std::vector<uint8_t> decoded;
  EXPECT_EQ(Lz4Decompress(encoded.payload.data(), encoded.payload.size(),
                          encoded.uncompressed_size, &decoded)
                .code(),
            StatusCode::kCorrupt);
}

}  // namespace
}  // namespace ebbackup
