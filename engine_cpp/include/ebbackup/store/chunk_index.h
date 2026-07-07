#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"

namespace ebbackup {

constexpr uint32_t kChunkIndexMagic = 0x44495848u;  // HXID
constexpr uint32_t kChunkIndexVersion = 1;
constexpr uint32_t kChunkIndexVersionEbPack = 2;

constexpr uint8_t kChunkStorageLegacy = 0;
constexpr uint8_t kChunkStorageEbPack = 1;

#pragma pack(push, 1)
struct ChunkIndexEntryV1 {
  uint8_t hash[32]{};
  uint64_t offset{0};
  uint32_t stored_len{0};
  uint32_t uncompressed_len{0};
  uint8_t codec{0};
  uint8_t reserved[3]{};
};

struct ChunkIndexHeader {
  uint32_t magic{kChunkIndexMagic};
  uint32_t version{kChunkIndexVersion};
  uint64_t entry_count{0};
  uint64_t chunks_file_size{0};
  uint32_t header_crc32{0};
};

struct ChunkIndexEntry {
  uint8_t hash[32]{};
  uint64_t offset{0};
  uint32_t stored_len{0};
  uint32_t uncompressed_len{0};
  uint8_t codec{0};
  uint8_t storage_flags{kChunkStorageLegacy};
  char pack_name[47]{};
};
#pragma pack(pop)

static_assert(sizeof(ChunkIndexEntryV1) == 52);
static_assert(sizeof(ChunkIndexEntry) == 97);

class ChunkIndexFile {
 public:
  static std::string PathForStore(const std::string& chunks_path);

  Status Load(const std::string& path, uint64_t expected_chunks_size,
              std::vector<ChunkIndexEntry>* out);
  Status Save(const std::string& path, uint64_t chunks_file_size,
              const std::vector<ChunkIndexEntry>& entries) const;
  static Status RebuildFromScan(
      const std::string& chunks_path,
      std::vector<ChunkIndexEntry>* out,
      uint64_t* chunks_file_size_out);
};

}  // namespace ebbackup
