#include "ebbackup/chunk/chunk_profile.h"
#include "ebbackup/chunk/topo_cdc.h"
#include "ebbackup/engine/manifest.h"

namespace ebbackup {

namespace {

TopoCdcConfig ProfileToTopo(ChunkProfileMode mode) {
  const FastCdcConfig fast = FastCdcConfigForProfile(mode);
  TopoCdcConfig cfg{};
  cfg.min_size = fast.min_size;
  cfg.avg_size = fast.avg_size;
  cfg.max_size = fast.max_size;
  cfg.window_w = fast.window_size;
  cfg.topo_shift = 1;
  return cfg;
}

ChunkProfileMode ResolveAutoProfile(size_t file_size) {
  if (file_size <= kChunkProfileSmallMaxBytes) return ChunkProfileMode::kSmall;
  if (file_size >= kChunkProfileLargeMinBytes) return ChunkProfileMode::kLarge;
  return ChunkProfileMode::kDefault;
}

}  // namespace

TopoCdcConfig TopoCdcConfigForProfile(ChunkProfileMode mode) {
  return ProfileToTopo(mode);
}

TopoCdcConfig TopoCdcConfigForFileSize(size_t file_size, ChunkProfileMode mode,
                                       DigestAlgo digest_algo) {
  const ChunkProfileMode resolved =
      mode == ChunkProfileMode::kAuto ? ResolveAutoProfile(file_size) : mode;
  TopoCdcConfig cfg = ProfileToTopo(resolved);
  cfg.digest_algo = digest_algo;
  return cfg;
}

TopoCdcConfig TopoCdcConfigForRepo(const BackupSuperBlock& sb, size_t file_size,
                                   ChunkProfileMode mode, DigestAlgo digest_algo) {
  TopoCdcConfig cfg = TopoCdcConfigForFileSize(file_size, mode, digest_algo);
  if (RepoUsesTopoChain(sb)) {
    cfg.variant = TopoCdcVariant::kChain;
    cfg.chain_lfsr_seed = sb.ext.topo_table_seed;
    cfg.chain_stride_log =
        static_cast<uint8_t>(RepoTopoChainStrideLog(sb) & 0xFFu);
    if (cfg.chain_stride_log == 0) cfg.chain_stride_log = 12;
    cfg.chain_quant_q = RepoTopoChainQuantQ(sb);
    cfg.chain_enable_beta1 = RepoTopoChainBeta1(sb);
    return cfg;
  }
  cfg.table_seed = sb.ext.topo_table_seed;
  cfg.topo_calib_permille = RepoTopoCalibPermille(sb);
  if (sb.ext.topo_variant == 1) {
    cfg.variant = TopoCdcVariant::kTri;
  }
  return cfg;
}

}  // namespace ebbackup
