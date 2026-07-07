#include "ebbackup/store/chunk_index.h"

#include <cstring>
#include <filesystem>
#include <fstream>

#include "ebbackup/common/crc32.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/fsync.h"
#include "ebbackup/store/chunk_store.h"

namespace ebbackup {

namespace {

uint32_t HeaderCrc(const ChunkIndexHeader& hdr) {
  ChunkIndexHeader tmp = hdr;
  tmp.header_crc32 = 0;
  return Crc32(&tmp, sizeof(tmp));
}

uint32_t RecordHeaderCrcV1(const ChunkRecordHeaderV1& hdr) {
  ChunkRecordHeaderV1 tmp = hdr;
  tmp.record_crc32 = 0;
  return Crc32(&tmp, sizeof(tmp));
}

uint32_t RecordHeaderCrcV2(const ChunkRecordHeader& hdr) {
  ChunkRecordHeader tmp = hdr;
  tmp.record_crc32 = 0;
  return Crc32(&tmp, sizeof(tmp));
}

Status ReadRecordMetaAt(std::ifstream& in, uint64_t offset,
                        ChunkIndexEntry* entry, size_t* record_size_out) {
  in.seekg(static_cast<std::streamoff>(offset));
  ChunkRecordHeaderV1 v1{};
  in.read(reinterpret_cast<char*>(&v1), sizeof(v1));
  if (!in) return Status::IoError("chunk header read short");

  const uint32_t stored_crc = v1.record_crc32;
  v1.record_crc32 = 0;
  if (RecordHeaderCrcV1(v1) == stored_crc) {
    std::memcpy(entry->hash, v1.hash, 32);
    entry->offset = offset;
    entry->stored_len = v1.raw_len;
    entry->uncompressed_len = v1.raw_len;
    entry->codec = static_cast<uint8_t>(ChunkCodec::kRaw);
    *record_size_out = kChunkHeaderV1Size + v1.raw_len;
    return Status::Ok();
  }

  in.seekg(static_cast<std::streamoff>(offset));
  ChunkRecordHeader v2{};
  in.read(reinterpret_cast<char*>(&v2), sizeof(v2));
  if (!in) return Status::IoError("chunk header read short");
  const uint32_t v2_crc = v2.record_crc32;
  v2.record_crc32 = 0;
  if (RecordHeaderCrcV2(v2) != v2_crc) {
    return Status::Corrupt("chunk record header crc mismatch");
  }
  std::memcpy(entry->hash, v2.hash, 32);
  entry->offset = offset;
  entry->stored_len = v2.stored_len;
  entry->uncompressed_len = v2.uncompressed_len;
  entry->codec = v2.codec;
  *record_size_out = kChunkHeaderV2Size + v2.stored_len;
  return Status::Ok();
}

}  // namespace

std::string ChunkIndexFile::PathForStore(const std::string& chunks_path) {
  return (std::filesystem::path(chunks_path).parent_path() / "chunk.idx")
      .string();
}

Status ChunkIndexFile::Load(const std::string& path, uint64_t expected_chunks_size,
                            std::vector<ChunkIndexEntry>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::NotFound("chunk index missing: " + path);

  ChunkIndexHeader hdr{};
  in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
  if (!in) return Status::Corrupt("chunk index header short");
  if (hdr.magic != kChunkIndexMagic) {
    return Status::Corrupt("chunk index bad magic/version");
  }
  if (hdr.version != kChunkIndexVersion &&
      hdr.version != kChunkIndexVersionEbPack) {
    return Status::Corrupt("chunk index bad magic/version");
  }
  const uint32_t stored_crc = hdr.header_crc32;
  if (HeaderCrc(hdr) != stored_crc) {
    return Status::Corrupt("chunk index header crc mismatch");
  }
  if (hdr.chunks_file_size != expected_chunks_size) {
    return Status::Corrupt("chunk index size mismatch with chunks file");
  }

  out->resize(static_cast<size_t>(hdr.entry_count));
  if (hdr.entry_count > 0) {
    if (hdr.version == kChunkIndexVersion) {
      std::vector<ChunkIndexEntryV1> v1(static_cast<size_t>(hdr.entry_count));
      in.read(reinterpret_cast<char*>(v1.data()),
              static_cast<std::streamsize>(v1.size() * sizeof(ChunkIndexEntryV1)));
      if (!in) return Status::Corrupt("chunk index entries short");
      for (size_t i = 0; i < v1.size(); ++i) {
        ChunkIndexEntry entry{};
        std::memcpy(entry.hash, v1[i].hash, 32);
        entry.offset = v1[i].offset;
        entry.stored_len = v1[i].stored_len;
        entry.uncompressed_len = v1[i].uncompressed_len;
        entry.codec = v1[i].codec;
        (*out)[i] = entry;
      }
    } else {
      in.read(reinterpret_cast<char*>(out->data()),
              static_cast<std::streamsize>(hdr.entry_count * sizeof(ChunkIndexEntry)));
      if (!in) return Status::Corrupt("chunk index entries short");
    }
  }
  return Status::Ok();
}

Status ChunkIndexFile::Save(const std::string& path, uint64_t chunks_file_size,
                            const std::vector<ChunkIndexEntry>& entries) const {
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  const std::string temp = path + ".new";
  std::ofstream out(temp, std::ios::binary | std::ios::trunc);
  if (!out) return Status::IoError("cannot write chunk index: " + temp);

  ChunkIndexHeader hdr{};
  hdr.entry_count = entries.size();
  hdr.chunks_file_size = chunks_file_size;
  bool use_v2 = false;
  for (const auto& entry : entries) {
    if (entry.storage_flags == kChunkStorageEbPack) {
      use_v2 = true;
      break;
    }
  }
  hdr.version = use_v2 ? kChunkIndexVersionEbPack : kChunkIndexVersion;
  hdr.header_crc32 = HeaderCrc(hdr);
  out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
  if (!entries.empty()) {
    if (use_v2) {
      out.write(reinterpret_cast<const char*>(entries.data()),
                static_cast<std::streamsize>(entries.size() * sizeof(ChunkIndexEntry)));
    } else {
      std::vector<ChunkIndexEntryV1> v1(entries.size());
      for (size_t i = 0; i < entries.size(); ++i) {
        std::memcpy(v1[i].hash, entries[i].hash, 32);
        v1[i].offset = entries[i].offset;
        v1[i].stored_len = entries[i].stored_len;
        v1[i].uncompressed_len = entries[i].uncompressed_len;
        v1[i].codec = entries[i].codec;
      }
      out.write(reinterpret_cast<const char*>(v1.data()),
                static_cast<std::streamsize>(v1.size() * sizeof(ChunkIndexEntryV1)));
    }
  }
  out.flush();
  if (!out) return Status::IoError("chunk index write failed");
  out.close();
  const Status fs = FsyncPath(temp);
  if (!fs.ok()) return fs;
  std::error_code ec;
  std::filesystem::rename(temp, path, ec);
  if (ec) return Status::IoError("chunk index rename failed");
  return FsyncPath(path);
}

Status ChunkIndexFile::RebuildFromScan(
    const std::string& chunks_path, std::vector<ChunkIndexEntry>* out,
    uint64_t* chunks_file_size_out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  if (!std::filesystem::exists(chunks_path)) {
    if (chunks_file_size_out) *chunks_file_size_out = 0;
    return Status::Ok();
  }

  std::ifstream in(chunks_path, std::ios::binary);
  if (!in) return Status::IoError("cannot open chunks: " + chunks_path);

  const uint64_t file_total = std::filesystem::file_size(chunks_path);
  uint64_t valid_size = file_total;
  if (chunks_file_size_out) *chunks_file_size_out = valid_size;

  uint64_t offset = 0;
  while (offset < valid_size) {
    ChunkIndexEntry entry{};
    size_t record_size = 0;
    const Status st = ReadRecordMetaAt(in, offset, &entry, &record_size);
    if (!st.ok()) {
      if (offset > 0 && offset < valid_size) {
        std::error_code ec;
        std::filesystem::resize_file(chunks_path,
                                     static_cast<std::uintmax_t>(offset), ec);
        if (ec) {
          return Status::IoError("cannot truncate corrupt chunk tail");
        }
        valid_size = offset;
        if (chunks_file_size_out) *chunks_file_size_out = valid_size;
        break;
      }
      return st;
    }
    out->push_back(entry);
    offset += record_size;
  }
  return Status::Ok();
}

}  // namespace ebbackup
