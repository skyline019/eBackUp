#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>

#include "ebbackup/crypto/envelope.h"
#include "ebbackup/crypto/aes_gcm.h"
#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

namespace ebbackup {
namespace crypto {
namespace {

TEST(CryptoEnvelopeTest, CreateAndUnwrapPasswordAndRecovery) {
  const std::string repo = test::TempDir("envelope_repo");
  std::filesystem::create_directories(repo);

  std::string recovery_key;
  uint8_t master[32]{};
  ASSERT_TRUE(CreateEnvelope(repo, "secret-pass", &recovery_key, master).ok());
  EXPECT_EQ(recovery_key.size(), 26u);
  EXPECT_TRUE(EnvelopeExists(repo));

  uint8_t from_pw[32]{};
  ASSERT_TRUE(
      UnwrapMasterKeyWithPassword(repo, "secret-pass", from_pw, DigestAlgo::kStandard)
          .ok());
  EXPECT_EQ(std::memcmp(from_pw, master, 32), 0);

  uint8_t from_rk[32]{};
  ASSERT_TRUE(UnwrapMasterKeyWithRecoveryKey(repo, recovery_key, from_rk,
                                             DigestAlgo::kStandard)
                  .ok());
  EXPECT_EQ(std::memcmp(from_rk, master, 32), 0);
}

TEST(CryptoEnvelopeTest, UpgradeLegacyAndRotatePassword) {
  const std::string repo = test::TempDir("envelope_legacy_repo");
  std::filesystem::create_directories(repo);
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  std::string recovery_key;
  uint8_t master_key[32]{};
  ASSERT_TRUE(
      CreateEnvelope(repo, "legacy-pass", &recovery_key, master_key).ok());

  uint8_t from_pw[32]{};
  ASSERT_TRUE(
      UnwrapMasterKeyWithPassword(repo, "legacy-pass", from_pw, DigestAlgo::kStandard)
          .ok());
  EXPECT_EQ(std::memcmp(from_pw, master_key, 32), 0);

  ASSERT_TRUE(
      RotateEnvelopePassword(repo, "legacy-pass", "new-pass", DigestAlgo::kStandard)
          .ok());
  uint8_t rotated[32]{};
  ASSERT_TRUE(
      UnwrapMasterKeyWithPassword(repo, "new-pass", rotated, DigestAlgo::kStandard)
          .ok());
  EXPECT_EQ(std::memcmp(rotated, master_key, 32), 0);
}

}  // namespace
}  // namespace crypto
}  // namespace ebbackup
