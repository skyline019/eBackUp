#include "ebbackup/store/chunk_store.h"
#include "ebbackup/store/eb_pack.h"

#include <array>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "ebbackup/codec/content_class.h"
#include "ebbackup/codec/lz4_codec.h"
#include "ebbackup/codec/zstd_codec.h"
#include "ebbackup/common/crc32.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/path_encoding.h"
#include "ebbackup/common/fsync.h"
#include "ebbackup/crypto/aes_gcm.h"

namespace ebbackup {

namespace {

std::string HashKey(const uint8_t hash[32]) {
  return std::string(reinterpret_cast<const char*>(hash), 32);
}

uint32_t HeaderCrcV1(const ChunkRecordHeaderV1& hdr) {
  ChunkRecordHeaderV1 tmp = hdr;
  tmp.record_crc32 = 0;
  return Crc32(&tmp, sizeof(tmp));
}

uint32_t HeaderCrcV2(const ChunkRecordHeader& hdr) {
  ChunkRecordHeader tmp = hdr;
  tmp.record_crc32 = 0;
  return Crc32(&tmp, sizeof(tmp));
}

ChunkCodec EncryptCodec(ChunkCodec base) {
  switch (base) {
    case ChunkCodec::kLz4:
      return ChunkCodec::kEncryptedLz4;
    case ChunkCodec::kZstd:
      return ChunkCodec::kEncryptedZstd;
    default:
      return ChunkCodec::kEncrypted;
  }
}

Status WriteAllFd(int fd, const uint8_t* data, size_t len) {
  size_t off = 0;
  while (off < len) {
#ifdef _WIN32
    const int n = _write(fd, reinterpret_cast<const char*>(data + off),
                         static_cast<unsigned int>(len - off));
#else
    const ssize_t n = write(fd, data + off, len - off);
#endif
    if (n <= 0) return Status::IoError("chunk write failed");
    off += static_cast<size_t>(n);
  }
  return Status::Ok();
}

}  // namespace

ChunkStore::ChunkStore(std::string path) : path_(std::move(path)) {}

ChunkStore::~ChunkStore() {
  if (append_fd_ >= 0) {
#ifdef _WIN32
    _close(append_fd_);
#else
    close(append_fd_);
#endif
    append_fd_ = -1;
  }
}

void ChunkStore::SetContentKey(const uint8_t key[32]) {
  if (key) {
    std::memcpy(content_key_, key, 32);
    has_content_key_ = true;
  }
}

void ChunkStore::ClearContentKey() {
  std::memset(content_key_, 0, sizeof(content_key_));
  has_content_key_ = false;
}

std::string ChunkStore::TombstonePath() const {
  return (std::filesystem::path(path_).parent_path() / "tombstones").string();
}

Status ChunkStore::LoadTombstones() {
  tombstones_.clear();
  const std::string path = TombstonePath();
  if (!std::filesystem::exists(PathFromUtf8(path))) return Status::Ok();
  std::ifstream in(PathFromUtf8(path));
  if (!in) return Status::IoError("cannot open tombstones: " + path);
  std::string line;
  while (std::getline(in, line)) {
    if (line.size() != 64) continue;
    uint8_t hash[32];
    if (!HexToBytes(line, hash, 32)) continue;
    tombstones_.insert(HashKey(hash));
  }
  return Status::Ok();
}

CompressMode ChunkStore::ResolveCompressMode(
    const ChunkStorePutOptions* options) const {
  if (!options) return CompressMode::kOff;
  if (options->compress_mode != CompressMode::kOff) {
    return options->compress_mode;
  }
  return options->use_lz4 ? CompressMode::kLz4 : CompressMode::kOff;
}

size_t ChunkStore::PackFlushBytes() const {
  return durability_mode_ == DurabilityMode::kBalanced
             ? kBalancedPackFlushBytes
             : kStrictPackFlushBytes;
}

uint64_t ChunkStore::PackFlushRecords() const {
  return durability_mode_ == DurabilityMode::kBalanced
             ? kBalancedPackFlushRecords
             : kStrictPackFlushRecords;
}

Status ChunkStore::ReadParsedHeaderAt(uint64_t offset,
                                      ParsedHeader* parsed) const {
  if (!parsed) return Status::InvalidArgument("parsed is null");
  std::ifstream in(PathFromUtf8(path_), std::ios::binary);
  if (!in) return Status::IoError("cannot open chunk store: " + path_);
  in.seekg(static_cast<std::streamoff>(offset));

  ChunkRecordHeaderV1 v1{};
  in.read(reinterpret_cast<char*>(&v1), sizeof(v1));
  if (!in) return Status::IoError("chunk header read short");

  const uint32_t stored_crc = v1.record_crc32;
  v1.record_crc32 = 0;
  if (HeaderCrcV1(v1) == stored_crc) {
    parsed->header_size = kChunkHeaderV1Size;
    std::memcpy(parsed->header.hash, v1.hash, 32);
    parsed->header.stored_len = v1.raw_len;
    parsed->header.uncompressed_len = v1.raw_len;
    parsed->header.codec = static_cast<uint8_t>(ChunkCodec::kRaw);
    parsed->header.record_crc32 = stored_crc;
    return Status::Ok();
  }

  in.seekg(static_cast<std::streamoff>(offset));
  in.read(reinterpret_cast<char*>(&parsed->header), sizeof(parsed->header));
  if (!in) return Status::IoError("chunk header read short");
  const uint32_t v2_crc = parsed->header.record_crc32;
  parsed->header.record_crc32 = 0;
  if (HeaderCrcV2(parsed->header) != v2_crc) {
    return Status::Corrupt("chunk record header crc mismatch");
  }
  parsed->header.record_crc32 = v2_crc;
  parsed->header_size = kChunkHeaderV2Size;
  return Status::Ok();
}

std::string ChunkStore::PacksDir() const {
  return (std::filesystem::path(path_).parent_path() / "packs").string();
}

uint64_t ChunkStore::PhysicalBytes() const {
  uint64_t total = file_size_;
  const std::string packs_dir = PacksDir();
  if (!std::filesystem::exists(packs_dir)) return total;
  for (const auto& ent : std::filesystem::directory_iterator(packs_dir)) {
    if (!ent.is_regular_file()) continue;
    if (ent.path().extension() == ".ebpack") {
      std::error_code ec;
      const auto sz = ent.file_size(ec);
      if (!ec) total += static_cast<uint64_t>(sz);
    }
  }
  return total;
}

uint64_t ChunkStore::record_count() const {
  uint64_t total = 0;
  for (size_t shard = 0; shard < kIndexShardCount; ++shard) {
    std::lock_guard<std::mutex> lock(shard_index_mu_[shard]);
    total += shard_index_[shard].size();
  }
  return total;
}

uint64_t ChunkStore::ComputeReferencedLiveBytes(
    const std::unordered_set<std::string>& referenced) const {
  uint64_t live = 0;
  std::unordered_set<std::string> pack_paths;
  for (size_t shard = 0; shard < kIndexShardCount; ++shard) {
    std::vector<std::pair<std::string, ChunkLocation>> entries;
    {
      std::lock_guard<std::mutex> lock(shard_index_mu_[shard]);
      entries.reserve(shard_index_[shard].size());
      for (const auto& kv : shard_index_[shard]) {
        entries.emplace_back(kv.first, kv.second);
      }
    }
    for (const auto& kv : entries) {
      {
        std::lock_guard<std::mutex> tomb_lock(tombstones_mu_);
        if (tombstones_.count(kv.first) > 0) continue;
      }
      if (referenced.find(kv.first) == referenced.end()) continue;
      live += kChunkHeaderV2Size + kv.second.stored_len;
      if (!kv.second.pack_path.empty()) {
        pack_paths.insert(kv.second.pack_path);
      }
    }
  }
  live += pack_paths.size() * kEbPackHeaderSize;
  return live;
}

Status ChunkStore::LoadIndex() {
  for (auto& shard : shard_index_) {
    shard.clear();
  }
  index_entries_.clear();
  file_size_ = 0;
  if (!use_ebpack_ && !std::filesystem::exists(PathFromUtf8(path_))) {
    return Status::Ok();
  }
  if (std::filesystem::exists(PathFromUtf8(path_))) {
    file_size_ = std::filesystem::file_size(PathFromUtf8(path_));
  }
  const uint64_t scan_end = file_size_;

  if (use_persistent_index_) {
    const std::string idx_path = ChunkIndexFile::PathForStore(path_);
    if (!std::filesystem::exists(idx_path)) {
      return Status::Ok();
    }
    ChunkIndexFile index_file;
    const Status idx_st =
        index_file.Load(idx_path, file_size_, &index_entries_);
    if (idx_st.ok()) {
      for (const auto& entry : index_entries_) {
        ChunkLocation loc{};
        loc.offset = entry.offset;
        loc.stored_len = entry.stored_len;
        loc.uncompressed_len = entry.uncompressed_len;
        loc.codec = entry.codec;
        if (entry.storage_flags == kChunkStorageEbPack) {
          loc.pack_path = (std::filesystem::path(PacksDir()) / entry.pack_name)
                              .string();
        }
        const size_t shard = IndexShardForHash(entry.hash);
        shard_index_[shard][HashKey(entry.hash)] = std::move(loc);
      }
      return Status::Ok();
    }
    if (use_persistent_index_ && use_ebpack_) {
      return idx_st;
    }
  }

  if (use_ebpack_ && use_persistent_index_) {
    return Status::Corrupt("ebpack persistent index missing or corrupt");
  }

  if (use_ebpack_) {
    return Status::Ok();
  }

  uint64_t offset = 0;
  while (offset < scan_end) {
    ParsedHeader parsed{};
    const Status hdr_st = ReadParsedHeaderAt(offset, &parsed);
    if (!hdr_st.ok()) {
      if (offset > 0 && offset < scan_end) {
        std::error_code ec;
        std::filesystem::resize_file(path_, static_cast<std::uintmax_t>(offset),
                                     ec);
        if (ec) {
          return Status::IoError("cannot truncate corrupt chunk tail: " + path_);
        }
        file_size_ = offset;
        break;
      }
      return hdr_st;
    }
    ChunkLocation loc{};
    loc.offset = offset;
    loc.stored_len = parsed.header.stored_len;
    loc.uncompressed_len = parsed.header.uncompressed_len;
    loc.codec = parsed.header.codec;
    const size_t shard = IndexShardForHash(parsed.header.hash);
    shard_index_[shard][HashKey(parsed.header.hash)] = loc;
    ChunkIndexEntry entry{};
    std::memcpy(entry.hash, parsed.header.hash, 32);
    entry.offset = offset;
    entry.stored_len = parsed.header.stored_len;
    entry.uncompressed_len = parsed.header.uncompressed_len;
    entry.codec = parsed.header.codec;
    index_entries_.push_back(entry);
    offset += parsed.header_size + parsed.header.stored_len;
  }
  file_size_ = offset;
  return Status::Ok();
}

Status ChunkStore::Open() {
  const std::string idx_path = ChunkIndexFile::PathForStore(path_);
  if (std::filesystem::exists(idx_path)) {
    use_persistent_index_ = true;
  }
  const Status idx = LoadIndex();
  if (!idx.ok()) return idx;
  if (!use_ebpack_) {
    for (const auto& entry : index_entries_) {
      if (entry.storage_flags == kChunkStorageEbPack) {
        use_ebpack_ = true;
        break;
      }
    }
  }
  if (!use_ebpack_) {
    const std::string packs = PacksDir();
    if (std::filesystem::exists(packs)) {
      for (const auto& ent : std::filesystem::directory_iterator(packs)) {
        if (ent.is_regular_file() && ent.path().extension() == ".ebpack") {
          use_ebpack_ = true;
          break;
        }
      }
    }
  }
  const Status ts = LoadTombstones();
  if (!ts.ok()) return ts;
  return Status::Ok();
}

void ChunkStore::TrackIndexEntry(const uint8_t hash[32], uint64_t offset,
                                 const ChunkRecordHeader& hdr,
                                 const std::string& pack_path) {
  ChunkIndexEntry entry{};
  std::memcpy(entry.hash, hash, 32);
  entry.offset = offset;
  entry.stored_len = hdr.stored_len;
  entry.uncompressed_len = hdr.uncompressed_len;
  entry.codec = hdr.codec;
  if (!pack_path.empty()) {
    entry.storage_flags = kChunkStorageEbPack;
    const std::string name = std::filesystem::path(pack_path).filename().string();
    std::strncpy(entry.pack_name, name.c_str(), sizeof(entry.pack_name) - 1);
  }
  {
    std::lock_guard<std::mutex> lock(index_entries_mu_);
    index_entries_.push_back(entry);
  }
}

Status ChunkStore::SavePersistentIndex() const {
  if (!use_persistent_index_) return Status::Ok();
  std::vector<ChunkIndexEntry> entries_copy;
  {
    std::lock_guard<std::mutex> lock(index_entries_mu_);
    entries_copy = index_entries_;
  }
  ChunkIndexFile index_file;
  return index_file.Save(ChunkIndexFile::PathForStore(path_), file_size_,
                         entries_copy);
}

Status ChunkStore::EnsureAppendSession() {
  if (append_fd_ >= 0) return Status::Ok();
  std::filesystem::create_directories(
      std::filesystem::path(path_).parent_path());
#ifdef _WIN32
  const std::wstring wide = Utf8ToWide(path_);
  if (_wsopen_s(&append_fd_, wide.c_str(),
                _O_RDWR | _O_BINARY | _O_CREAT | _O_APPEND, _SH_DENYNO,
                _S_IREAD | _S_IWRITE) != 0) {
    append_fd_ = -1;
    return Status::IoError("cannot open chunk store for append: " + path_);
  }
#else
  append_fd_ =
      open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND, static_cast<mode_t>(0644));
  if (append_fd_ < 0) {
    return Status::IoError("cannot open chunk store for append: " + path_);
  }
#endif
  return Status::Ok();
}

Status ChunkStore::BeginAppendSession() {
  if (append_session_active_) return Status::Ok();
  if (use_ebpack_) {
    eb_pack_shards_ = std::make_unique<EbPackShardSet>(
        PacksDir(), append_txn_id_, PackFlushBytes(), PackFlushRecords());
    append_session_active_ = true;
    return Status::Ok();
  }
  const Status st = EnsureAppendSession();
  if (!st.ok()) return st;
  append_session_active_ = true;
  return Status::Ok();
}

Status ChunkStore::EndAppendSession() {
  if (!append_session_active_) return Status::Ok();
  append_session_active_ = false;
  eb_pack_shards_.reset();
  if (append_fd_ >= 0) {
#ifdef _WIN32
    _close(append_fd_);
#else
    close(append_fd_);
#endif
    append_fd_ = -1;
  }
  return Status::Ok();
}

Status ChunkStore::WriteBufferToSession(bool fsync_after) {
  if (write_buffer_.empty()) return Status::Ok();

  if (append_fd_ < 0) {
    std::ofstream out(PathFromUtf8(path_), std::ios::binary | std::ios::app);
    if (!out) return Status::IoError("cannot flush chunk buffer");
    out.write(reinterpret_cast<const char*>(write_buffer_.data()),
              static_cast<std::streamsize>(write_buffer_.size()));
    out.flush();
    if (!out) return Status::IoError("chunk buffer flush failed");
    out.close();
    write_buffer_.clear();
    pending_records_ = 0;
    if (fsync_after) {
      return FsyncPath(path_);
    }
    return Status::Ok();
  }

  const Status wr =
      WriteAllFd(append_fd_, write_buffer_.data(), write_buffer_.size());
  if (!wr.ok()) return wr;
  write_buffer_.clear();
  pending_records_ = 0;
  if (fsync_after) {
    return FsyncFd(append_fd_);
  }
  return Status::Ok();
}

Status ChunkStore::MaybeFlushPack() {
  if (!append_session_active_) return Status::Ok();
  if (write_buffer_.size() < PackFlushBytes() &&
      pending_records_ < PackFlushRecords()) {
    return Status::Ok();
  }
  return WriteBufferToSession(false);
}

Status ChunkStore::Flush() {
  if (use_ebpack_ && eb_pack_shards_) {
    const Status spill = eb_pack_shards_->FlushAllOpenPacks(true);
    if (!spill.ok()) return spill;
    const Status fs = eb_pack_shards_->FsyncAll();
    if (!fs.ok()) return fs;
    if (!defer_persistent_index_save_) {
      return SavePersistentIndex();
    }
    return Status::Ok();
  }
  const Status wr = WriteBufferToSession(true);
  if (!wr.ok()) return wr;
  if (!defer_persistent_index_save_) {
    return SavePersistentIndex();
  }
  return Status::Ok();
}

Status ChunkStore::AppendRecordEbPack(const uint8_t hash[32],
                                      const uint8_t* data, size_t len,
                                      ChunkCodec codec,
                                      uint32_t uncompressed_len) {
  if (!eb_pack_shards_) {
    return Status::Internal("ebpack shard set not active");
  }
  EbPackRecordRef ref{};
  const Status st = eb_pack_shards_->AppendRecord(hash, data, len, uncompressed_len,
                                                    codec, &ref);
  if (!st.ok()) return st;

  ChunkRecordHeader hdr{};
  std::memcpy(hdr.hash, hash, 32);
  hdr.stored_len = static_cast<uint32_t>(len);
  hdr.uncompressed_len = uncompressed_len;
  hdr.codec = static_cast<uint8_t>(codec);

  {
    const size_t shard = IndexShardForHash(hash);
    std::lock_guard<std::mutex> lock(shard_index_mu_[shard]);
    ChunkLocation loc{};
    loc.offset = ref.offset;
    loc.pack_path = ref.pack_path;
    loc.stored_len = ref.stored_len;
    loc.uncompressed_len = ref.uncompressed_len;
    loc.codec = ref.codec;
    shard_index_[shard][HashKey(hash)] = loc;
    TrackIndexEntry(hash, ref.offset, hdr, ref.pack_path);
  }
  return Status::Ok();
}

Status ChunkStore::AppendRecordInternal(const uint8_t hash[32],
                                        const uint8_t* data, size_t len,
                                        ChunkCodec codec,
                                        uint32_t uncompressed_len) {
  if (use_ebpack_) {
    return AppendRecordEbPack(hash, data, len, codec, uncompressed_len);
  }

  std::lock_guard<std::mutex> lock(append_mu_);

  ChunkRecordHeader hdr{};
  std::memcpy(hdr.hash, hash, 32);
  hdr.stored_len = static_cast<uint32_t>(len);
  hdr.uncompressed_len = uncompressed_len;
  hdr.codec = static_cast<uint8_t>(codec);
  hdr.record_crc32 = HeaderCrcV2(hdr);

  const uint64_t offset = file_size_;
  const size_t before = write_buffer_.size();
  write_buffer_.resize(before + sizeof(hdr) + len);
  std::memcpy(write_buffer_.data() + before, &hdr, sizeof(hdr));
  std::memcpy(write_buffer_.data() + before + sizeof(hdr), data, len);
  ++pending_records_;
  ChunkLocation loc{};
  loc.offset = offset;
  loc.stored_len = hdr.stored_len;
  loc.uncompressed_len = hdr.uncompressed_len;
  loc.codec = hdr.codec;
  {
    const size_t shard = IndexShardForHash(hash);
    std::lock_guard<std::mutex> shard_lock(shard_index_mu_[shard]);
    shard_index_[shard][HashKey(hash)] = loc;
  }
  file_size_ += sizeof(hdr) + len;
  TrackIndexEntry(hash, offset, hdr);

  if (append_session_active_) {
    return MaybeFlushPack();
  }

  const Status st = EnsureAppendSession();
  if (!st.ok()) return st;
  const Status flush_st = WriteBufferToSession(true);
  if (!flush_st.ok()) return flush_st;
#ifdef _WIN32
  _close(append_fd_);
#else
  close(append_fd_);
#endif
  append_fd_ = -1;
  if (!defer_persistent_index_save_) {
    return SavePersistentIndex();
  }
  return Status::Ok();
}

Status ChunkStore::AppendRecord(const uint8_t hash[32], const uint8_t* data,
                                size_t len, ChunkCodec codec,
                                uint32_t uncompressed_len) {
  return AppendRecordInternal(hash, data, len, codec, uncompressed_len);
}

Status ChunkStore::Put(const uint8_t* data, size_t len, uint8_t hash_out[32],
                       bool* newly_written,
                       const ChunkStorePutOptions* options) {
  if (!data || !hash_out) {
    return Status::InvalidArgument("null argument");
  }
  ContentHash(digest_algo_, data, len, hash_out);
  return PutKnownHash(data, len, hash_out, newly_written, options);
}

Status ChunkStore::PutPrecompressed(const uint8_t hash[32],
                                    const uint8_t* payload, size_t stored_len,
                                    uint32_t uncompressed_len, ChunkCodec codec,
                                    bool* newly_written,
                                    bool skip_exists_check) {
  if (!hash) return Status::InvalidArgument("hash is null");
  const char* fail_env = std::getenv("EBTEST_PIPELINE_STORE_FAIL");
  if (fail_env && fail_env[0] == '1') {
    return Status::IoError("test injected pipeline store failure");
  }
  if (!skip_exists_check && !pipeline_dedup_trust_) {
    if (Exists(hash)) {
      if (newly_written) *newly_written = false;
      return Status::Ok();
    }
  }
  const Status st =
      AppendRecord(hash, payload, stored_len, codec, uncompressed_len);
  if (!st.ok()) return st;
  if (newly_written) *newly_written = true;
  return Status::Ok();
}

Status ChunkStore::PutKnownHash(const uint8_t* data, size_t len,
                                const uint8_t hash[32], bool* newly_written,
                                const ChunkStorePutOptions* options) {
  if (!hash) return Status::InvalidArgument("hash is null");
  if (Exists(hash)) {
    if (newly_written) *newly_written = false;
    return Status::Ok();
  }
  if (!data && len > 0) {
    return Status::InvalidArgument("data is null");
  }

  const bool use_encryption = options && options->use_encryption;
  const uint8_t* key =
      (options && options->content_key) ? options->content_key
                                        : (has_content_key_ ? content_key_
                                                            : nullptr);
  if (use_encryption && !key) {
    return Status::InvalidArgument("encryption requires content key");
  }

  const CompressMode mode = ResolveCompressMode(options);
  ContentEncodeRequest enc_req{};
  enc_req.data = data;
  enc_req.len = len;
  enc_req.mode = mode;
  enc_req.cpu_budget_permille =
      options ? options->cpu_budget_permille : 1000u;
  enc_req.path_hint = options ? options->path_hint : nullptr;

  ContentClassStats delta{};
  ContentEncodeResult encoded{};
  const Status enc_st = ContentClassEncode(
      enc_req, &encoded, options ? options->content_stats : &delta);
  if (!enc_st.ok()) return enc_st;
  if (options && options->content_stats) {
    options->content_stats->incompressible_skips += delta.incompressible_skips;
    options->content_stats->lz4_only += delta.lz4_only;
    options->content_stats->zstd_attempts += delta.zstd_attempts;
    options->content_stats->zstd_wins += delta.zstd_wins;
    options->content_stats->cpu_budget_spent_permille +=
        delta.cpu_budget_spent_permille;
  }

  ChunkCodec codec = encoded.codec;
  std::vector<uint8_t> stored = std::move(encoded.payload);

  if (use_encryption) {
    std::vector<uint8_t> encrypted;
    const Status encr_st =
        crypto::Aes256GcmEncrypt(key, stored.data(), stored.size(), &encrypted);
    if (!encr_st.ok()) return encr_st;
    stored = std::move(encrypted);
    codec = EncryptCodec(codec);
  }

  const Status st = AppendRecord(hash, stored.data(), stored.size(), codec,
                                 encoded.uncompressed_len);
  if (!st.ok()) return st;
  if (newly_written) *newly_written = true;
  return Status::Ok();
}

bool ChunkStore::Exists(const uint8_t hash[32]) const {
  const std::string key = HashKey(hash);
  {
    std::lock_guard<std::mutex> tomb_lock(tombstones_mu_);
    if (tombstones_.count(key) > 0) return false;
  }
  const size_t shard = IndexShardForKey(key);
  std::lock_guard<std::mutex> lock(shard_index_mu_[shard]);
  return shard_index_[shard].find(key) != shard_index_[shard].end();
}

void ChunkStore::ExistsMany(const uint8_t* const hashes[], size_t count,
                            bool* out_exists) const {
  if (!hashes || !out_exists || count == 0) return;
  for (size_t i = 0; i < count; ++i) out_exists[i] = false;

  for (size_t i = 0; i < count; ++i) {
    if (!hashes[i]) continue;
    const std::string key = HashKey(hashes[i]);
    {
      std::lock_guard<std::mutex> tomb_lock(tombstones_mu_);
      if (tombstones_.count(key) > 0) continue;
    }
    const size_t shard = IndexShardForHash(hashes[i]);
    std::lock_guard<std::mutex> lock(shard_index_mu_[shard]);
    if (shard_index_[shard].find(key) != shard_index_[shard].end()) {
      out_exists[i] = true;
    }
  }
}

Status ChunkStore::ReadEbPackRecordAt(const std::string& pack_path,
                                      uint64_t offset, ParsedHeader* parsed,
                                      std::vector<uint8_t>* payload) {
  if (!parsed) return Status::InvalidArgument("parsed is null");
  std::ifstream in(PathFromUtf8(pack_path), std::ios::binary);
  if (!in) return Status::IoError("cannot open ebpack: " + pack_path);

  EbPackHeader hdr{};
  in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
  if (!in) return Status::Corrupt("ebpack header short");
  if (std::memcmp(hdr.magic, kEbPackMagic, sizeof(kEbPackMagic)) != 0) {
    return Status::Corrupt("ebpack bad magic");
  }
  if (offset < kEbPackHeaderSize || offset >= hdr.pack_size) {
    return Status::Corrupt("ebpack record offset out of range");
  }

  in.seekg(static_cast<std::streamoff>(offset));
  in.read(reinterpret_cast<char*>(&parsed->header), sizeof(parsed->header));
  if (!in) return Status::IoError("ebpack record header short");
  parsed->header_size = kChunkHeaderV2Size;

  if (!payload) return Status::Ok();
  payload->resize(parsed->header.stored_len);
  in.read(reinterpret_cast<char*>(payload->data()),
          static_cast<std::streamsize>(parsed->header.stored_len));
  if (!in) return Status::Corrupt("ebpack payload read short");
  return Status::Ok();
}

Status ChunkStore::ReadRecordAt(uint64_t offset, ParsedHeader* parsed,
                                std::vector<uint8_t>* payload) const {
  const Status hdr_st = ReadParsedHeaderAt(offset, parsed);
  if (!hdr_st.ok()) return hdr_st;
  if (!payload) return Status::Ok();
  std::ifstream in(PathFromUtf8(path_), std::ios::binary);
  if (!in) return Status::IoError("cannot open chunk store: " + path_);
  in.seekg(static_cast<std::streamoff>(offset + parsed->header_size));
  payload->resize(parsed->header.stored_len);
  in.read(reinterpret_cast<char*>(payload->data()),
          static_cast<std::streamsize>(parsed->header.stored_len));
  if (!in) return Status::Corrupt("chunk payload read short");
  return Status::Ok();
}

Status ChunkStore::DecodePayload(const ParsedHeader& parsed,
                                 std::vector<uint8_t> payload,
                                 std::vector<uint8_t>* out) const {
  if (!out) return Status::InvalidArgument("out is null");
  const auto codec = static_cast<ChunkCodec>(parsed.header.codec);

  if (codec == ChunkCodec::kEncrypted || codec == ChunkCodec::kEncryptedLz4 ||
      codec == ChunkCodec::kEncryptedZstd) {
    if (!has_content_key_) {
      return Status::InvalidArgument("encrypted chunk requires content key");
    }
    std::vector<uint8_t> decrypted;
    const Status st = crypto::Aes256GcmDecrypt(content_key_, payload.data(),
                                               payload.size(), &decrypted);
    if (!st.ok()) return st;
    payload = std::move(decrypted);
  }

  if (codec == ChunkCodec::kLz4 || codec == ChunkCodec::kEncryptedLz4) {
    std::vector<uint8_t> decoded;
    const Status st = Lz4Decompress(payload.data(), payload.size(),
                                    parsed.header.uncompressed_len, &decoded);
    if (!st.ok()) return st;
    payload = std::move(decoded);
  } else if (codec == ChunkCodec::kZstd ||
             codec == ChunkCodec::kEncryptedZstd) {
    std::vector<uint8_t> decoded;
    const Status st = ZstdDecompress(payload.data(), payload.size(),
                                   parsed.header.uncompressed_len, &decoded);
    if (!st.ok()) return st;
    payload = std::move(decoded);
  }

  *out = std::move(payload);
  return Status::Ok();
}

Status ChunkStore::ReadRecordForHash(const uint8_t hash[32],
                                     ParsedHeader* parsed,
                                     std::vector<uint8_t>* payload) const {
  const std::string key = HashKey(hash);
  const size_t shard = IndexShardForKey(key);
  ChunkLocation loc{};
  {
    std::lock_guard<std::mutex> lock(shard_index_mu_[shard]);
    const auto it = shard_index_[shard].find(key);
    if (it == shard_index_[shard].end()) {
      return Status::NotFound("chunk not found");
    }
    loc = it->second;
  }
  if (!loc.pack_path.empty()) {
    return ReadEbPackRecordAt(loc.pack_path, loc.offset, parsed, payload);
  }
  return ReadRecordAt(loc.offset, parsed, payload);
}

Status ChunkStore::Get(const uint8_t hash[32], std::vector<uint8_t>* out) const {
  if (!out) return Status::InvalidArgument("out is null");
  const std::string key = HashKey(hash);
  {
    std::lock_guard<std::mutex> tomb_lock(tombstones_mu_);
    if (tombstones_.count(key) > 0) {
      return Status::NotFound("chunk tombstoned");
    }
  }
  ParsedHeader parsed{};
  std::vector<uint8_t> payload;
  Status st = ReadRecordForHash(hash, &parsed, &payload);
  if (!st.ok()) return st;

  st = DecodePayload(parsed, std::move(payload), out);
  if (!st.ok()) return st;

  uint8_t computed[32];
  ContentHash(digest_algo_, out->data(), out->size(), computed);
  if (std::memcmp(computed, hash, 32) != 0) {
    return Status::Corrupt("chunk hash mismatch");
  }
  return Status::Ok();
}

Status ChunkStore::ForEachRecord(const RecordCallback& cb) const {
  if (!cb) return Status::InvalidArgument("callback is null");
  struct RecordRef {
    uint8_t hash[32]{};
    uint64_t offset{0};
    uint32_t stored_len{0};
  };
  std::vector<RecordRef> refs;
  refs.reserve(static_cast<size_t>(record_count()));
  for (size_t shard = 0; shard < kIndexShardCount; ++shard) {
    std::lock_guard<std::mutex> lock(shard_index_mu_[shard]);
    for (const auto& kv : shard_index_[shard]) {
      RecordRef ref{};
      std::memcpy(ref.hash, kv.first.data(), 32);
      ref.offset = kv.second.offset;
      ref.stored_len = kv.second.stored_len;
      refs.push_back(ref);
    }
  }
  {
    std::lock_guard<std::mutex> tomb_lock(tombstones_mu_);
    refs.erase(
        std::remove_if(refs.begin(), refs.end(),
                       [this](const RecordRef& ref) {
                         return tombstones_.count(HashKey(ref.hash)) > 0;
                       }),
        refs.end());
  }
  for (const RecordRef& ref : refs) {
    const Status cb_st = cb(ref.hash, ref.offset, ref.stored_len);
    if (!cb_st.ok()) return cb_st;
  }
  return Status::Ok();
}

Status ChunkStore::TombstoneHash(const uint8_t hash[32]) {
  const std::string key = HashKey(hash);
  const size_t shard = IndexShardForKey(key);
  {
    std::lock_guard<std::mutex> lock(shard_index_mu_[shard]);
    if (shard_index_[shard].find(key) == shard_index_[shard].end()) {
      return Status::NotFound("chunk not in store");
    }
  }
  {
    std::lock_guard<std::mutex> tomb_lock(tombstones_mu_);
    if (tombstones_.count(key) > 0) return Status::Ok();
    tombstones_.insert(key);
  }
  std::ofstream out(PathFromUtf8(TombstonePath()), std::ios::app);
  if (!out) return Status::IoError("cannot append tombstone");
  out << BytesToHex(hash, 32) << "\n";
  out.flush();
  if (!out) return Status::IoError("tombstone write failed");
  out.close();
  return FsyncPath(TombstonePath());
}

Status ChunkStore::CorruptRecordAtOffsetForTest(uint64_t offset) {
  std::fstream io(path_, std::ios::binary | std::ios::in | std::ios::out);
  if (!io) {
    return Status::IoError("cannot open chunk store for corrupt");
  }
  io.seekp(static_cast<std::streamoff>(offset));
  const uint8_t bad = 0xFF;
  io.write(reinterpret_cast<const char*>(&bad), 1);
  if (!io) {
    return Status::IoError("corrupt write failed");
  }
  return Status::Ok();
}

}  // namespace ebbackup
