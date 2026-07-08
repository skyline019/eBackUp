#include "ebsync/ebb_reader.h"

#include <cstring>
#include <fstream>

#include "ebbackup/common/crc32.h"

namespace ebsync {
namespace {

constexpr char kEbbMagicV1[4] = {'E', 'B', 'B', '1'};
constexpr char kEbbMagicV2[4] = {'E', 'B', 'B', '2'};
constexpr char kEbbFooterMagic[4] = {'E', 'B', 'B', 'F'};
constexpr uint32_t kEbbVersionV1 = 1;
constexpr uint32_t kEbbVersionV2 = 2;

}  // namespace

bool IsChunkTocPath(const std::string& rel, std::string* hex_out) {
  if (rel.rfind("chunks/", 0) != 0 || rel.size() != 7 + 64) return false;
  if (hex_out) *hex_out = rel.substr(7, 64);
  return true;
}

bool ReadEbbBundle(const std::string& path, EbbBundle* out, std::string* error) {
  if (!out) {
    if (error) *error = "null out";
    return false;
  }
  out->toc.clear();
  out->payloads.clear();
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    if (error) *error = "cannot open bundle";
    return false;
  }
  in.seekg(0, std::ios::end);
  const auto file_end = in.tellg();
  if (file_end < static_cast<std::streamoff>(12)) {
    if (error) *error = "bundle too short";
    return false;
  }
  in.seekg(static_cast<std::streamoff>(file_end - static_cast<std::streamoff>(8)),
           std::ios::beg);
  char footer_magic[4];
  in.read(footer_magic, 4);
  if (std::memcmp(footer_magic, kEbbFooterMagic, 4) != 0) {
    if (error) *error = "invalid footer";
    return false;
  }
  uint32_t stored_crc = 0;
  in.read(reinterpret_cast<char*>(&stored_crc), sizeof(stored_crc));
  const uint64_t crc_len = static_cast<uint64_t>(file_end) - 8;
  std::vector<uint8_t> file_bytes(crc_len);
  in.seekg(0, std::ios::beg);
  in.read(reinterpret_cast<char*>(file_bytes.data()),
          static_cast<std::streamsize>(crc_len));
  if (!in) {
    if (error) *error = "read failed";
    return false;
  }
  if (ebbackup::Crc32(file_bytes.data(), file_bytes.size()) != stored_crc) {
    if (error) *error = "crc mismatch";
    return false;
  }

  size_t pos = 0;
  auto read_bytes = [&](void* dst, size_t len) -> bool {
    if (pos + len > file_bytes.size()) return false;
    std::memcpy(dst, file_bytes.data() + pos, len);
    pos += len;
    return true;
  };

  EbbHeader& hdr = out->header;
  if (!read_bytes(hdr.magic, 4) || !read_bytes(&hdr.version, sizeof(hdr.version))) {
    if (error) *error = "truncated header";
    return false;
  }
  if (std::memcmp(hdr.magic, kEbbMagicV1, 4) != 0 &&
      std::memcmp(hdr.magic, kEbbMagicV2, 4) != 0) {
    if (error) *error = "bad magic";
    return false;
  }
  if (hdr.version != kEbbVersionV1 && hdr.version != kEbbVersionV2) {
    if (error) *error = "bad version";
    return false;
  }
  hdr.base_txn_id = 0;
  hdr.target_txn_id = 0;
  if (hdr.version >= kEbbVersionV2) {
    if (!read_bytes(&hdr.base_txn_id, sizeof(hdr.base_txn_id)) ||
        !read_bytes(&hdr.target_txn_id, sizeof(hdr.target_txn_id))) {
      if (error) *error = "truncated v2 header";
      return false;
    }
  }
  if (!read_bytes(&hdr.count, sizeof(hdr.count))) {
    if (error) *error = "truncated count";
    return false;
  }

  out->toc.reserve(hdr.count);
  for (uint32_t i = 0; i < hdr.count; ++i) {
    uint16_t path_len = 0;
    if (!read_bytes(&path_len, sizeof(path_len))) {
      if (error) *error = "truncated toc";
      return false;
    }
    EbbTocEntry entry{};
    entry.relative_path.resize(path_len);
    if (!read_bytes(entry.relative_path.data(), path_len) ||
        !read_bytes(&entry.offset, sizeof(entry.offset)) ||
        !read_bytes(&entry.size, sizeof(entry.size)) ||
        !read_bytes(&entry.crc32, sizeof(entry.crc32))) {
      if (error) *error = "truncated toc entry";
      return false;
    }
    out->toc.push_back(std::move(entry));
  }

  const uint64_t payload_base = pos;
  out->payloads.reserve(out->toc.size());
  for (const auto& entry : out->toc) {
    if (payload_base + entry.offset + entry.size > file_bytes.size()) {
      if (error) *error = "payload out of range";
      return false;
    }
    std::vector<uint8_t> member(entry.size);
    std::memcpy(member.data(), file_bytes.data() + payload_base + entry.offset,
                entry.size);
    if (ebbackup::Crc32(member.data(), member.size()) != entry.crc32) {
      if (error) *error = "member crc mismatch";
      return false;
    }
    out->payloads.push_back(std::move(member));
  }
  return true;
}

bool WriteEbbBundle(const std::string& path, const EbbBundle& bundle, std::string* error) {
  if (bundle.toc.size() != bundle.payloads.size()) {
    if (error) *error = "toc/payload mismatch";
    return false;
  }
  std::vector<uint8_t> file_bytes;
  const EbbHeader& hdr = bundle.header;
  file_bytes.insert(file_bytes.end(), hdr.magic, hdr.magic + 4);
  auto append_u32 = [&](uint32_t v) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
    file_bytes.insert(file_bytes.end(), p, p + 4);
  };
  auto append_u64 = [&](uint64_t v) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
    file_bytes.insert(file_bytes.end(), p, p + 8);
  };
  append_u32(hdr.version);
  if (hdr.version >= kEbbVersionV2) {
    append_u64(hdr.base_txn_id);
    append_u64(hdr.target_txn_id);
  }
  append_u32(hdr.count);

  for (size_t i = 0; i < bundle.toc.size(); ++i) {
    const auto& entry = bundle.toc[i];
    const uint16_t path_len = static_cast<uint16_t>(entry.relative_path.size());
    const uint8_t* plen = reinterpret_cast<const uint8_t*>(&path_len);
    file_bytes.insert(file_bytes.end(), plen, plen + 2);
    file_bytes.insert(file_bytes.end(), entry.relative_path.begin(),
                      entry.relative_path.end());
    append_u64(entry.offset);
    append_u64(entry.size);
    append_u32(entry.crc32);
  }

  const uint64_t payload_base = file_bytes.size();
  uint64_t max_end = payload_base;
  for (size_t i = 0; i < bundle.toc.size(); ++i) {
    const auto& entry = bundle.toc[i];
    const uint64_t end = payload_base + entry.offset + entry.size;
    if (end > max_end) max_end = end;
  }
  file_bytes.resize(static_cast<size_t>(max_end), 0);
  for (size_t i = 0; i < bundle.toc.size(); ++i) {
    const auto& entry = bundle.toc[i];
    const auto& payload = bundle.payloads[i];
    if (payload.size() != entry.size) {
      if (error) *error = "payload size mismatch";
      return false;
    }
    std::memcpy(file_bytes.data() + payload_base + entry.offset, payload.data(),
                payload.size());
  }

  const uint32_t crc = ebbackup::Crc32(file_bytes.data(), file_bytes.size());
  file_bytes.insert(file_bytes.end(), kEbbFooterMagic, kEbbFooterMagic + 4);
  const uint8_t* crc_p = reinterpret_cast<const uint8_t*>(&crc);
  file_bytes.insert(file_bytes.end(), crc_p, crc_p + 4);

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    if (error) *error = "cannot write bundle";
    return false;
  }
  out.write(reinterpret_cast<const char*>(file_bytes.data()),
            static_cast<std::streamsize>(file_bytes.size()));
  return static_cast<bool>(out);
}

}  // namespace ebsync
