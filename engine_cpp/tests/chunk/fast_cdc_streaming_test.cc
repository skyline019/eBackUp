#include <gtest/gtest.h>

#include "ebbackup/chunk/fast_cdc.h"
#include "ebbackup/chunk/fast_cdc_streaming.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(FastCdcStreamingParityTest, MatchesFullChunk) {
  const std::string data = test::MakeSyntheticData(8 * 1024 * 1024, 31);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  const size_t len = data.size();

  FastCdcSlice chunker;
  std::vector<ChunkDescriptor> full;
  ASSERT_TRUE(chunker.Chunk(bytes, len, &full).ok());

  FastCdcStreamState state{};
  FastCdcStreamInit(&state, chunker.config());
  std::vector<ChunkDescriptor> streamed;
  size_t off = 0;
  constexpr size_t kFeed = 16u * 1024u * 1024u;
  while (off < len) {
    const size_t n = std::min(kFeed, len - off);
    const bool last = (off + n >= len);
    ASSERT_TRUE(FastCdcStreamFeed(&state, bytes + off, n, last, &streamed).ok());
    off += n;
  }

  ASSERT_EQ(streamed.size(), full.size());
  for (size_t i = 0; i < full.size(); ++i) {
    EXPECT_EQ(streamed[i].offset, full[i].offset);
    EXPECT_EQ(streamed[i].length, full[i].length);
    EXPECT_EQ(std::memcmp(streamed[i].hash, full[i].hash, 32), 0) << "chunk " << i;
  }
}

TEST(FastCdcStreamingParityTest, MatchesFullChunkLarge) {
  const std::string data = test::MakeSyntheticData(256 * 1024 * 1024, 31);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  const size_t len = data.size();

  FastCdcSlice chunker;
  std::vector<ChunkDescriptor> full;
  ASSERT_TRUE(chunker.Chunk(bytes, len, &full).ok());

  FastCdcStreamState state{};
  FastCdcStreamInit(&state, chunker.config());
  std::vector<ChunkDescriptor> streamed;
  size_t off = 0;
  constexpr size_t kFeed = 16u * 1024u * 1024u;
  while (off < len) {
    const size_t n = std::min(kFeed, len - off);
    const bool last = (off + n >= len);
    ASSERT_TRUE(FastCdcStreamFeed(&state, bytes + off, n, last, &streamed).ok());
    off += n;
  }

  ASSERT_EQ(streamed.size(), full.size());
  for (size_t i = 0; i < full.size(); ++i) {
    EXPECT_EQ(streamed[i].offset, full[i].offset);
    EXPECT_EQ(streamed[i].length, full[i].length);
    EXPECT_EQ(std::memcmp(streamed[i].hash, full[i].hash, 32), 0) << "chunk " << i;
  }
}

TEST(FastCdcStreamingParityTest, MatchesFullChunk256MBWith32MBFeeds) {
  const std::string data = test::MakeSyntheticData(256 * 1024 * 1024, 31);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  const size_t len = data.size();

  FastCdcSlice chunker;
  std::vector<ChunkDescriptor> full;
  ASSERT_TRUE(chunker.Chunk(bytes, len, &full).ok());

  FastCdcStreamState state{};
  FastCdcStreamInit(&state, chunker.config());
  std::vector<ChunkDescriptor> streamed;
  size_t off = 0;
  constexpr size_t kFeed = 32u * 1024u * 1024u;
  while (off < len) {
    const size_t n = std::min(kFeed, len - off);
    const bool last = (off + n >= len);
    ASSERT_TRUE(FastCdcStreamFeed(&state, bytes + off, n, last, &streamed).ok());
    off += n;
  }

  ASSERT_EQ(streamed.size(), full.size());
  for (size_t i = 0; i < full.size(); ++i) {
    EXPECT_EQ(streamed[i].offset, full[i].offset);
    EXPECT_EQ(streamed[i].length, full[i].length);
    EXPECT_EQ(std::memcmp(streamed[i].hash, full[i].hash, 32), 0) << "chunk " << i;
  }
}

TEST(FastCdcStreamingParityTest, CarryBufferNeverExceedsMaxSize) {
  FastCdcConfig cfg{};
  cfg.min_size = 64u * 1024u;
  cfg.avg_size = 256u * 1024u;
  cfg.max_size = 1024u * 1024u;

  const std::string data = test::MakeSyntheticData(64 * 1024 * 1024, 17);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  const size_t len = data.size();

  FastCdcStreamState state{};
  FastCdcStreamInit(&state, cfg);
  std::vector<ChunkDescriptor> streamed;
  size_t off = 0;
  constexpr size_t kFeed = 32u * 1024u * 1024u;
  while (off < len) {
    const size_t n = std::min(kFeed, len - off);
    const bool last = (off + n >= len);
    ASSERT_TRUE(FastCdcStreamFeed(&state, bytes + off, n, last, &streamed).ok());
    EXPECT_LE(state.carry.size(), static_cast<size_t>(cfg.max_size));
    off += n;
  }
}

TEST(FastCdcStreamingParityTest, MatchesFullChunk256MBWith32MBFeedsAndDigestBase) {
  const std::string data = test::MakeSyntheticData(256 * 1024 * 1024, 31);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  const size_t len = data.size();

  FastCdcSlice chunker;
  std::vector<ChunkDescriptor> full;
  ASSERT_TRUE(chunker.Chunk(bytes, len, &full).ok());

  FastCdcStreamState state{};
  FastCdcStreamInit(&state, chunker.config());
  state.digest_base = bytes;
  std::vector<ChunkDescriptor> streamed;
  size_t off = 0;
  constexpr size_t kFeed = 32u * 1024u * 1024u;
  while (off < len) {
    const size_t n = std::min(kFeed, len - off);
    const bool last = (off + n >= len);
    ASSERT_TRUE(FastCdcStreamFeed(&state, bytes + off, n, last, &streamed).ok());
    off += n;
  }

  ASSERT_EQ(streamed.size(), full.size());
  for (size_t i = 0; i < full.size(); ++i) {
    EXPECT_EQ(streamed[i].offset, full[i].offset);
    EXPECT_EQ(streamed[i].length, full[i].length);
    EXPECT_EQ(std::memcmp(streamed[i].hash, full[i].hash, 32), 0) << "chunk " << i;
  }
}

TEST(FastCdcStreamingParityTest, DigestBaseReducesDigestTimeOnCarryFeeds) {
  const std::string data = test::MakeSyntheticData(256 * 1024 * 1024, 31);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  const size_t len = data.size();

  auto run_stream = [&](bool use_digest_base) -> uint64_t {
    FastCdcSlice chunker;
    FastCdcStreamState state{};
    FastCdcStreamInit(&state, chunker.config());
    if (use_digest_base) state.digest_base = bytes;
    std::vector<ChunkDescriptor> streamed;
    size_t off = 0;
    constexpr size_t kFeed = 32u * 1024u * 1024u;
    while (off < len) {
      const size_t n = std::min(kFeed, len - off);
      const bool last = (off + n >= len);
      if (!FastCdcStreamFeed(&state, bytes + off, n, last, &streamed).ok()) {
        return 0;
      }
      off += n;
    }
    return state.profile.digest_ns;
  };

  const uint64_t without_base = run_stream(false);
  const uint64_t with_base = run_stream(true);
  ASSERT_GT(without_base, 0u);
  ASSERT_GT(with_base, 0u);
  EXPECT_LT(with_base, without_base / 2)
      << "digest_base should cut digest time on carry feeds";
}

}  // namespace
}  // namespace ebbackup
