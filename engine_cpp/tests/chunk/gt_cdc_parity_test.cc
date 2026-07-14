#include <gtest/gtest.h>

#include <cstdlib>

#include "ebbackup/chunk/gt_cdc.h"
#include "ebbackup/chunk/chunk_profile.h"
#include "ebbackup/chunk/gt_cdc_internal.h"
#include "ebbackup/chunk/gt_cdc_streaming.h"
#include "test_util.h"

namespace ebbackup {
namespace {

#if defined(_WIN32)
void SetProfileEnv() { _putenv_s("EBBACKUP_PIPELINE_PROFILE", "1"); }
#else
void SetProfileEnv() { setenv("EBBACKUP_PIPELINE_PROFILE", "1", 1); }
#endif

void ExpectCutsEqual(const std::vector<size_t>& a_off,
                     const std::vector<uint32_t>& a_len,
                     const std::vector<size_t>& b_off,
                     const std::vector<uint32_t>& b_len) {
  ASSERT_EQ(a_off.size(), b_off.size());
  for (size_t i = 0; i < a_off.size(); ++i) {
    EXPECT_EQ(a_off[i], b_off[i]) << "offset index " << i;
    EXPECT_EQ(a_len[i], b_len[i]) << "length index " << i;
  }
}

TEST(GtCdcParityTest, ScalarMatchesTensor) {
  const std::string data = test::MakeSyntheticData(4 * 1024 * 1024, 23);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  GtCdcSlice chunker;
  std::vector<size_t> scalar_off;
  std::vector<uint32_t> scalar_len;
  std::vector<size_t> tensor_off;
  std::vector<uint32_t> tensor_len;
  ASSERT_TRUE(chunker.ChunkCutsScalar(bytes, data.size(), &scalar_off, &scalar_len)
                  .ok());
  ASSERT_TRUE(chunker.ChunkCuts(bytes, data.size(), &tensor_off, &tensor_len).ok());
  ExpectCutsEqual(scalar_off, scalar_len, tensor_off, tensor_len);
}

TEST(GtCdcParityTest, ScalarMatchesTensor256MB) {
  constexpr size_t kFileSize = 256 * 1024 * 1024;
  const std::string data = test::MakeSyntheticData(kFileSize, 23);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  GtCdcSlice chunker;
  std::vector<size_t> scalar_off;
  std::vector<uint32_t> scalar_len;
  std::vector<size_t> tensor_off;
  std::vector<uint32_t> tensor_len;
  ASSERT_TRUE(chunker.ChunkCutsScalar(bytes, kFileSize, &scalar_off, &scalar_len)
                  .ok());
  ASSERT_TRUE(chunker.ChunkCuts(bytes, kFileSize, &tensor_off, &tensor_len).ok());
  ExpectCutsEqual(scalar_off, scalar_len, tensor_off, tensor_len);
}

TEST(GtCdcParityTest, ScalarMatchesTensorAlignedBoundary) {
  constexpr size_t kFileSize = 256 * 1024 * 1024;
  std::string data = test::MakeSyntheticData(kFileSize, 23);
  for (size_t off : {0u, 64u, 63u, 4096u, 4095u}) {
    if (off >= data.size()) continue;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data()) + off;
    const size_t len = kFileSize - off;
    GtCdcSlice chunker;
    std::vector<size_t> scalar_off;
    std::vector<uint32_t> scalar_len;
    std::vector<size_t> tensor_off;
    std::vector<uint32_t> tensor_len;
    ASSERT_TRUE(chunker.ChunkCutsScalar(bytes, len, &scalar_off, &scalar_len).ok());
    ASSERT_TRUE(chunker.ChunkCuts(bytes, len, &tensor_off, &tensor_len).ok());
    ExpectCutsEqual(scalar_off, scalar_len, tensor_off, tensor_len);
  }
}

TEST(GtCdcParityTest, StreamingMatchesFullFileSmallFeed) {
  constexpr size_t kFileSize = 8 * 1024 * 1024;
  const std::string data = test::MakeSyntheticData(kFileSize, 67);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  GtCdcSlice chunker;
  std::vector<size_t> full_off;
  std::vector<uint32_t> full_len;
  ASSERT_TRUE(chunker.ChunkCuts(bytes, kFileSize, &full_off, &full_len).ok());

  GtCdcConfig cfg = chunker.config();
  GtCdcStreamState state{};
  GtCdcStreamInit(&state, cfg);
  state.digest_base = bytes;

  std::vector<size_t> stream_off;
  std::vector<uint32_t> stream_len;
  constexpr size_t kFeed = 512 * 1024;
  for (size_t feed_off = 0; feed_off < kFileSize; feed_off += kFeed) {
    const size_t n = std::min(kFeed, kFileSize - feed_off);
    const bool last = (feed_off + n >= kFileSize);
    std::vector<ChunkDescriptor> batch;
    ASSERT_TRUE(
        GtCdcStreamFeed(&state, bytes + feed_off, n, last, &batch).ok());
    for (const auto& d : batch) {
      stream_off.push_back(d.offset);
      stream_len.push_back(d.length);
    }
  }

  ExpectCutsEqual(full_off, full_len, stream_off, stream_len);
}

TEST(GtCdcParityTest, StreamingSmallFeedProfileCountersNonZero) {
  SetProfileEnv();
  constexpr size_t kFileSize = 4 * 1024 * 1024;
  const std::string data = test::MakeSyntheticData(kFileSize, 71);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  GtCdcConfig cfg = GtCdcConfigForFileSize(kFileSize, ChunkProfileMode::kDefault,
                                           DigestAlgo::kLegacy);
  GtCdcStreamState state{};
  GtCdcStreamInit(&state, cfg);
  state.digest_base = bytes;

  constexpr size_t kFeed = 512 * 1024;
  for (size_t off = 0; off < kFileSize; off += kFeed) {
    const size_t n = std::min(kFeed, kFileSize - off);
    const bool last = (off + n >= kFileSize);
    std::vector<ChunkDescriptor> batch;
    ASSERT_TRUE(GtCdcStreamFeed(&state, bytes + off, n, last, &batch).ok());
  }
  EXPECT_GT(state.profile.blocks_composed, 0u);
  EXPECT_GT(state.profile.vector8_groups, 0u);
}

TEST(GtCdcParityTest, StreamingMatchesFullFile) {
  constexpr size_t kFileSize = 256 * 1024 * 1024;
  const std::string data = test::MakeSyntheticData(kFileSize, 55);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  GtCdcSlice chunker;
  std::vector<size_t> full_off;
  std::vector<uint32_t> full_len;
  ASSERT_TRUE(chunker.ChunkCuts(bytes, kFileSize, &full_off, &full_len).ok());

  GtCdcConfig cfg = chunker.config();
  GtCdcStreamState state{};
  GtCdcStreamInit(&state, cfg);
  state.digest_base = bytes;

  std::vector<size_t> stream_off;
  std::vector<uint32_t> stream_len;
  constexpr size_t kFeed = 32u * 1024u * 1024u;
  for (size_t feed_off = 0; feed_off < kFileSize; feed_off += kFeed) {
    const size_t n = std::min(kFeed, kFileSize - feed_off);
    const bool last = (feed_off + n >= kFileSize);
    std::vector<ChunkDescriptor> batch;
    ASSERT_TRUE(
        GtCdcStreamFeed(&state, bytes + feed_off, n, last, &batch).ok());
    for (const auto& d : batch) {
      stream_off.push_back(d.offset);
      stream_len.push_back(d.length);
    }
  }

  ExpectCutsEqual(full_off, full_len, stream_off, stream_len);
}

TEST(GtCdcParityTest, StreamingProfileCountersNonZero) {
  SetProfileEnv();
  constexpr size_t kFileSize = 8 * 1024 * 1024;
  const std::string data = test::MakeSyntheticData(kFileSize, 55);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  GtCdcConfig cfg = GtCdcConfigForFileSize(kFileSize, ChunkProfileMode::kDefault,
                                           DigestAlgo::kLegacy);
  GtCdcStreamState state{};
  GtCdcStreamInit(&state, cfg);
  state.digest_base = bytes;

  std::vector<ChunkDescriptor> batch;
  ASSERT_TRUE(GtCdcStreamFeed(&state, bytes, kFileSize, true, &batch).ok());
  EXPECT_GT(state.profile.blocks_composed, 0u);
  EXPECT_GT(state.profile.vector8_groups, 0u);
}

TEST(GtCdcParityTest, CarryBufferNeverExceedsMaxSize) {
  const std::string data = test::MakeSyntheticData(8 * 1024 * 1024, 31);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  GtCdcConfig cfg = GtCdcConfigForFileSize(data.size(), ChunkProfileMode::kDefault,
                                           DigestAlgo::kLegacy);
  GtCdcStreamState state{};
  GtCdcStreamInit(&state, cfg);
  state.digest_base = bytes;

  constexpr size_t kFeed = 512 * 1024;
  for (size_t off = 0; off < data.size(); off += kFeed) {
    const size_t n = std::min(kFeed, data.size() - off);
    const bool last = (off + n >= data.size());
    std::vector<ChunkDescriptor> batch;
    ASSERT_TRUE(GtCdcStreamFeed(&state, bytes + off, n, last, &batch).ok());
    EXPECT_LE(state.carry.size(), cfg.max_size);
  }
}

TEST(GtCdcParityTest, DigestBaseRequired) {
  const std::string data = test::MakeSyntheticData(256 * 1024, 5);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  GtCdcStreamState state{};
  GtCdcStreamInit(&state, GtCdcConfig{});
  std::vector<ChunkDescriptor> batch;
  const Status st = GtCdcStreamFeed(&state, bytes, data.size(), true, &batch);
  EXPECT_FALSE(st.ok());
  EXPECT_NE(st.message().find("digest_base"), std::string::npos);
}

TEST(GtCdcParityTest, GearStreamingMatchesFullFile) {
  constexpr size_t kFileSize = 8 * 1024 * 1024;
  const std::string data = test::MakeSyntheticData(kFileSize, 91);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  GtCdcConfig cfg = GtCdcConfigForFileSize(
      kFileSize, ChunkProfileMode::kDefault, DigestAlgo::kLegacy,
      GtCdcKernel::kGear);
  GtCdcSlice chunker(cfg);
  std::vector<size_t> full_off;
  std::vector<uint32_t> full_len;
  ASSERT_TRUE(chunker.ChunkCuts(bytes, kFileSize, &full_off, &full_len).ok());

  GtCdcStreamState state{};
  GtCdcStreamInit(&state, cfg);
  state.digest_base = bytes;

  std::vector<size_t> stream_off;
  std::vector<uint32_t> stream_len;
  constexpr size_t kFeed = 512 * 1024;
  for (size_t feed_off = 0; feed_off < kFileSize; feed_off += kFeed) {
    const size_t n = std::min(kFeed, kFileSize - feed_off);
    const bool last = (feed_off + n >= kFileSize);
    std::vector<ChunkDescriptor> batch;
    ASSERT_TRUE(
        GtCdcStreamFeed(&state, bytes + feed_off, n, last, &batch).ok());
    for (const auto& d : batch) {
      stream_off.push_back(d.offset);
      stream_len.push_back(d.length);
    }
  }

  ExpectCutsEqual(full_off, full_len, stream_off, stream_len);
}

GtCdcConfig MakeNativeTestConfig(size_t file_size) {
  GtCdcConfig cfg = GtCdcConfigForFileSize(
      file_size, ChunkProfileMode::kDefault, DigestAlgo::kLegacy,
      GtCdcKernel::kNative);
  cfg.table_seed = 0x12345678u;
  cfg.nc_level = 2;
  gtcdc_internal::InitGearTableForConfig(&cfg);
  return cfg;
}

TEST(GtCdcParityTest, NativeStreamingMatchesFullFile) {
  constexpr size_t kFileSize = 8 * 1024 * 1024;
  const std::string data = test::MakeSyntheticData(kFileSize, 93);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  GtCdcConfig cfg = MakeNativeTestConfig(kFileSize);
  GtCdcSlice chunker(cfg);
  std::vector<size_t> full_off;
  std::vector<uint32_t> full_len;
  ASSERT_TRUE(chunker.ChunkCuts(bytes, kFileSize, &full_off, &full_len).ok());

  GtCdcStreamState state{};
  GtCdcStreamInit(&state, cfg);
  state.digest_base = bytes;

  std::vector<size_t> stream_off;
  std::vector<uint32_t> stream_len;
  constexpr size_t kFeed = 512 * 1024;
  for (size_t feed_off = 0; feed_off < kFileSize; feed_off += kFeed) {
    const size_t n = std::min(kFeed, kFileSize - feed_off);
    const bool last = (feed_off + n >= kFileSize);
    std::vector<ChunkDescriptor> batch;
    ASSERT_TRUE(
        GtCdcStreamFeed(&state, bytes + feed_off, n, last, &batch).ok());
    for (const auto& d : batch) {
      stream_off.push_back(d.offset);
      stream_len.push_back(d.length);
    }
  }

  ExpectCutsEqual(full_off, full_len, stream_off, stream_len);
}

}  // namespace
}  // namespace ebbackup
