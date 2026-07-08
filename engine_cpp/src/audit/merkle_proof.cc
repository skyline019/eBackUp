#include "ebbackup/audit/merkle_proof.h"

#include <cstring>

#include "ebbackup/audit/merkle.h"

namespace ebbackup {
namespace audit {

namespace {

void HashPairLocal(DigestAlgo algo, const uint8_t left[32], const uint8_t right[32],
                   uint8_t out[32]) {
  uint8_t buf[64];
  std::memcpy(buf, left, 32);
  std::memcpy(buf + 32, right, 32);
  ContentHash(algo, buf, 64, out);
}

Status HexLeavesToBytes(const std::vector<std::string>& leaf_hex,
                        std::vector<std::vector<uint8_t>>* leaves) {
  if (!leaves) return Status::InvalidArgument("leaves is null");
  leaves->clear();
  leaves->reserve(leaf_hex.size());
  for (const auto& hex : leaf_hex) {
    if (hex.size() != 64) return Status::Corrupt("invalid leaf hash length");
    std::vector<uint8_t> hash(32);
    if (!HexToBytes(hex, hash.data(), 32)) {
      return Status::Corrupt("invalid leaf hash hex");
    }
    leaves->push_back(std::move(hash));
  }
  return Status::Ok();
}

uint8_t* NodeAt(std::vector<std::vector<uint8_t>>* level, size_t index) {
  return level->at(index).data();
}

}  // namespace

Status GenerateMerkleProof(const std::vector<std::string>& leaf_hex,
                           size_t leaf_index, DigestAlgo algo,
                           std::vector<MerkleProofStep>* proof_out) {
  if (!proof_out) return Status::InvalidArgument("proof_out is null");
  proof_out->clear();
  if (leaf_hex.empty()) return Status::InvalidArgument("empty leaves");
  if (leaf_index >= leaf_hex.size()) {
    return Status::InvalidArgument("leaf_index out of range");
  }

  std::vector<std::vector<uint8_t>> level;
  const Status conv = HexLeavesToBytes(leaf_hex, &level);
  if (!conv.ok()) return conv;

  size_t index = leaf_index;
  while (level.size() > 1) {
    MerkleProofStep step;
    const size_t sibling_index =
        (index % 2 == 0) ? index + 1 : index - 1;
    uint8_t* sibling = nullptr;
    int is_right = 0;
    if (sibling_index < level.size()) {
      sibling = NodeAt(&level, sibling_index);
      is_right = (index % 2 == 0) ? 1 : 0;
    } else {
      sibling = NodeAt(&level, index);
      is_right = 1;
    }
    step.sibling_hex = BytesToHex(sibling, 32);
    step.is_right = is_right;
    proof_out->push_back(std::move(step));

    std::vector<std::vector<uint8_t>> next;
    next.reserve((level.size() + 1) / 2);
    for (size_t i = 0; i < level.size(); i += 2) {
      const uint8_t* left = level[i].data();
      const uint8_t* right = left;
      if (i + 1 < level.size()) right = level[i + 1].data();
      std::vector<uint8_t> combined(32);
      HashPairLocal(algo, left, right, combined.data());
      next.push_back(std::move(combined));
    }
    index /= 2;
    level = std::move(next);
  }
  return Status::Ok();
}

Status VerifyMerkleProof(const std::string& leaf_hex,
                         const std::string& root_hex,
                         const std::vector<MerkleProofStep>& proof,
                         DigestAlgo algo) {
  if (leaf_hex.size() != 64 || root_hex.size() != 64) {
    return Status::InvalidArgument("invalid hash hex length");
  }
  std::vector<uint8_t> current(32);
  if (!HexToBytes(leaf_hex, current.data(), 32)) {
    return Status::Corrupt("invalid leaf hex");
  }
  for (const auto& step : proof) {
    if (step.sibling_hex.size() != 64) {
      return Status::Corrupt("invalid proof sibling");
    }
    uint8_t sibling[32];
    if (!HexToBytes(step.sibling_hex, sibling, 32)) {
      return Status::Corrupt("invalid proof sibling hex");
    }
    uint8_t combined[32];
    if (step.is_right) {
      HashPairLocal(algo, current.data(), sibling, combined);
    } else {
      HashPairLocal(algo, sibling, current.data(), combined);
    }
    std::memcpy(current.data(), combined, 32);
  }
  uint8_t expected[32];
  if (!HexToBytes(root_hex, expected, 32)) {
    return Status::Corrupt("invalid root hex");
  }
  if (std::memcmp(current.data(), expected, 32) != 0) {
    return Status::Corrupt("merkle proof verification failed");
  }
  return Status::Ok();
}

std::string MerkleProofToJson(const std::vector<MerkleProofStep>& proof) {
  std::string j = "[";
  for (size_t i = 0; i < proof.size(); ++i) {
    if (i) j += ',';
    j += "{\"sibling\":\"";
    j += proof[i].sibling_hex;
    j += "\",\"is_right\":";
    j += std::to_string(proof[i].is_right);
    j += '}';
  }
  j += ']';
  return j;
}

}  // namespace audit
}  // namespace ebbackup
