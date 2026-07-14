#pragma once

#include <cstdint>

#include "ebbackup/chunk/eb_hcrbo.h"
#include "ebbackup/chunk/eb_hcrbo_gt.h"
#include "ebbackup/chunk/gt_cdc.h"
#include "ebbackup/chunk/topo_cdc.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/state/superblock.h"

namespace ebbackup {

enum class ChunkProfileMode { kAuto = 0, kSmall, kDefault, kLarge };

constexpr uint32_t kChunkProfileSmallMaxBytes = 256u * 1024u;
constexpr uint32_t kChunkProfileLargeMinBytes = 64u * 1024u * 1024u;

inline GtCdcKernel GtCdcKernelForRepo(const BackupSuperBlock& sb) {
  if (RepoUsesGtCdcTwoFGear(sb)) return GtCdcKernel::kTwoFGear;
  if (RepoUsesGtCdcAnGear(sb)) return GtCdcKernel::kAnGear;
  if (RepoUsesGtCdcNative(sb)) return GtCdcKernel::kNative;
  if (RepoUsesGtCdcGear(sb)) return GtCdcKernel::kGear;
  return GtCdcKernel::kRabin;
}

FastCdcConfig FastCdcConfigForProfile(ChunkProfileMode mode);
GtCdcConfig GtCdcConfigForProfile(ChunkProfileMode mode);
GtCdcConfig GtCdcConfigForRepo(const BackupSuperBlock& sb, ChunkProfileMode mode,
                               DigestAlgo digest_algo);
GtCdcConfig GtCdcConfigForRepo(const BackupSuperBlock& sb, size_t file_size,
                               ChunkProfileMode mode, DigestAlgo digest_algo);
EbHcrboConfig EbHcrboConfigForProfile(ChunkProfileMode mode,
                                       DigestAlgo digest_algo);
EbHcrboConfig EbHcrboConfigForFileSize(size_t file_size, ChunkProfileMode mode,
                                       DigestAlgo digest_algo);
GtCdcConfig GtCdcConfigForFileSize(size_t file_size, ChunkProfileMode mode,
                                   DigestAlgo digest_algo);
GtCdcConfig GtCdcConfigForFileSize(size_t file_size, ChunkProfileMode mode,
                                   DigestAlgo digest_algo, GtCdcKernel kernel);
EbHcrboGtConfig EbHcrboGtConfigForProfile(ChunkProfileMode mode,
                                          DigestAlgo digest_algo,
                                          GtCdcKernel kernel);
EbHcrboGtConfig EbHcrboGtConfigForFileSize(size_t file_size,
                                           ChunkProfileMode mode,
                                           DigestAlgo digest_algo,
                                           GtCdcKernel kernel);

TopoCdcConfig TopoCdcConfigForProfile(ChunkProfileMode mode);
TopoCdcConfig TopoCdcConfigForFileSize(size_t file_size, ChunkProfileMode mode,
                                       DigestAlgo digest_algo);
TopoCdcConfig TopoCdcConfigForRepo(const BackupSuperBlock& sb, size_t file_size,
                                   ChunkProfileMode mode, DigestAlgo digest_algo);

}  // namespace ebbackup
