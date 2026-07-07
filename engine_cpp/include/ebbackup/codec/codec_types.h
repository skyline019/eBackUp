#pragma once

#include <cstdint>

namespace ebbackup {

enum class ChunkCodec : uint8_t {
  kRaw = 0,
  kLz4 = 1,
  kEncrypted = 2,
  kEncryptedLz4 = 3,
  kZstd = 4,
  kEncryptedZstd = 5,
};

enum class CompressMode { kOff = 0, kLz4, kZstd, kAuto };

}  // namespace ebbackup
