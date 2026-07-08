#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ebbackup/common/digest.h"
#include "ebbackup/common/status.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/store/chunk_store.h"

namespace ebbackup {
namespace audit {

Status ComputeMerkleRoot(const ManifestDocument& doc, uint8_t root_out[32],
                         DigestAlgo algo = DigestAlgo::kLegacy);

Status ComputeMerkleRootForFiles(const std::vector<ManifestFileEntry>& files,
                                 uint8_t root_out[32],
                                 DigestAlgo algo = DigestAlgo::kLegacy);

Status ComputeMerkleRootFromHashes(const std::vector<std::string>& leaf_hex,
                                   uint8_t root_out[32],
                                   DigestAlgo algo = DigestAlgo::kLegacy);

Status VerifyRestoredFileChunks(const std::string& restored_path,
                                const ManifestFileEntry& manifest,
                                ChunkStore* store);

Status ComputeMerkleRootFromRestoredFiles(
    const std::string& dest_root,
    const std::vector<ManifestFileEntry>& files, ChunkStore* store,
    uint8_t root_out[32],
    const std::unordered_map<std::string, std::string>* dest_rel_override =
        nullptr);

}  // namespace audit
}  // namespace ebbackup
