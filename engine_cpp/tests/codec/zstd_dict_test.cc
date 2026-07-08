#include <gtest/gtest.h>

#include <filesystem>
#include <mutex>
#include <vector>

#include "ebbackup/codec/content_class.h"
#include "ebbackup/codec/zstd_codec.h"
#include "ebbackup/codec/zstd_dict.h"
#include "test_util.h"

namespace ebbackup {
namespace {

// Zstd dictionary training touches native code with global state; serialize tests
// to avoid rare SEH when the full suite runs under memory pressure.
std::mutex g_zstd_dict_test_mu;

class ZstdDictTest : public ::testing::Test {
 protected:
  void SetUp() override { g_zstd_dict_test_mu.lock(); }
  void TearDown() override { g_zstd_dict_test_mu.unlock(); }

  std::string UniquePath(const char* leaf) const {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string prefix =
        std::string("zstd_dict_") + (info ? info->name() : "case");
    return test::TempDir(prefix) + "/" + leaf;
  }
};

std::vector<uint8_t> MakeTextSample(size_t size, char seed) {
  std::vector<uint8_t> data(size);
  for (size_t i = 0; i < size; ++i) {
    data[i] = static_cast<uint8_t>(seed + static_cast<char>(i % 23));
  }
  return data;
}

TEST_F(ZstdDictTest, TrainSaveLoadRoundTrip) {
  std::vector<std::vector<uint8_t>> samples;
  for (int i = 0; i < 10; ++i) {
    samples.push_back(MakeTextSample(2048, static_cast<char>('a' + i)));
  }

  ZstdDictionary trained;
  ASSERT_TRUE(trained.TrainFromSamples(samples).ok());
  ASSERT_FALSE(trained.empty());

  const std::string path = UniquePath("dict.bin");
  ASSERT_TRUE(trained.SaveToFile(path).ok());

  ZstdDictionary loaded;
  ASSERT_TRUE(loaded.LoadFromFile(path).ok());
  EXPECT_EQ(loaded.size(), trained.size());
}

TEST_F(ZstdDictTest, DictionaryRoundTripWithCDict) {
  std::vector<std::vector<uint8_t>> samples;
  for (int i = 0; i < 10; ++i) {
    samples.push_back(MakeTextSample(4096, static_cast<char>('x' + (i % 5))));
  }

  ZstdDictionary dict;
  ASSERT_TRUE(dict.TrainFromSamples(samples).ok());
  const auto payload = MakeTextSample(4096, 'x');

  ZstdCompressOptions opts{};
  opts.level = 3;
  opts.cdict = dict.AcquireCDict(3);
  ASSERT_TRUE(opts.cdict);
  ZstdEncodeResult encoded{};
  ASSERT_TRUE(ZstdCompressEx(payload.data(), payload.size(), opts, &encoded).ok());
  ASSERT_TRUE(encoded.compressed);

  std::vector<uint8_t> decoded;
  ZstdDecompressOptions dopts{};
  dopts.ddict = dict.AcquireDDict();
  ASSERT_TRUE(dopts.ddict);
  ASSERT_TRUE(
      ZstdDecompressEx(encoded.payload.data(), encoded.payload.size(),
                       encoded.uncompressed_size, dopts, &decoded)
          .ok());
  EXPECT_EQ(decoded, payload);
}

TEST_F(ZstdDictTest, TrainerCollectsAndFinalizes) {
  ZstdDictTrainer trainer;
  const auto sample = MakeTextSample(2048, 't');
  for (int i = 0; i < 10; ++i) {
    trainer.MaybeAddSample(sample.data(), sample.size(),
                           ContentDataClass::kFastCompressible);
  }
  EXPECT_EQ(trainer.sample_count(), 10u);

  const std::string repo = test::TempDir("dict_repo_finalize");
  std::filesystem::create_directories(repo + "/meta");

  ZstdDictionary out;
  ASSERT_TRUE(trainer.FinalizeAndSave(repo, &out).ok());
  EXPECT_FALSE(out.empty());
  EXPECT_TRUE(std::filesystem::exists(ZstdDictionaryPath(repo)));
}

}  // namespace
}  // namespace ebbackup
