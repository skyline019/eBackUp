#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/digest.h"
#include "ebbackup/common/status.h"

namespace ebbackup {
namespace audit {

struct MerkleProofStep {
  std::string sibling_hex;
  int is_right{0};  /* 1 if sibling is combined on the right */
};

Status GenerateMerkleProof(const std::vector<std::string>& leaf_hex,
                           size_t leaf_index, DigestAlgo algo,
                           std::vector<MerkleProofStep>* proof_out);

Status VerifyMerkleProof(const std::string& leaf_hex,
                         const std::string& root_hex,
                         const std::vector<MerkleProofStep>& proof,
                         DigestAlgo algo);

std::string MerkleProofToJson(const std::vector<MerkleProofStep>& proof);

}  // namespace audit
}  // namespace ebbackup
