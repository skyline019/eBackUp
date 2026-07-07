#pragma once

#include <cstdint>
#include <array>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ebbackup/codec/content_class.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/status.h"
#include "ebbackup/store/chunk_index.h"

namespace ebbackup {

class EbPackShardSet;

enum class DurabilityMode { kStrict = 0, kBalanced = 1 };

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
  CompressMode compress_mode{CompressMode::kOff};
  uint32_t cpu_budget_permille{1000};
  const char* path_hint{nullptr};
  ContentClassStats* content_stats{nullptr};
};

class ChunkStore {
 public:
  explicit ChunkStore(std::string path);
  ~ChunkStore();

  Status Open();
  Status BeginAppendSession();
  Status EndAppendSession();
  Status Flush();
  Status Put(const uint8_t* data, size_t len, uint8_t hash_out[32],
             bool* newly_written = nullptr,
             const ChunkStorePutOptions* options = nullptr);
  Status PutKnownHash(const uint8_t* data, size_t len, const uint8_t hash[32],
                      bool* newly_written = nullptr,
                      const ChunkStorePutOptions* options = nullptr);
  Status PutPrecompressed(const uint8_t hash[32], const uint8_t* payload,
                          size_t stored_len, uint32_t uncompressed_len,
                          ChunkCodec codec, bool* newly_written = nullptr,
                          bool skip_exists_check = false);
  Status Get(const uint8_t hash[32], std::vector<uint8_t>* out) const;
  bool Exists(const uint8_t hash[32]) const;
  void ExistsMany(const uint8_t* const hashes[], size_t count,
                  bool* out_exists) const;

  void SetPipelineDedupTrust(bool enabled) { pipeline_dedup_trust_ = enabled; }

  using RecordCallback = std::function<Status(const uint8_t hash[32],
                                              uint64_t offset,
                                              uint32_t stored_len)>;
  Status ForEachRecord(const RecordCallback& cb) const;

  Status TombstoneHash(const uint8_t hash[32]);
  uint64_t tombstone_count() const { return tombstones_.size(); }

  Status CorruptRecordAtOffsetForTest(uint64_t offset);
  uint64_t file_size() const { return file_size_; }
  uint64_t PhysicalBytes() const;
  uint64_t record_count() const;

  uint64_t ComputeReferencedLiveBytes(
      const std::unordered_set<std::string>& referenced) const;

  void SetDigestAlgo(DigestAlgo algo) { digest_algo_ = algo; }
  DigestAlgo digest_algo() const { return digest_algo_; }

  void SetDurabilityMode(DurabilityMode mode) { durability_mode_ = mode; }
  DurabilityMode durability_mode() const { return durability_mode_; }

  void SetUsePersistentIndex(bool enabled) { use_persistent_index_ = enabled; }
  bool use_persistent_index() const { return use_persistent_index_; }

  void SetDeferPersistentIndexSave(bool enabled) {
    defer_persistent_index_save_ = enabled;
  }
  bool defer_persistent_index_save() const {
    return defer_persistent_index_save_;
  }

  void SetUseEbPack(bool enabled) { use_ebpack_ = enabled; }
  bool use_ebpack() const { return use_ebpack_; }

  void SetTxnId(uint64_t txn_id) { append_txn_id_ = txn_id; }

  const std::string& path() const { return path_; }

  void SetContentKey(const uint8_t key[32]);
  void ClearContentKey();
  bool has_content_key() const { return has_content_key_; }

  Status SavePersistentIndex() const;

  struct ParsedHeader {
    ChunkRecordHeader header{};
    size_t header_size{kChunkHeaderV1Size};
  };

  Status ReadRecordAt(uint64_t offset, ParsedHeader* parsed,
                      std::vector<uint8_t>* payload) const;

  Status ReadRecordForHash(const uint8_t hash[32], ParsedHeader* parsed,
                           std::vector<uint8_t>* payload) const;

  static Status ReadEbPackRecordAt(const std::string& pack_path, uint64_t offset,
                                   ParsedHeader* parsed,
                                   std::vector<uint8_t>* payload);

 private:
  Status LoadIndex();
  Status LoadTombstones();
  Status AppendRecord(const uint8_t hash[32], const uint8_t* data, size_t len,
                      ChunkCodec codec, uint32_t uncompressed_len);
  Status AppendRecordInternal(const uint8_t hash[32], const uint8_t* data,
                              size_t len, ChunkCodec codec,
                              uint32_t uncompressed_len);
  Status ReadParsedHeaderAt(uint64_t offset, ParsedHeader* parsed) const;
  Status DecodePayload(const ParsedHeader& parsed,
                       std::vector<uint8_t> payload,
                       std::vector<uint8_t>* out) const;
  std::string TombstonePath() const;
  void TrackIndexEntry(const uint8_t hash[32], uint64_t offset,
                       const ChunkRecordHeader& hdr,
                       const std::string& pack_path = "");
  CompressMode ResolveCompressMode(const ChunkStorePutOptions* options) const;
  Status EnsureAppendSession();
  Status WriteBufferToSession(bool fsync_after);
  Status MaybeFlushPack();
  size_t PackFlushBytes() const;
  uint64_t PackFlushRecords() const;
  std::string PacksDir() const;
  Status AppendRecordEbPack(const uint8_t hash[32], const uint8_t* data,
                            size_t len, ChunkCodec codec,
                            uint32_t uncompressed_len);

  struct ChunkLocation {
    uint64_t offset{0};
    std::string pack_path;
    uint32_t stored_len{0};
    uint32_t uncompressed_len{0};
    uint8_t codec{0};
  };

  static constexpr size_t kIndexShardCount = 16;

  static size_t IndexShardForHash(const uint8_t hash[32]) {
    return static_cast<size_t>(hash[0] & 0x0F);
  }
  static size_t IndexShardForKey(const std::string& key) {
    return static_cast<size_t>(static_cast<uint8_t>(key[0]) & 0x0F);
  }

  std::string path_;
  uint64_t file_size_{0};
  std::array<std::unordered_map<std::string, ChunkLocation>, kIndexShardCount>
      shard_index_{};
  std::unordered_set<std::string> tombstones_;
  std::vector<ChunkIndexEntry> index_entries_;
  uint8_t content_key_[32]{};
  bool has_content_key_{false};
  DigestAlgo digest_algo_{DigestAlgo::kLegacy};
  DurabilityMode durability_mode_{DurabilityMode::kStrict};
  bool use_persistent_index_{false};
  bool defer_persistent_index_save_{false};
  bool use_ebpack_{false};
  uint64_t append_txn_id_{0};
  std::unique_ptr<EbPackShardSet> eb_pack_shards_;
  mutable std::mutex append_mu_;
  mutable std::array<std::mutex, kIndexShardCount> shard_index_mu_;
  mutable std::mutex index_entries_mu_;
  mutable std::mutex tombstones_mu_;
  bool pipeline_dedup_trust_{false};

  int append_fd_{-1};
  bool append_session_active_{false};

  std::vector<uint8_t> write_buffer_;
  uint64_t pending_records_{0};
  static constexpr size_t kStrictPackFlushBytes = 4u * 1024u * 1024u;
  static constexpr uint64_t kStrictPackFlushRecords = 32;
  static constexpr size_t kBalancedPackFlushBytes = 16u * 1024u * 1024u;
  static constexpr uint64_t kBalancedPackFlushRecords = 64;
};

}  // namespace ebbackup
