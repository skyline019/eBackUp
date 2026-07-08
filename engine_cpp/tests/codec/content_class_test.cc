#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ebbackup/codec/content_class.h"

namespace ebbackup {
namespace {

TEST(ContentClassTest, BalancedTierBeatsFastOnRepeatedText) {
  std::string text(65536, 'a');
  for (size_t i = 0; i < text.size(); i += 64) {
    text.replace(i, 8, "function");
  }

  ContentEncodeRequest fast_req{};
  fast_req.data = reinterpret_cast<const uint8_t*>(text.data());
  fast_req.len = text.size();
  fast_req.mode = CompressMode::kAuto;
  fast_req.tier = CompressTier::kFast;
  fast_req.cpu_budget_permille = 1000;

  ContentEncodeRequest balanced_req = fast_req;
  balanced_req.tier = CompressTier::kBalanced;

  ContentEncodeResult fast_out{};
  ContentEncodeResult balanced_out{};
  ASSERT_TRUE(ContentClassEncode(fast_req, &fast_out, nullptr).ok());
  ASSERT_TRUE(ContentClassEncode(balanced_req, &balanced_out, nullptr).ok());
  EXPECT_NE(fast_out.codec, ChunkCodec::kRaw);
  EXPECT_NE(balanced_out.codec, ChunkCodec::kRaw);
  EXPECT_LE(balanced_out.payload.size(), fast_out.payload.size());
}

TEST(ContentClassTest, IncompressibleSkipsCompression) {
  std::vector<uint8_t> data(4096);
  for (size_t i = 0; i < data.size(); ++i) {
    data[i] = static_cast<uint8_t>(i);
  }
  ContentEncodeRequest req{};
  req.data = data.data();
  req.len = data.size();
  req.mode = CompressMode::kAuto;
  req.path_hint = "payload.bin.jpg";
  req.cpu_budget_permille = 600;
  ContentEncodeResult out{};
  ContentClassStats stats{};
  ASSERT_TRUE(ContentClassEncode(req, &out, &stats).ok());
  EXPECT_EQ(out.codec, ChunkCodec::kRaw);
  EXPECT_GE(stats.incompressible_skips, 1u);
}

TEST(ContentClassTest, TextUsesLz4OrZstd) {
  std::string text(8192, 'a');
  ContentEncodeRequest req{};
  req.data = reinterpret_cast<const uint8_t*>(text.data());
  req.len = text.size();
  req.mode = CompressMode::kAuto;
  req.cpu_budget_permille = 1000;
  ContentEncodeResult out{};
  ASSERT_TRUE(ContentClassEncode(req, &out, nullptr).ok());
  EXPECT_NE(out.codec, ChunkCodec::kRaw);
  EXPECT_LT(out.payload.size(), text.size());
}

TEST(ContentClassTest, ZipExtensionSkipsCompression) {
  std::vector<uint8_t> data(4096, 0xAB);
  ContentEncodeRequest req{};
  req.data = data.data();
  req.len = data.size();
  req.mode = CompressMode::kAuto;
  req.path_hint = "archives/sample.zip";
  req.cpu_budget_permille = 600;
  ContentEncodeResult out{};
  ContentClassStats stats{};
  ASSERT_TRUE(ContentClassEncode(req, &out, &stats).ok());
  EXPECT_EQ(out.codec, ChunkCodec::kRaw);
  EXPECT_GE(stats.incompressible_skips, 1u);
}

TEST(ContentClassTest, ExeExtensionSkipsCompression) {
  std::vector<uint8_t> data(4096, 0x90);
  ContentEncodeRequest req{};
  req.data = data.data();
  req.len = data.size();
  req.mode = CompressMode::kAuto;
  req.path_hint = "binaries/sample.exe";
  req.cpu_budget_permille = 600;
  ContentEncodeResult out{};
  ContentClassStats stats{};
  ASSERT_TRUE(ContentClassEncode(req, &out, &stats).ok());
  EXPECT_EQ(out.codec, ChunkCodec::kRaw);
  EXPECT_GE(stats.incompressible_skips, 1u);
}

}  // namespace
}  // namespace ebbackup
