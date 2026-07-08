#include <gtest/gtest.h>

#include "ebbackup/audit/merkle.h"
#include "ebbackup/audit/merkle_proof.h"

namespace ebbackup {
namespace audit {
namespace {

TEST(MerkleProofTest, GenerateAndVerify) {
  std::vector<std::string> leaves;
  for (int i = 0; i < 4; ++i) {
    uint8_t buf[32]{};
    buf[0] = static_cast<uint8_t>(i);
    leaves.push_back(BytesToHex(buf, 32));
  }
  uint8_t root[32]{};
  ASSERT_TRUE(ComputeMerkleRootFromHashes(leaves, root, DigestAlgo::kStandard).ok());
  const std::string root_hex = BytesToHex(root, 32);

  for (size_t idx = 0; idx < leaves.size(); ++idx) {
    std::vector<MerkleProofStep> proof;
    ASSERT_TRUE(
        GenerateMerkleProof(leaves, idx, DigestAlgo::kStandard, &proof).ok());
    EXPECT_TRUE(VerifyMerkleProof(leaves[idx], root_hex, proof,
                                  DigestAlgo::kStandard)
                    .ok());
  }
}

TEST(MerkleProofTest, TamperedLeafFails) {
  std::vector<std::string> leaves(2, std::string(64, 'a'));
  uint8_t root[32]{};
  ASSERT_TRUE(ComputeMerkleRootFromHashes(leaves, root, DigestAlgo::kStandard).ok());
  std::vector<MerkleProofStep> proof;
  ASSERT_TRUE(GenerateMerkleProof(leaves, 0, DigestAlgo::kStandard, &proof).ok());
  EXPECT_FALSE(VerifyMerkleProof(std::string(64, 'b'), BytesToHex(root, 32),
                                 proof, DigestAlgo::kStandard)
                   .ok());
}

}  // namespace
}  // namespace audit
}  // namespace ebbackup
