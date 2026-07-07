#include <gtest/gtest.h>

#include <cstring>
#include <thread>
#include <vector>

#include "ebbackup/chunk/fast_cdc.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/digest_pool.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(DigestPoolTest, MatchesSerialHash) {
  const std::string data = test::MakeSyntheticData(4 * 1024 * 1024, 91);
  FastCdcConfig cfg{};
  cfg.digest_algo = DigestAlgo::kStandard;
  FastCdcSlice chunker(cfg);
  std::vector<ChunkDescriptor> chunks;
  ASSERT_TRUE(chunker.Chunk(reinterpret_cast<const uint8_t*>(data.data()),
                            data.size(), &chunks)
                  .ok());
  ASSERT_GT(chunks.size(), 1u);

  std::vector<DigestSpan> spans(chunks.size());
  std::vector<uint8_t> parallel(chunks.size() * 32);
  for (size_t i = 0; i < chunks.size(); ++i) {
    spans[i].offset = chunks[i].offset;
    spans[i].length = chunks[i].length;
  }

  DigestPool pool(4);
  pool.HashRegions(DigestAlgo::kStandard, reinterpret_cast<const uint8_t*>(data.data()),
                   spans.data(), spans.size(), parallel.data());

  for (size_t i = 0; i < chunks.size(); ++i) {
    uint8_t serial[32];
    ContentHash(DigestAlgo::kStandard,
                reinterpret_cast<const uint8_t*>(data.data()) + chunks[i].offset,
                chunks[i].length, serial);
    EXPECT_EQ(std::memcmp(serial, parallel.data() + i * 32, 32), 0)
        << "chunk index " << i;
    EXPECT_EQ(std::memcmp(chunks[i].hash, serial, 32), 0) << "fastcdc hash " << i;
  }
}

TEST(DigestPoolTest, SingleThreadParityWithFastCdc) {
  DigestPool pool(1);
  const std::string data = test::MakeSyntheticData(2 * 1024 * 1024, 13);
  FastCdcSlice chunker;
  std::vector<ChunkDescriptor> a;
  std::vector<ChunkDescriptor> b;
  ASSERT_TRUE(chunker.Chunk(reinterpret_cast<const uint8_t*>(data.data()),
                            data.size(), &a)
                  .ok());
  pool.SetThreads(1);
  ASSERT_TRUE(chunker.Chunk(reinterpret_cast<const uint8_t*>(data.data()),
                            data.size(), &b)
                  .ok());
  ASSERT_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    EXPECT_EQ(a[i], b[i]);
  }
}

TEST(DigestPoolTest, ConcurrentCallersDoNotDeadlock) {
  const std::string data = test::MakeSyntheticData(4 * 1024 * 1024, 17);
  FastCdcSlice chunker;
  std::vector<ChunkDescriptor> chunks;
  ASSERT_TRUE(chunker.Chunk(reinterpret_cast<const uint8_t*>(data.data()),
                            data.size(), &chunks)
                  .ok());
  ASSERT_GT(chunks.size(), 1u);

  std::vector<DigestSpan> spans(chunks.size());
  for (size_t i = 0; i < chunks.size(); ++i) {
    spans[i].offset = chunks[i].offset;
    spans[i].length = chunks[i].length;
  }

  DigestPool pool(4);
  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&] {
      std::vector<uint8_t> out(chunks.size() * 32);
      pool.HashRegions(DigestAlgo::kStandard,
                       reinterpret_cast<const uint8_t*>(data.data()),
                       spans.data(), spans.size(), out.data());
      for (size_t i = 0; i < chunks.size(); ++i) {
        EXPECT_EQ(std::memcmp(out.data() + i * 32, chunks[i].hash, 32), 0);
      }
    });
  }
  for (auto& th : threads) {
    th.join();
  }
}

}  // namespace
}  // namespace ebbackup
