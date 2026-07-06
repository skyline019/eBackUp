#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ebbackup/common/status.h"

namespace ebbackup {

enum class ChunkCodec : uint8_t {
  kRaw = 0,
  kLz4 = 1,
  kEncrypted = 2,
  kEncryptedLz4 = 3,
};

constexpr size_t kChunkHeaderV1Size = 40;
constexpr size_t kChunkHeaderV2Size = 48;
constexpr uint8_t kChunkHeaderVersion2 = 2;

#pragma pack(push, 1)
struct ChunkRecordHeaderV1 {
  uint8_t hash[32]{};
  uint32_t raw_len{0};
  uint32_t record_crc32{0};
};

struct ChunkRecordHeader {
  uint8_t hash[32]{};
  uint32_t stored_len{0};
  uint32_t uncompressed_len{0};
  uint8_t codec{0};
  uint8_t reserved[3]{};
  uint32_t record_crc32{0};
};
#pragma pack(pop)

static_assert(sizeof(ChunkRecordHeaderV1) == kChunkHeaderV1Size);
static_assert(sizeof(ChunkRecordHeader) == kChunkHeaderV2Size);

struct ChunkStorePutOptions {
  bool use_lz4{false};
  bool use_encryption{false};
  const uint8_t* content_key{nullptr};
};

class ChunkStore {
 public:
  explicit ChunkStore(std::string path);

  Status Open();
  Status Put(const uint8_t* data, size_t len, uint8_t hash_out[32],
             bool* newly_written = nullptr,
             const ChunkStorePutOptions* options = nullptr);
  Status PutKnownHash(const uint8_t* data, size_t len, const uint8_t hash[32],
                      bool* newly_written = nullptr,
                      const ChunkStorePutOptions* options = nullptr);
  Status PutPrecompressed(const uint8_t hash[32], const uint8_t* payload,
                            size_t stored_len, uint32_t uncompressed_len,
                            ChunkCodec codec, bool* newly_written = nullptr);
  Status Get(const uint8_t hash[32], std::vector<uint8_t>* out) const;
  bool Exists(const uint8_t hash[32]) const;

  using RecordCallback = std::function<Status(const uint8_t hash[32],
                                                uint64_t offset,
                                                uint32_t stored_len)>;
  Status ForEachRecord(const RecordCallback& cb) const;

  Status TombstoneHash(const uint8_t hash[32]);
  uint64_t tombstone_count() const { return tombstones_.size(); }

  Status CorruptRecordAtOffsetForTest(uint64_t offset);
  uint64_t file_size() const { return file_size_; }
  uint64_t record_count() const { return index_.size(); }

  const std::string& path() const { return path_; }

  void SetContentKey(const uint8_t key[32]);
  void ClearContentKey();
  bool has_content_key() const { return has_content_key_; }

 private:
  struct ParsedHeader {
    ChunkRecordHeader header{};
    size_t header_size{kChunkHeaderV1Size};
  };

  Status LoadIndex();
  Status LoadTombstones();
  Status AppendRecord(const uint8_t hash[32], const uint8_t* data, size_t len,
                      ChunkCodec codec, uint32_t uncompressed_len);
  Status ReadParsedHeaderAt(uint64_t offset, ParsedHeader* parsed) const;
  Status ReadRecordAt(uint64_t offset, ParsedHeader* parsed,
                      std::vector<uint8_t>* payload) const;
  std::string TombstonePath() const;

  std::string path_;
  uint64_t file_size_{0};
  std::unordered_map<std::string, uint64_t> index_;
  std::unordered_set<std::string> tombstones_;
  uint8_t content_key_[32]{};
  bool has_content_key_{false};
};

}  // namespace ebbackup
