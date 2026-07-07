#include "ebbackup/chunk/chunk_profile.h"

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

}  // namespace ebbackup
