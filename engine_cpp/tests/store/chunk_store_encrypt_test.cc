#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>

#include "ebbackup/common/digest.h"
#include "ebbackup/store/chunk_store.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(ChunkStoreEncryptTest, PutGetRoundTrip) {
  const std::string repo = test::TempDir("encrypt_store");
  std::filesystem::create_directories(repo);
  ChunkStore store(repo + "/data/chunks");
  ASSERT_TRUE(store.Open().ok());

  uint8_t key[32]{};
  for (size_t i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(i + 1);
  store.SetContentKey(key);

  const std::string payload = test::MakeSyntheticData(4096, 3);
  uint8_t hash[32];
  Sha256(reinterpret_cast<const uint8_t*>(payload.data()), payload.size(), hash);

  ChunkStorePutOptions opts{};
  opts.use_encryption = true;
  opts.content_key = key;
  bool written = false;
  ASSERT_TRUE(store.PutKnownHash(reinterpret_cast<const uint8_t*>(payload.data()),
                                 payload.size(), hash, &written, &opts)
                    .ok());
  EXPECT_TRUE(written);

  std::vector<uint8_t> got;
  ASSERT_TRUE(store.Get(hash, &got).ok());
  EXPECT_EQ(got.size(), payload.size());
  EXPECT_EQ(std::memcmp(got.data(), payload.data(), payload.size()), 0);
}

TEST(ChunkStoreEncryptTest, WrongKeyFails) {
  const std::string repo = test::TempDir("encrypt_wrong_key");
  std::filesystem::create_directories(repo);
  ChunkStore store(repo + "/data/chunks");
  ASSERT_TRUE(store.Open().ok());

  uint8_t key[32]{};
  uint8_t wrong[32]{};
  for (size_t i = 0; i < 32; ++i) {
    key[i] = static_cast<uint8_t>(i + 2);
    wrong[i] = static_cast<uint8_t>(i + 3);
  }
  store.SetContentKey(key);

  const std::string payload = "secret-chunk";
  uint8_t hash[32];
  Sha256(reinterpret_cast<const uint8_t*>(payload.data()), payload.size(), hash);
  ChunkStorePutOptions opts{};
  opts.use_encryption = true;
  opts.content_key = key;
  ASSERT_TRUE(store.PutKnownHash(reinterpret_cast<const uint8_t*>(payload.data()),
                                 payload.size(), hash, nullptr, &opts)
                    .ok());

  store.SetContentKey(wrong);
  std::vector<uint8_t> got;
  EXPECT_FALSE(store.Get(hash, &got).ok());
}

}  // namespace
}  // namespace ebbackup
