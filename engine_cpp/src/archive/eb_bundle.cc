#include "ebbackup/archive/eb_bundle.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "ebbackup/common/crc32.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/fsync.h"
#include "ebbackup/crypto/aes_gcm.h"

namespace ebbackup {

namespace {

constexpr char kEbbMagic[4] = {'E', 'B', 'B', '1'};
constexpr char kEbbFooterMagic[4] = {'E', 'B', 'B', 'F'};
constexpr uint32_t kEbbVersion = 1;

struct EbbTocEntry {
  std::string relative_path;
  uint64_t offset{0};
  uint64_t size{0};
  uint32_t crc32{0};
};

Status ReadFileBytes(const std::string& path, std::vector<uint8_t>* out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::IoError("cannot open: " + path);
  in.seekg(0, std::ios::end);
  const auto len = in.tellg();
  if (len < 0) return Status::IoError("tell failed: " + path);
  in.seekg(0, std::ios::beg);
  out->resize(static_cast<size_t>(len));
  in.read(reinterpret_cast<char*>(out->data()), len);
  if (!in) return Status::IoError("read failed: " + path);
  return Status::Ok();
}

Status WriteAll(const std::string& path, const uint8_t* data, size_t len) {
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return Status::IoError("cannot write: " + path);
  out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(len));
  out.flush();
  if (!out) return Status::IoError("write failed: " + path);
  out.close();
  return FsyncPath(path);
}

void CollectRepoFiles(const std::string& repo_path,
                      std::vector<std::pair<std::string, std::string>>* out) {
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(repo_path)) {
    if (!entry.is_regular_file()) continue;
    const auto rel =
        std::filesystem::relative(entry.path(), repo_path).generic_string();
    out->push_back({rel, entry.path().string()});
  }
}

Status WriteBundleFile(const std::string& bundle_path,
                       const std::vector<EbbTocEntry>& toc,
                       const std::vector<std::vector<uint8_t>>& payloads) {
  std::vector<uint8_t> body;
  auto append_bytes = [&body](const void* data, size_t len) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    body.insert(body.end(), bytes, bytes + len);
  };

  append_bytes(kEbbMagic, 4);
  const uint32_t version = kEbbVersion;
  const uint32_t count = static_cast<uint32_t>(toc.size());
  append_bytes(&version, sizeof(version));
  append_bytes(&count, sizeof(count));

  for (const auto& entry : toc) {
    const uint16_t path_len = static_cast<uint16_t>(entry.relative_path.size());
    append_bytes(&path_len, sizeof(path_len));
    append_bytes(entry.relative_path.data(), entry.relative_path.size());
    append_bytes(&entry.offset, sizeof(entry.offset));
    append_bytes(&entry.size, sizeof(entry.size));
    append_bytes(&entry.crc32, sizeof(entry.crc32));
  }

  for (const auto& blob : payloads) {
    append_bytes(blob.data(), blob.size());
  }

  const uint32_t footer_crc = Crc32(body.data(), body.size());
  append_bytes(kEbbFooterMagic, 4);
  append_bytes(&footer_crc, sizeof(footer_crc));

  std::ofstream out(bundle_path, std::ios::binary | std::ios::trunc);
  if (!out) return Status::IoError("cannot create bundle: " + bundle_path);
  out.write(reinterpret_cast<const char*>(body.data()),
            static_cast<std::streamsize>(body.size()));
  out.flush();
  if (!out) return Status::IoError("bundle write failed");
  out.close();
  return FsyncPath(bundle_path);
}

Status ReadBundleFile(const std::string& bundle_path, std::vector<EbbTocEntry>* toc,
                      std::vector<std::vector<uint8_t>>* payloads) {
  std::ifstream in(bundle_path, std::ios::binary);
  if (!in) return Status::IoError("cannot open bundle: " + bundle_path);
  in.seekg(0, std::ios::end);
  const auto file_end = in.tellg();
  if (file_end < static_cast<std::streamoff>(12)) {
    return Status::Corrupt("bundle too short");
  }

  in.seekg(static_cast<std::streamoff>(file_end - static_cast<std::streamoff>(8)),
           std::ios::beg);
  char footer_magic[4];
  in.read(footer_magic, 4);
  if (std::memcmp(footer_magic, kEbbFooterMagic, 4) != 0) {
    return Status::Corrupt("invalid bundle footer");
  }
  uint32_t stored_crc = 0;
  in.read(reinterpret_cast<char*>(&stored_crc), sizeof(stored_crc));

  const uint64_t crc_len = static_cast<uint64_t>(file_end) - 8;
  std::vector<uint8_t> file_bytes(crc_len);
  in.seekg(0, std::ios::beg);
  in.read(reinterpret_cast<char*>(file_bytes.data()),
          static_cast<std::streamsize>(crc_len));
  if (!in) return Status::Corrupt("bundle read failed");
  if (Crc32(file_bytes.data(), file_bytes.size()) != stored_crc) {
    return Status::Corrupt("bundle crc mismatch");
  }

  size_t pos = 0;
  auto read_bytes = [&](void* dst, size_t len) -> Status {
    if (pos + len > file_bytes.size()) return Status::Corrupt("bundle truncated");
    std::memcpy(dst, file_bytes.data() + pos, len);
    pos += len;
    return Status::Ok();
  };

  char magic[4];
  Status st = read_bytes(magic, 4);
  if (!st.ok()) return st;
  if (std::memcmp(magic, kEbbMagic, 4) != 0) {
    return Status::Corrupt("invalid bundle magic");
  }
  uint32_t version = 0;
  uint32_t count = 0;
  st = read_bytes(&version, sizeof(version));
  if (!st.ok()) return st;
  st = read_bytes(&count, sizeof(count));
  if (!st.ok()) return st;
  if (version != kEbbVersion) {
    return Status::Corrupt("unsupported bundle version");
  }

  toc->clear();
  toc->reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    uint16_t path_len = 0;
    st = read_bytes(&path_len, sizeof(path_len));
    if (!st.ok()) return st;
    EbbTocEntry entry{};
    entry.relative_path.resize(path_len);
    st = read_bytes(entry.relative_path.data(), path_len);
    if (!st.ok()) return st;
    st = read_bytes(&entry.offset, sizeof(entry.offset));
    if (!st.ok()) return st;
    st = read_bytes(&entry.size, sizeof(entry.size));
    if (!st.ok()) return st;
    st = read_bytes(&entry.crc32, sizeof(entry.crc32));
    if (!st.ok()) return st;
    toc->push_back(std::move(entry));
  }

  const uint64_t payload_base = pos;
  payloads->clear();
  payloads->reserve(toc->size());
  for (const auto& entry : *toc) {
    if (payload_base + entry.offset + entry.size > file_bytes.size()) {
      return Status::Corrupt("bundle member out of range");
    }
    std::vector<uint8_t> member(entry.size);
    std::memcpy(member.data(), file_bytes.data() + payload_base + entry.offset,
                entry.size);
    if (Crc32(member.data(), member.size()) != entry.crc32) {
      return Status::Corrupt("bundle member crc mismatch: " + entry.relative_path);
    }
    payloads->push_back(std::move(member));
  }
  return Status::Ok();
}

}  // namespace

Status ExportRepoToBundle(const std::string& repo_path,
                          const std::string& bundle_path,
                          const EbBundleOptions& options) {
  if (!std::filesystem::exists(repo_path)) {
    return Status::NotFound("repo not found");
  }
  std::vector<std::pair<std::string, std::string>> files;
  CollectRepoFiles(repo_path, &files);
  std::sort(files.begin(), files.end());

  std::vector<EbbTocEntry> toc;
  std::vector<std::vector<uint8_t>> payloads;
  uint64_t offset = 0;
  for (const auto& [rel, abs] : files) {
    std::vector<uint8_t> bytes;
    const Status rd = ReadFileBytes(abs, &bytes);
    if (!rd.ok()) return rd;
    if (options.encrypt_bundle) {
#ifdef _WIN32
      if (options.password.empty()) {
        return Status::InvalidArgument("bundle encryption requires password");
      }
      uint8_t salt[16]{};
      uint8_t key[32]{};
      Pbkdf2Sha256(DigestAlgo::kLegacy,
                   reinterpret_cast<const uint8_t*>(options.password.data()),
                   options.password.size(), salt, 16, 100000, key);
      std::vector<uint8_t> encrypted;
      const Status enc_st =
          crypto::Aes256GcmEncrypt(key, bytes.data(), bytes.size(), &encrypted);
      if (!enc_st.ok()) return enc_st;
      bytes = std::move(encrypted);
#else
      return Status::Internal("bundle encryption requires Windows BCrypt");
#endif
    }
    EbbTocEntry entry{};
    entry.relative_path = rel;
    entry.offset = offset;
    entry.size = bytes.size();
    entry.crc32 = Crc32(bytes.data(), bytes.size());
    toc.push_back(entry);
    payloads.push_back(std::move(bytes));
    offset += entry.size;
  }
  return WriteBundleFile(bundle_path, toc, payloads);
}

Status ImportBundleToRepo(const std::string& bundle_path,
                          const std::string& repo_path) {
  std::vector<EbbTocEntry> toc;
  std::vector<std::vector<uint8_t>> payloads;
  const Status rd = ReadBundleFile(bundle_path, &toc, &payloads);
  if (!rd.ok()) return rd;
  if (toc.size() != payloads.size()) {
    return Status::Corrupt("bundle toc/payload mismatch");
  }
  std::error_code ec;
  std::filesystem::create_directories(repo_path, ec);
  if (ec) return Status::IoError("cannot create repo: " + ec.message());
  for (size_t i = 0; i < toc.size(); ++i) {
    const std::string out_path =
        (std::filesystem::path(repo_path) / toc[i].relative_path).string();
    const Status wr = WriteAll(out_path, payloads[i].data(), payloads[i].size());
    if (!wr.ok()) return wr;
  }
  return Status::Ok();
}

}  // namespace ebbackup
