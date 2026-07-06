#include "ebbackup/store/chunk_store.h"

#include <cstring>
#include <filesystem>
#include <fstream>

#include "ebbackup/codec/lz4_codec.h"
#include "ebbackup/common/crc32.h"
#include "ebbackup/common/digest.h"
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

}  // namespace

ChunkStore::ChunkStore(std::string path) : path_(std::move(path)) {}

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
  if (!std::filesystem::exists(path)) return Status::Ok();
  std::ifstream in(path);
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

Status ChunkStore::Open() {
  const Status idx = LoadIndex();
  if (!idx.ok()) return idx;
  return LoadTombstones();
}

Status ChunkStore::ReadParsedHeaderAt(uint64_t offset, ParsedHeader* parsed) const {
  if (!parsed) return Status::InvalidArgument("parsed is null");
  std::ifstream in(path_, std::ios::binary);
  if (!in) return Status::IoError("cannot open chunk store: " + path_);
  in.seekg(static_cast<std::streamoff>(offset));

  ChunkRecordHeaderV1 v1{};
  in.read(reinterpret_cast<char*>(&v1), sizeof(v1));
  if (!in) return Status::IoError("chunk header read short");

  const uint32_t stored_crc = v1.record_crc32;
  v1.record_crc32 = 0;
  if (Crc32(&v1, sizeof(v1)) == stored_crc) {
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
  if (Crc32(&parsed->header, sizeof(parsed->header)) != v2_crc) {
    return Status::Corrupt("chunk record header crc mismatch");
  }
  parsed->header.record_crc32 = v2_crc;
  parsed->header_size = kChunkHeaderV2Size;
  return Status::Ok();
}

Status ChunkStore::LoadIndex() {
  index_.clear();
  file_size_ = 0;
  if (!std::filesystem::exists(path_)) {
    return Status::Ok();
  }
  uint64_t offset = 0;
  while (offset < std::filesystem::file_size(path_)) {
    ParsedHeader parsed{};
    const Status hdr_st = ReadParsedHeaderAt(offset, &parsed);
    if (!hdr_st.ok()) return hdr_st;
    index_[HashKey(parsed.header.hash)] = offset;
    offset += parsed.header_size + parsed.header.stored_len;
    file_size_ = offset;
  }
  return Status::Ok();
}

Status ChunkStore::AppendRecord(const uint8_t hash[32], const uint8_t* data,
                                size_t len, ChunkCodec codec,
                                uint32_t uncompressed_len) {
  std::filesystem::create_directories(
      std::filesystem::path(path_).parent_path());
  std::ofstream out(path_, std::ios::binary | std::ios::app);
  if (!out) {
    return Status::IoError("cannot append chunk: " + path_);
  }
  ChunkRecordHeader hdr{};
  std::memcpy(hdr.hash, hash, 32);
  hdr.stored_len = static_cast<uint32_t>(len);
  hdr.uncompressed_len = uncompressed_len;
  hdr.codec = static_cast<uint8_t>(codec);
  hdr.record_crc32 = HeaderCrcV2(hdr);
  out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
  out.write(reinterpret_cast<const char*>(data),
            static_cast<std::streamsize>(len));
  out.flush();
  if (!out) {
    return Status::IoError("chunk write failed");
  }
  out.close();
  const Status fs = FsyncPath(path_);
  if (!fs.ok()) return fs;
  index_[HashKey(hash)] = file_size_;
  file_size_ += sizeof(hdr) + len;
  return Status::Ok();
}

Status ChunkStore::Put(const uint8_t* data, size_t len, uint8_t hash_out[32],
                       bool* newly_written, const ChunkStorePutOptions* options) {
  if (!data || !hash_out) {
    return Status::InvalidArgument("null argument");
  }
  Sha256(data, len, hash_out);
  return PutKnownHash(data, len, hash_out, newly_written, options);
}

Status ChunkStore::PutPrecompressed(const uint8_t hash[32],
                                    const uint8_t* payload, size_t stored_len,
                                    uint32_t uncompressed_len, ChunkCodec codec,
                                    bool* newly_written) {
  if (!hash) return Status::InvalidArgument("hash is null");
  if (Exists(hash)) {
    if (newly_written) *newly_written = false;
    return Status::Ok();
  }
  if (!payload && stored_len > 0) {
    return Status::InvalidArgument("payload is null");
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

  const bool use_lz4 = options && options->use_lz4;
  const bool use_encryption = options && options->use_encryption;
  const uint8_t* key =
      (options && options->content_key) ? options->content_key
                                        : (has_content_key_ ? content_key_ : nullptr);
  if (use_encryption && !key) {
    return Status::InvalidArgument("encryption requires content key");
  }

  std::vector<uint8_t> stored;
  ChunkCodec codec = ChunkCodec::kRaw;
  if (use_lz4) {
    Lz4EncodeResult encoded{};
    const Status enc = Lz4Compress(data, len, &encoded);
    if (!enc.ok()) return enc;
    if (encoded.compressed) {
      stored = std::move(encoded.payload);
      codec = ChunkCodec::kLz4;
    } else {
      stored.assign(data, data + len);
    }
  } else {
    stored.assign(data, data + len);
  }

  if (use_encryption) {
    std::vector<uint8_t> encrypted;
    const Status enc_st =
        crypto::Aes256GcmEncrypt(key, stored.data(), stored.size(), &encrypted);
    if (!enc_st.ok()) return enc_st;
    stored = std::move(encrypted);
    codec = (codec == ChunkCodec::kLz4) ? ChunkCodec::kEncryptedLz4
                                        : ChunkCodec::kEncrypted;
  }

  const Status st = AppendRecord(hash, stored.data(), stored.size(), codec,
                                 static_cast<uint32_t>(len));
  if (!st.ok()) return st;
  if (newly_written) *newly_written = true;
  return Status::Ok();
}

bool ChunkStore::Exists(const uint8_t hash[32]) const {
  const std::string key = HashKey(hash);
  if (tombstones_.count(key) > 0) return false;
  return index_.find(key) != index_.end();
}

Status ChunkStore::ReadRecordAt(uint64_t offset, ParsedHeader* parsed,
                                std::vector<uint8_t>* payload) const {
  const Status hdr_st = ReadParsedHeaderAt(offset, parsed);
  if (!hdr_st.ok()) return hdr_st;
  std::ifstream in(path_, std::ios::binary);
  if (!in) return Status::IoError("cannot open chunk store: " + path_);
  in.seekg(static_cast<std::streamoff>(offset + parsed->header_size));
  payload->resize(parsed->header.stored_len);
  in.read(reinterpret_cast<char*>(payload->data()),
          static_cast<std::streamsize>(parsed->header.stored_len));
  if (!in) return Status::Corrupt("chunk payload read short");
  return Status::Ok();
}

Status ChunkStore::Get(const uint8_t hash[32], std::vector<uint8_t>* out) const {
  if (!out) return Status::InvalidArgument("out is null");
  const std::string key = HashKey(hash);
  if (tombstones_.count(key) > 0) {
    return Status::NotFound("chunk tombstoned");
  }
  const auto it = index_.find(key);
  if (it == index_.end()) {
    return Status::NotFound("chunk not found");
  }
  ParsedHeader parsed{};
  std::vector<uint8_t> payload;
  Status st = ReadRecordAt(it->second, &parsed, &payload);
  if (!st.ok()) return st;

  if (parsed.header.codec == static_cast<uint8_t>(ChunkCodec::kEncrypted) ||
      parsed.header.codec == static_cast<uint8_t>(ChunkCodec::kEncryptedLz4)) {
    if (!has_content_key_) {
      return Status::InvalidArgument("encrypted chunk requires content key");
    }
    std::vector<uint8_t> decrypted;
    st = crypto::Aes256GcmDecrypt(content_key_, payload.data(), payload.size(),
                                  &decrypted);
    if (!st.ok()) return st;
    payload = std::move(decrypted);
  }

  if (parsed.header.codec == static_cast<uint8_t>(ChunkCodec::kLz4) ||
      parsed.header.codec == static_cast<uint8_t>(ChunkCodec::kEncryptedLz4)) {
    std::vector<uint8_t> decoded;
    st = Lz4Decompress(payload.data(), payload.size(),
                       parsed.header.uncompressed_len, &decoded);
    if (!st.ok()) return st;
    payload = std::move(decoded);
  }

  uint8_t computed[32];
  Sha256(payload.data(), payload.size(), computed);
  if (std::memcmp(computed, hash, 32) != 0) {
    return Status::Corrupt("chunk hash mismatch");
  }
  *out = std::move(payload);
  return Status::Ok();
}

Status ChunkStore::ForEachRecord(const RecordCallback& cb) const {
  if (!cb) return Status::InvalidArgument("callback is null");
  for (const auto& kv : index_) {
    if (tombstones_.count(kv.first) > 0) continue;
    uint8_t hash[32];
    std::memcpy(hash, kv.first.data(), 32);
    ParsedHeader parsed{};
    std::vector<uint8_t> payload;
    const Status rd = ReadRecordAt(kv.second, &parsed, &payload);
    if (!rd.ok()) return rd;
    const Status cb_st = cb(hash, kv.second, parsed.header.stored_len);
    if (!cb_st.ok()) return cb_st;
  }
  return Status::Ok();
}

Status ChunkStore::TombstoneHash(const uint8_t hash[32]) {
  const std::string key = HashKey(hash);
  if (index_.find(key) == index_.end()) {
    return Status::NotFound("chunk not in store");
  }
  if (tombstones_.count(key) > 0) return Status::Ok();
  tombstones_.insert(key);
  std::ofstream out(TombstonePath(), std::ios::app);
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
