#include "ebbackup/chunk/chunk_profile.h"
#include "ebbackup/chunk/gt_cdc_internal.h"

namespace ebbackup {

namespace {

FastCdcConfig SmallProfile() {
  FastCdcConfig cfg{};
  cfg.min_size = 4u * 1024u;
  cfg.avg_size = 16u * 1024u;
  cfg.max_size = 64u * 1024u;
  return cfg;
}

FastCdcConfig DefaultProfile() {
  FastCdcConfig cfg{};
  cfg.min_size = 64u * 1024u;
  cfg.avg_size = 256u * 1024u;
  cfg.max_size = 1024u * 1024u;
  return cfg;
}

FastCdcConfig LargeProfile() {
  FastCdcConfig cfg{};
  cfg.min_size = 256u * 1024u;
  cfg.avg_size = 1024u * 1024u;
  cfg.max_size = 4u * 1024u * 1024u;
  return cfg;
}

ChunkProfileMode ResolveAutoProfile(size_t file_size) {
  if (file_size <= kChunkProfileSmallMaxBytes) return ChunkProfileMode::kSmall;
  if (file_size >= kChunkProfileLargeMinBytes) return ChunkProfileMode::kLarge;
  return ChunkProfileMode::kDefault;
}

}  // namespace

FastCdcConfig FastCdcConfigForProfile(ChunkProfileMode mode) {
  switch (mode) {
    case ChunkProfileMode::kSmall:
      return SmallProfile();
    case ChunkProfileMode::kLarge:
      return LargeProfile();
    case ChunkProfileMode::kDefault:
    case ChunkProfileMode::kAuto:
    default:
      return DefaultProfile();
  }
}

GtCdcConfig GtCdcConfigForProfile(ChunkProfileMode mode) {
  GtCdcConfig cfg{};
  const FastCdcConfig fast = FastCdcConfigForProfile(mode);
  cfg.min_size = fast.min_size;
  cfg.avg_size = fast.avg_size;
  cfg.max_size = fast.max_size;
  cfg.window_w = fast.window_size;
  cfg.block_B = 64;
  cfg.alpha = 0x00010001u;
  gtcdc_internal::InitGearTable(cfg.beta_table);
  return cfg;
}

GtCdcConfig GtCdcConfigForRepo(const BackupSuperBlock& sb, ChunkProfileMode mode,
                               DigestAlgo digest_algo) {
  return GtCdcConfigForRepo(sb, 0, mode, digest_algo);
}

GtCdcConfig GtCdcConfigForRepo(const BackupSuperBlock& sb, size_t file_size,
                               ChunkProfileMode mode, DigestAlgo digest_algo) {
  const GtCdcKernel kernel = GtCdcKernelForRepo(sb);
  GtCdcConfig cfg = GtCdcConfigForFileSize(file_size, mode, digest_algo, kernel);
  if (kernel == GtCdcKernel::kNative || kernel == GtCdcKernel::kAnGear ||
      kernel == GtCdcKernel::kTwoFGear) {
    cfg.table_seed = sb.ext.gtcdc_table_seed;
    cfg.nc_level = sb.ext.gtcdc_nc_level;
    gtcdc_internal::InitGearTableForConfig(&cfg);
  }
  return cfg;
}

EbHcrboConfig EbHcrboConfigForProfile(ChunkProfileMode mode,
                                      DigestAlgo digest_algo) {
  EbHcrboConfig cfg{};
  cfg.fast = FastCdcConfigForProfile(mode);
  cfg.fast.digest_algo = digest_algo;
  cfg.digest_algo = digest_algo;
  return cfg;
}

EbHcrboConfig EbHcrboConfigForFileSize(size_t file_size, ChunkProfileMode mode,
                                       DigestAlgo digest_algo) {
  const ChunkProfileMode resolved =
      mode == ChunkProfileMode::kAuto ? ResolveAutoProfile(file_size) : mode;
  return EbHcrboConfigForProfile(resolved, digest_algo);
}

GtCdcConfig GtCdcConfigForFileSize(size_t file_size, ChunkProfileMode mode,
                                   DigestAlgo digest_algo) {
  return GtCdcConfigForFileSize(file_size, mode, digest_algo,
                                  GtCdcKernel::kRabin);
}

GtCdcConfig GtCdcConfigForFileSize(size_t file_size, ChunkProfileMode mode,
                                   DigestAlgo digest_algo, GtCdcKernel kernel) {
  const ChunkProfileMode resolved =
      mode == ChunkProfileMode::kAuto ? ResolveAutoProfile(file_size) : mode;
  GtCdcConfig cfg = GtCdcConfigForProfile(resolved);
  cfg.digest_algo = digest_algo;
  cfg.kernel = kernel;
  if ((kernel == GtCdcKernel::kNative || kernel == GtCdcKernel::kAnGear) &&
      cfg.nc_level == 0) {
    cfg.nc_level = 2;
  }
  gtcdc_internal::InitGearTableForConfig(&cfg);
  return cfg;
}

EbHcrboGtConfig EbHcrboGtConfigForProfile(ChunkProfileMode mode,
                                          DigestAlgo digest_algo,
                                          GtCdcKernel kernel) {
  EbHcrboGtConfig cfg{};
  cfg.gt = GtCdcConfigForProfile(mode);
  cfg.gt.digest_algo = digest_algo;
  cfg.gt.kernel = kernel;
  cfg.digest_algo = digest_algo;
  return cfg;
}

EbHcrboGtConfig EbHcrboGtConfigForFileSize(size_t file_size,
                                           ChunkProfileMode mode,
                                           DigestAlgo digest_algo,
                                           GtCdcKernel kernel) {
  const ChunkProfileMode resolved =
      mode == ChunkProfileMode::kAuto ? ResolveAutoProfile(file_size) : mode;
  EbHcrboGtConfig cfg = EbHcrboGtConfigForProfile(resolved, digest_algo, kernel);
  cfg.gt = GtCdcConfigForFileSize(file_size, mode, digest_algo, kernel);
  return cfg;
}

}  // namespace ebbackup
