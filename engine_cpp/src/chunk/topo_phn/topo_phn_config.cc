#include "ebbackup/chunk/topo_phn.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ebbackup/chunk/chunk_profile.h"
#include "ebbackup/chunk/fast_cdc.h"
#include "ebbackup/chunk/topo_phn_internal.h"
#include "ebbackup/engine/manifest.h"

namespace ebbackup {

namespace {

TopoPhnConfig ProfileToPhn(ChunkProfileMode mode) {
  const FastCdcConfig fast = FastCdcConfigForProfile(mode);
  TopoPhnConfig cfg{};
  cfg.min_size = fast.min_size;
  cfg.avg_size = fast.avg_size;
  cfg.max_size = fast.max_size;
  cfg.k_points = 8;
  cfg.q_mod = 1024;
  cfg.event_stride = 64;
  cfg.flip_tau = 0;
  cfg.persist_delta = 1;
  cfg.enable_persist_delta = false;
  return cfg;
}

ChunkProfileMode ResolveAutoProfile(size_t file_size) {
  if (file_size <= kChunkProfileSmallMaxBytes) return ChunkProfileMode::kSmall;
  if (file_size >= kChunkProfileLargeMinBytes) return ChunkProfileMode::kLarge;
  return ChunkProfileMode::kDefault;
}

}  // namespace

bool CdcTopoPhnEnabled() {
  const char* env = std::getenv("EBBACKUP_CDC_ALGO");
  return env && std::strcmp(env, "topophn") == 0;
}

bool CdcTopoPhnKernelIsPh() {
  const char* env = std::getenv("EBBACKUP_TOPOPHN_KERNEL");
  return env && std::strcmp(env, "ph") == 0;
}

TopoPhnKernel TopoPhnKernelFromEnv() {
  return CdcTopoPhnKernelIsPh() ? TopoPhnKernel::kPhH0Native
                                : TopoPhnKernel::kTriNative;
}

TopoPhnConfig TopoPhnConfigForProfile(ChunkProfileMode mode) {
  return ProfileToPhn(mode);
}

TopoPhnConfig TopoPhnConfigForFileSize(size_t file_size, ChunkProfileMode mode,
                                       DigestAlgo digest_algo) {
  const ChunkProfileMode resolved =
      mode == ChunkProfileMode::kAuto ? ResolveAutoProfile(file_size) : mode;
  TopoPhnConfig cfg = ProfileToPhn(resolved);
  cfg.digest_algo = digest_algo;
  return cfg;
}

TopoPhnConfig TopoPhnConfigForRepo(const BackupSuperBlock& sb, size_t file_size,
                                   ChunkProfileMode mode,
                                   DigestAlgo digest_algo) {
  TopoPhnConfig cfg = TopoPhnConfigForFileSize(file_size, mode, digest_algo);
  cfg.table_seed = sb.ext.topo_table_seed;
  const uint32_t sb_stride =
      topo_phn_internal::ClampEventStride(RepoTopoPhnEventStride(sb));
  cfg.event_stride =
      topo_phn_internal::ScaleEventStrideForAvg(sb_stride, cfg.avg_size);
  cfg.k_points = topo_phn_internal::ClampKPoints(RepoTopoPhnKPoints(sb));
  cfg.enable_persist_delta = RepoTopoPhnPersistDelta(sb);
  if (sb.ext.topo_variant == static_cast<uint8_t>(TopoPhnKernel::kPhH0Native)) {
    cfg.kernel = TopoPhnKernel::kPhH0Native;
  } else {
    cfg.kernel = TopoPhnKernel::kTriNative;
  }
  // Large/Small: ≥8×avg samples so mean banding has enough chunks.
  const size_t runtime_n = std::max(
      topo_phn_internal::kPhnCalibSampleBytes,
      static_cast<size_t>(cfg.avg_size) * 8u);
  std::vector<uint8_t> sample(runtime_n);
  topo_phn_internal::FillPhnCalibSample(sample.data(), sample.size(),
                                        cfg.table_seed);
  topo_phn_internal::CalibratePhnRuntimeParams(sample.data(), sample.size(),
                                               &cfg);
  return cfg;
}

TopoPhnSlice::TopoPhnSlice(TopoPhnConfig config) : config_(config) {}

Status TopoPhnSlice::ChunkCuts(const uint8_t* data, size_t len,
                               std::vector<size_t>* offsets,
                               std::vector<uint32_t>* lengths) const {
  return topo_phn_internal::CollectChunkCutsTopoPhn(data, len, config_, offsets,
                                                    lengths);
}

}  // namespace ebbackup
