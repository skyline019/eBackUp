#pragma once

#include <cstdint>

#include "ebbackup/chunk/eb_hcrbo.h"

namespace ebbackup {

enum class ChunkProfileMode { kAuto = 0, kSmall, kDefault, kLarge };

constexpr uint32_t kChunkProfileSmallMaxBytes = 256u * 1024u;
constexpr uint32_t kChunkProfileLargeMinBytes = 64u * 1024u * 1024u;

FastCdcConfig FastCdcConfigForProfile(ChunkProfileMode mode);
EbHcrboConfig EbHcrboConfigForProfile(ChunkProfileMode mode,
                                       DigestAlgo digest_algo);
EbHcrboConfig EbHcrboConfigForFileSize(size_t file_size, ChunkProfileMode mode,
                                       DigestAlgo digest_algo);

}  // namespace ebbackup
