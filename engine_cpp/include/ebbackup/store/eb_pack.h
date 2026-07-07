#pragma once

#include <array>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "ebbackup/common/status.h"
#include "ebbackup/codec/content_class.h"

namespace ebbackup {

constexpr char kEbPackMagic[8] = {'E', 'B', 'P', 'A', 'C', 'K', '1', '\0'};
constexpr size_t kEbPackHeaderSize = 4096;
constexpr size_t kEbPackTargetBytes = 8u * 1024u * 1024u;

#pragma pack(push, 1)
struct EbPackHeader {
  char magic[8]{};
  uint64_t txn_id{0};
  uint32_t seq{0};
  uint32_t record_count{0};
  uint32_t payload_crc32{0};
  uint32_t reserved{0};
  uint64_t pack_size{0};
  uint8_t padding[kEbPackHeaderSize - 8 - 8 - 4 - 4 - 4 - 4 - 8]{};
};
#pragma pack(pop)

static_assert(sizeof(EbPackHeader) == kEbPackHeaderSize);

struct EbPackRecordRef {
  std::string pack_path;
  uint64_t offset{0};
  uint32_t stored_len{0};
  uint32_t uncompressed_len{0};
  uint8_t codec{0};
};

class EbPackWriter {
 public:
  EbPackWriter(std::string packs_dir, uint64_t txn_id,
               uint32_t shard_id = UINT32_MAX);

  void SetSpillThresholds(size_t max_buffer_bytes, uint64_t max_records);

  Status AppendRecord(const uint8_t hash[32], const uint8_t* payload,
                      size_t stored_len, uint32_t uncompressed_len,
                      ChunkCodec codec, EbPackRecordRef* out_ref);

  Status FlushOpenPack(bool fsync_after);
  Status FsyncAll();

  uint32_t pack_count() const { return seq_; }
  uint32_t shard_id() const { return shard_id_; }

 private:
  Status EnsureOpenPack();
  Status FinalizeOpenPack(bool fsync_after);
  Status MaybeSpillBeforeAppend(size_t record_size);
  uint32_t ComputeOpenPayloadCrc32();

  std::string packs_dir_;
  uint64_t txn_id_{0};
  uint32_t shard_id_{UINT32_MAX};
  uint32_t seq_{0};
  std::fstream append_fd_;
  mutable std::mutex mu_;
  bool pack_open_{false};
  uint64_t open_payload_size_{0};
  uint32_t open_records_{0};
  std::string open_path_;
  std::vector<std::string> written_paths_;
  std::unordered_set<std::string> fsynced_paths_;
  size_t spill_bytes_{kEbPackTargetBytes};
  uint64_t spill_records_{UINT64_MAX};
};

class EbPackShardSet {
 public:
  static constexpr size_t kShardCount = 16;

  EbPackShardSet(std::string packs_dir, uint64_t txn_id, size_t spill_bytes,
                 uint64_t spill_records);

  static size_t ShardForHash(const uint8_t hash[32]) {
    return static_cast<size_t>(hash[0] & 0x0F);
  }

  Status AppendRecord(const uint8_t hash[32], const uint8_t* payload,
                      size_t stored_len, uint32_t uncompressed_len,
                      ChunkCodec codec, EbPackRecordRef* out_ref);

  Status FlushAllOpenPacks(bool fsync_after);
  Status FsyncAll();

 private:
  std::array<EbPackWriter, kShardCount> shards_;
};

}  // namespace ebbackup
