#include "ebbackup/chunk/topo_ph.h"

#include <cstdlib>
#include <cstring>

#include "ebbackup/chunk/chunk_profile.h"
#include "ebbackup/chunk/fast_cdc.h"
#include "ebbackup/chunk/topo_ph_internal.h"
#include "ebbackup/engine/manifest.h"

namespace ebbackup {

namespace {

TopoPhConfig ProfileToPh(ChunkProfileMode mode) {
  const FastCdcConfig fast = FastCdcConfigForProfile(mode);
  TopoPhConfig cfg{};
  cfg.min_size = fast.min_size;
  cfg.avg_size = fast.avg_size;
  cfg.max_size = fast.max_size;
  cfg.window_w = fast.window_size;
  cfg.topo_shift = 1;
  cfg.k_points = 16;
  cfg.q_mod = 1024;
  return cfg;
}

ChunkProfileMode ResolveAutoProfile(size_t file_size) {
  if (file_size <= kChunkProfileSmallMaxBytes) return ChunkProfileMode::kSmall;
  if (file_size >= kChunkProfileLargeMinBytes) return ChunkProfileMode::kLarge;
  return ChunkProfileMode::kDefault;
}

}  // namespace

bool CdcTopoPhEnabled() {
  const char* env = std::getenv("EBBACKUP_CDC_ALGO");
  return env && std::strcmp(env, "topoph") == 0;
}

bool CdcTopoPhKernelIsPh() {
  const char* env = std::getenv("EBBACKUP_TOPOPH_KERNEL");
  return env && std::strcmp(env, "ph") == 0;
}

TopoPhKernel TopoPhKernelFromEnv() {
  return CdcTopoPhKernelIsPh() ? TopoPhKernel::kPhH0 : TopoPhKernel::kTriV2;
}

TopoPhConfig TopoPhConfigForProfile(ChunkProfileMode mode) {
  return ProfileToPh(mode);
}

TopoPhConfig TopoPhConfigForFileSize(size_t file_size, ChunkProfileMode mode,
                                     DigestAlgo digest_algo) {
  const ChunkProfileMode resolved =
      mode == ChunkProfileMode::kAuto ? ResolveAutoProfile(file_size) : mode;
  TopoPhConfig cfg = ProfileToPh(resolved);
  cfg.digest_algo = digest_algo;
  return cfg;
}

TopoPhConfig TopoPhConfigForRepo(const BackupSuperBlock& sb, size_t file_size,
                                 ChunkProfileMode mode, DigestAlgo digest_algo) {
  TopoPhConfig cfg = TopoPhConfigForFileSize(file_size, mode, digest_algo);
  cfg.table_seed = sb.ext.topo_table_seed;
  cfg.topo_calib_permille = RepoTopoPhCalibPermille(sb);
  cfg.k_points = topo_ph_internal::ClampKPoints(RepoTopoPhKPoints(sb));
  if (sb.ext.topo_variant == static_cast<uint8_t>(TopoPhKernel::kPhH0)) {
    cfg.kernel = TopoPhKernel::kPhH0;
  } else {
    cfg.kernel = TopoPhKernel::kTriV2;
  }
  return cfg;
}

TopoPhSlice::TopoPhSlice(TopoPhConfig config) : config_(config) {
  topo_ph_internal::InitGearTable(gear_, config_.table_seed);
}

Status TopoPhSlice::ChunkCuts(const uint8_t* data, size_t len,
                              std::vector<size_t>* offsets,
                              std::vector<uint32_t>* lengths) const {
  return topo_ph_internal::CollectChunkCutsTopoPh(data, len, config_, gear_,
                                                    offsets, lengths);
}

}  // namespace ebbackup
