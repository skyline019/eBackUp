#include "ebbackup/store/eb_pack.h"

#include <climits>
#include <cstring>
#include <filesystem>
#include <vector>

#include "ebbackup/common/crc32.h"
#include "ebbackup/common/fsync.h"
#include "ebbackup/store/chunk_store.h"

namespace ebbackup {

namespace {

uint32_t HeaderCrcV2(const ChunkRecordHeader& hdr) {
  ChunkRecordHeader tmp = hdr;
  tmp.record_crc32 = 0;
  return Crc32(&tmp, sizeof(tmp));
}

std::string MakePackName(uint64_t txn_id, uint32_t seq, uint32_t shard_id) {
  char buf[80];
  if (shard_id != UINT32_MAX) {
    snprintf(buf, sizeof(buf), "pack-%08llx-%04x-s%02x.ebpack",
             static_cast<unsigned long long>(txn_id),
             static_cast<unsigned>(seq), static_cast<unsigned>(shard_id));
  } else {
    snprintf(buf, sizeof(buf), "pack-%08llx-%04x.ebpack",
             static_cast<unsigned long long>(txn_id),
             static_cast<unsigned>(seq));
  }
  return buf;
}

}  // namespace

EbPackWriter::EbPackWriter(std::string packs_dir, uint64_t txn_id,
                           uint32_t shard_id)
    : packs_dir_(std::move(packs_dir)),
      txn_id_(txn_id),
      shard_id_(shard_id) {
  std::filesystem::create_directories(packs_dir_);
}

void EbPackWriter::SetSpillThresholds(size_t max_buffer_bytes,
                                      uint64_t max_records) {
  std::lock_guard<std::mutex> lock(mu_);
  spill_bytes_ = max_buffer_bytes;
  spill_records_ = max_records;
}

Status EbPackWriter::EnsureOpenPack() {
  if (pack_open_) return Status::Ok();

  const std::string name = MakePackName(txn_id_, seq_, shard_id_);
  open_path_ = (std::filesystem::path(packs_dir_) / name).string();
  append_fd_.open(open_path_,
                  std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
  if (!append_fd_) {
    return Status::IoError("cannot create ebpack: " + open_path_);
  }

  EbPackHeader hdr{};
  std::memcpy(hdr.magic, kEbPackMagic, sizeof(kEbPackMagic));
  hdr.txn_id = txn_id_;
  hdr.seq = seq_;
  append_fd_.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
  if (!append_fd_) {
    return Status::IoError("ebpack header write failed: " + open_path_);
  }

  pack_open_ = true;
  open_payload_size_ = 0;
  open_records_ = 0;
  return Status::Ok();
}

uint32_t EbPackWriter::ComputeOpenPayloadCrc32() {
  if (!pack_open_ || open_payload_size_ == 0) return 0;
  std::vector<uint8_t> payload(open_payload_size_);
  append_fd_.clear();
  append_fd_.seekg(static_cast<std::streamoff>(kEbPackHeaderSize));
  append_fd_.read(reinterpret_cast<char*>(payload.data()),
                  static_cast<std::streamsize>(payload.size()));
  if (!append_fd_) return 0;
  append_fd_.clear();
  append_fd_.seekp(0, std::ios::end);
  return Crc32(payload.data(), payload.size());
}

Status EbPackWriter::FinalizeOpenPack(bool fsync_after) {
  if (!pack_open_ || open_records_ == 0) {
    if (pack_open_) {
      append_fd_.close();
      pack_open_ = false;
      open_path_.clear();
      open_payload_size_ = 0;
    }
    return Status::Ok();
  }

  EbPackHeader hdr{};
  std::memcpy(hdr.magic, kEbPackMagic, sizeof(kEbPackMagic));
  hdr.txn_id = txn_id_;
  hdr.seq = seq_;
  hdr.record_count = open_records_;
  hdr.payload_crc32 = ComputeOpenPayloadCrc32();
  hdr.pack_size = kEbPackHeaderSize + open_payload_size_;

  append_fd_.seekp(0);
  append_fd_.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
  append_fd_.flush();
  if (!append_fd_) {
    return Status::IoError("ebpack header patch failed: " + open_path_);
  }
  append_fd_.close();
  pack_open_ = false;

  if (fsync_after) {
    const Status fs = FsyncPath(open_path_);
    if (!fs.ok()) return fs;
    fsynced_paths_.insert(open_path_);
  }

  written_paths_.push_back(open_path_);
  open_payload_size_ = 0;
  open_records_ = 0;
  ++seq_;
  open_path_.clear();
  return Status::Ok();
}

Status EbPackWriter::MaybeSpillBeforeAppend(size_t record_size) {
  if (!pack_open_) return Status::Ok();
  const bool bytes_exceeded =
      open_payload_size_ >= spill_bytes_ ||
      (open_payload_size_ + record_size > spill_bytes_ &&
       open_payload_size_ + record_size > kEbPackTargetBytes);
  const bool records_exceeded =
      spill_records_ != UINT64_MAX && open_records_ >= spill_records_;
  if (bytes_exceeded || records_exceeded) {
    return FinalizeOpenPack(false);
  }
  if (open_payload_size_ + record_size > kEbPackTargetBytes) {
    return FinalizeOpenPack(false);
  }
  return Status::Ok();
}

Status EbPackWriter::AppendRecord(const uint8_t hash[32], const uint8_t* payload,
                                  size_t stored_len, uint32_t uncompressed_len,
                                  ChunkCodec codec, EbPackRecordRef* out_ref) {
  if (!hash || !payload || !out_ref) {
    return Status::InvalidArgument("null ebpack append argument");
  }

  std::lock_guard<std::mutex> lock(mu_);

  ChunkRecordHeader rec{};
  std::memcpy(rec.hash, hash, 32);
  rec.stored_len = static_cast<uint32_t>(stored_len);
  rec.uncompressed_len = uncompressed_len;
  rec.codec = static_cast<uint8_t>(codec);
  rec.record_crc32 = HeaderCrcV2(rec);

  const size_t record_size = sizeof(rec) + stored_len;
  const Status spill = MaybeSpillBeforeAppend(record_size);
  if (!spill.ok()) return spill;

  const Status open_st = EnsureOpenPack();
  if (!open_st.ok()) return open_st;

  const uint64_t payload_offset =
      kEbPackHeaderSize + static_cast<uint64_t>(open_payload_size_);
  append_fd_.write(reinterpret_cast<const char*>(&rec), sizeof(rec));
  append_fd_.write(reinterpret_cast<const char*>(payload),
                   static_cast<std::streamsize>(stored_len));
  if (!append_fd_) {
    return Status::IoError("ebpack record append failed: " + open_path_);
  }

  open_payload_size_ += record_size;
  ++open_records_;

  out_ref->pack_path = open_path_;
  out_ref->offset = payload_offset;
  out_ref->stored_len = rec.stored_len;
  out_ref->uncompressed_len = rec.uncompressed_len;
  out_ref->codec = rec.codec;
  return Status::Ok();
}

Status EbPackWriter::FlushOpenPack(bool fsync_after) {
  std::lock_guard<std::mutex> lock(mu_);
  return FinalizeOpenPack(fsync_after);
}

Status EbPackWriter::FsyncAll() {
  std::lock_guard<std::mutex> lock(mu_);
  for (const auto& path : written_paths_) {
    if (fsynced_paths_.count(path) > 0) continue;
    const Status fs = FsyncPath(path);
    if (!fs.ok()) return fs;
    fsynced_paths_.insert(path);
  }
  if (pack_open_ && !open_path_.empty() &&
      fsynced_paths_.count(open_path_) == 0) {
    append_fd_.flush();
    const Status fs = FsyncPath(open_path_);
    if (!fs.ok()) return fs;
    fsynced_paths_.insert(open_path_);
  }
  return Status::Ok();
}

EbPackShardSet::EbPackShardSet(std::string packs_dir, uint64_t txn_id,
                               size_t spill_bytes, uint64_t spill_records)
    : shards_{{
          EbPackWriter(packs_dir, txn_id, 0),
          EbPackWriter(packs_dir, txn_id, 1),
          EbPackWriter(packs_dir, txn_id, 2),
          EbPackWriter(packs_dir, txn_id, 3),
          EbPackWriter(packs_dir, txn_id, 4),
          EbPackWriter(packs_dir, txn_id, 5),
          EbPackWriter(packs_dir, txn_id, 6),
          EbPackWriter(packs_dir, txn_id, 7),
          EbPackWriter(packs_dir, txn_id, 8),
          EbPackWriter(packs_dir, txn_id, 9),
          EbPackWriter(packs_dir, txn_id, 10),
          EbPackWriter(packs_dir, txn_id, 11),
          EbPackWriter(packs_dir, txn_id, 12),
          EbPackWriter(packs_dir, txn_id, 13),
          EbPackWriter(packs_dir, txn_id, 14),
          EbPackWriter(packs_dir, txn_id, 15),
      }} {
  for (auto& shard : shards_) {
    shard.SetSpillThresholds(spill_bytes, spill_records);
  }
}

Status EbPackShardSet::AppendRecord(const uint8_t hash[32],
                                    const uint8_t* payload, size_t stored_len,
                                    uint32_t uncompressed_len, ChunkCodec codec,
                                    EbPackRecordRef* out_ref) {
  const size_t shard = ShardForHash(hash);
  return shards_[shard].AppendRecord(hash, payload, stored_len, uncompressed_len,
                                     codec, out_ref);
}

Status EbPackShardSet::FlushAllOpenPacks(bool fsync_after) {
  for (auto& shard : shards_) {
    const Status st = shard.FlushOpenPack(fsync_after);
    if (!st.ok()) return st;
  }
  return Status::Ok();
}

Status EbPackShardSet::FsyncAll() {
  for (auto& shard : shards_) {
    const Status st = shard.FsyncAll();
    if (!st.ok()) return st;
  }
  return Status::Ok();
}

}  // namespace ebbackup
