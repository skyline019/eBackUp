#include <gtest/gtest.h>

#include <cstring>

#include "ebbackup/common/digest.h"
#include "ebbackup/audit/rar_chain.h"
#include "test_util.h"

namespace ebbackup {
namespace audit {
namespace {

TEST(RarChainTest, AppendVerifyChain) {
  const std::string path = test::TempDir("rar_chain") + "/rar.chain";
  uint8_t merkle[32]{};
  std::memset(merkle, 0xAB, 32);

  RarChainEntry e1{};
  e1.sequence = 1;
  e1.txn_id = 1;
  e1.manifest_crc32 = "deadbeef";
  e1.merkle_root = BytesToHex(merkle, 32);
  e1.body_json = BuildRarBodyJson(1, 0xDEADBEEFu, merkle);
  e1.rar_sha256 = ComputeRarSha256(e1.body_json, DigestAlgo::kLegacy);
  e1.generated_at_unix = 1000;
  ASSERT_TRUE(AppendRarChainEntry(path, e1).ok());

  RarChainEntry e2{};
  e2.sequence = 2;
  e2.txn_id = 2;
  e2.prev_rar_sha256 = e1.rar_sha256;
  e2.manifest_crc32 = "cafebabe";
  e2.merkle_root = BytesToHex(merkle, 32);
  e2.body_json = BuildRarBodyJson(2, 0xCAFEBABEu, merkle);
  e2.rar_sha256 = ComputeRarSha256(e2.body_json, DigestAlgo::kLegacy);
  e2.generated_at_unix = 2000;
  ASSERT_TRUE(AppendRarChainEntry(path, e2).ok());

  RarChainVerifyReport report{};
  ASSERT_TRUE(VerifyRarChain(path, &report).ok());
  EXPECT_TRUE(report.consistent);
  EXPECT_EQ(report.entry_count, 2u);
  EXPECT_EQ(RarChainNextSequence(path), 3u);
}

}  // namespace
}  // namespace audit
}  // namespace ebbackup
