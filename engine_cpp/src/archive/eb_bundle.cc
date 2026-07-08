#include "ebbackup/archive/eb_bundle.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <vector>

#include "ebbackup/common/crc32.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/fsync.h"
#include "ebbackup/common/path_util.h"
#include "ebbackup/crypto/aes_gcm.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/state/superblock.h"
#include "ebbackup/store/chunk_store.h"
#include "ebbackup/store/orphan_gc.h"
#include "ebbackup/store/snapshot_store.h"

namespace ebbackup {

namespace {

constexpr char kEbbMagicV1[4] = {'E', 'B', 'B', '1'};
constexpr char kEbbMagicV2[4] = {'E', 'B', 'B', '2'};
constexpr char kEbbFooterMagic[4] = {'E', 'B', 'B', 'F'};
constexpr uint32_t kEbbVersionV1 = 1;
constexpr uint32_t kEbbVersionV2 = 2;

struct EbbTocEntry {
  std::string relative_path;
  uint64_t offset{0};
  uint64_t size{0};
  uint32_t crc32{0};
};

struct EbbHeader {
  char magic[4]{};
  uint32_t version{0};
  uint64_t base_txn_id{0};
  uint64_t target_txn_id{0};
  uint32_t count{0};
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

bool IsEbPackFile(const std::string& rel) {
  return rel.size() >= 7 && rel.compare(rel.size() - 7, 7, ".ebpack") == 0;
}

void ConfigureStoreFromSuperBlock(ChunkStore* store, const BackupSuperBlock& sb) {
  if (!store) return;
  store->SetDigestAlgo(DigestAlgoFromSuperBlock(sb));
  if (RepoUsesPersistentIndex(sb)) {
    store->SetUsePersistentIndex(true);
  }
  if (RepoUsesEbPack(sb)) {
    store->SetUseEbPack(true);
  }
}

Status OpenRepoChunkStore(const std::string& repo_path, ChunkStore* store) {
  if (!store) return Status::InvalidArgument("store is null");
  BackupSuperBlockStore sb_store(
      (std::filesystem::path(repo_path) / "superblock.bin").string());
  BackupSuperBlock sb{};
  const Status sb_st = sb_store.Load(&sb);
  if (!sb_st.ok()) return sb_st;
  ConfigureStoreFromSuperBlock(store, sb);
  store->SetTxnId(sb.critical.txn_id);
  return store->Open();
}

Status MaybeEncryptPayload(const EbBundleOptions& options,
                           std::vector<uint8_t>* bytes) {
  if (!options.encrypt_bundle) return Status::Ok();
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
      crypto::Aes256GcmEncrypt(key, bytes->data(), bytes->size(), &encrypted);
  if (!enc_st.ok()) return enc_st;
  *bytes = std::move(encrypted);
  return Status::Ok();
#else
  (void)options;
  (void)bytes;
  return Status::Internal("bundle encryption requires Windows BCrypt");
#endif
}

Status WriteBundleFile(const std::string& bundle_path, const EbbHeader& hdr,
                       const std::vector<EbbTocEntry>& toc,
                       const std::vector<std::vector<uint8_t>>& payloads) {
  std::vector<uint8_t> body;
  auto append_bytes = [&body](const void* data, size_t len) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    body.insert(body.end(), bytes, bytes + len);
  };

  append_bytes(hdr.magic, 4);
  append_bytes(&hdr.version, sizeof(hdr.version));
  if (hdr.version >= kEbbVersionV2) {
    append_bytes(&hdr.base_txn_id, sizeof(hdr.base_txn_id));
    append_bytes(&hdr.target_txn_id, sizeof(hdr.target_txn_id));
  }
  const uint32_t count = static_cast<uint32_t>(toc.size());
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

Status ReadBundleFile(const std::string& bundle_path, EbbHeader* hdr,
                      std::vector<EbbTocEntry>* toc,
                      std::vector<std::vector<uint8_t>>* payloads) {
  if (!hdr || !toc || !payloads) return Status::InvalidArgument("null argument");
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

  Status st = read_bytes(hdr->magic, 4);
  if (!st.ok()) return st;
  st = read_bytes(&hdr->version, sizeof(hdr->version));
  if (!st.ok()) return st;

  if (std::memcmp(hdr->magic, kEbbMagicV1, 4) != 0 &&
      std::memcmp(hdr->magic, kEbbMagicV2, 4) != 0) {
    return Status::Corrupt("invalid bundle magic");
  }
  if (hdr->version != kEbbVersionV1 && hdr->version != kEbbVersionV2) {
    return Status::Corrupt("unsupported bundle version");
  }

  hdr->base_txn_id = 0;
  hdr->target_txn_id = 0;
  if (hdr->version >= kEbbVersionV2) {
    st = read_bytes(&hdr->base_txn_id, sizeof(hdr->base_txn_id));
    if (!st.ok()) return st;
    st = read_bytes(&hdr->target_txn_id, sizeof(hdr->target_txn_id));
    if (!st.ok()) return st;
  }

  st = read_bytes(&hdr->count, sizeof(hdr->count));
  if (!st.ok()) return st;

  toc->clear();
  toc->reserve(hdr->count);
  for (uint32_t i = 0; i < hdr->count; ++i) {
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

Status CopyRepoTree(const std::string& from, const std::string& to) {
  std::error_code ec;
  std::filesystem::create_directories(to, ec);
  if (ec) return Status::IoError("cannot create repo: " + ec.message());
  std::filesystem::copy(from, to,
                        std::filesystem::copy_options::recursive |
                            std::filesystem::copy_options::overwrite_existing,
                        ec);
  if (ec) return Status::IoError("repo copy failed: " + ec.message());
  return Status::Ok();
}

Status ApplyBundleEntries(const std::string& repo_path,
                          const std::vector<EbbTocEntry>& toc,
                          const std::vector<std::vector<uint8_t>>& payloads) {
  if (toc.size() != payloads.size()) {
    return Status::Corrupt("bundle toc/payload mismatch");
  }

  const std::string chunks_path =
      (std::filesystem::path(repo_path) / "data" / "chunks").string();
  ChunkStore store(chunks_path);
  const Status open_st = OpenRepoChunkStore(repo_path, &store);
  if (!open_st.ok()) return open_st;

  bool has_chunk_entries = false;
  for (const auto& entry : toc) {
    const std::string& rel = entry.relative_path;
    if (rel.rfind("chunks/", 0) == 0 && rel.size() == 7 + 64) {
      has_chunk_entries = true;
      break;
    }
  }
  if (has_chunk_entries) {
    const Status sess_st = store.BeginAppendSession();
    if (!sess_st.ok()) return sess_st;
  }

  for (size_t i = 0; i < toc.size(); ++i) {
    const std::string& rel = toc[i].relative_path;
    if (rel.rfind("chunks/", 0) == 0 && rel.size() == 7 + 64) {
      const std::string hex = rel.substr(7, 64);
      uint8_t hash[32];
      if (!HexToBytes(hex, hash, 32)) {
        return Status::Corrupt("invalid chunk path in bundle: " + rel);
      }
      const auto& blob = payloads[i];
      Status put_st;
      if (blob.size() >= sizeof(ChunkRecordHeader)) {
        ChunkRecordHeader hdr{};
        std::memcpy(&hdr, blob.data(), sizeof(hdr));
        if (std::memcmp(hdr.hash, hash, 32) == 0) {
          const uint8_t* payload = blob.data() + sizeof(hdr);
          const size_t stored_len = blob.size() - sizeof(hdr);
          put_st = store.PutPrecompressed(
              hash, payload, stored_len, hdr.uncompressed_len,
              static_cast<ChunkCodec>(hdr.codec), nullptr, true);
        } else {
          put_st = store.PutKnownHash(blob.data(), blob.size(), hash, nullptr);
        }
      } else {
        put_st = store.PutKnownHash(blob.data(), blob.size(), hash, nullptr);
      }
      if (!put_st.ok()) return put_st;
      continue;
    }
    const std::string out_path =
        (std::filesystem::path(repo_path) / rel).string();
    const Status wr = WriteAll(out_path, payloads[i].data(), payloads[i].size());
    if (!wr.ok()) return wr;
  }
  return store.Flush();
}

Status ResolveTargetTxn(const std::string& repo_path, uint64_t requested,
                        uint64_t* out) {
  if (!out) return Status::InvalidArgument("out is null");
  if (requested != 0) {
    *out = requested;
    return Status::Ok();
  }
  ManifestDocument doc;
  const Status rd =
      ReadManifestAuto((std::filesystem::path(repo_path) / "manifest").string(),
                       &doc);
  if (!rd.ok()) return rd;
  *out = doc.txn_id;
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
    const Status enc_st = MaybeEncryptPayload(options, &bytes);
    if (!enc_st.ok()) return enc_st;
    EbbTocEntry entry{};
    entry.relative_path = rel;
    entry.offset = offset;
    entry.size = bytes.size();
    entry.crc32 = Crc32(bytes.data(), bytes.size());
    toc.push_back(entry);
    payloads.push_back(std::move(bytes));
    offset += entry.size;
  }

  EbbHeader hdr{};
  std::memcpy(hdr.magic, kEbbMagicV1, 4);
  hdr.version = kEbbVersionV1;
  hdr.count = static_cast<uint32_t>(toc.size());
  return WriteBundleFile(bundle_path, hdr, toc, payloads);
}

Status ExportRepoDeltaToBundle(const std::string& repo_path,
                               const std::string& bundle_path,
                               const EbBundleDeltaOptions& options,
                               EbBundleDeltaStats* stats) {
  if (!std::filesystem::exists(repo_path)) {
    return Status::NotFound("repo not found");
  }
  if (options.base_txn_id == 0) {
    return Status::InvalidArgument("base_txn_id required for delta export");
  }

  uint64_t target_txn = 0;
  const Status txn_st = ResolveTargetTxn(repo_path, options.target_txn_id,
                                         &target_txn);
  if (!txn_st.ok()) return txn_st;
  if (target_txn <= options.base_txn_id) {
    return Status::InvalidArgument("target txn must be after base txn");
  }

  ManifestDocument base_doc;
  ManifestDocument target_doc;
  const Status base_rd =
      LoadSnapshotManifest(repo_path, options.base_txn_id, &base_doc);
  if (!base_rd.ok()) return base_rd;
  const Status target_rd = LoadSnapshotManifest(repo_path, target_txn, &target_doc);
  if (!target_rd.ok()) return target_rd;

  std::unordered_set<std::string> base_hashes;
  std::unordered_set<std::string> target_hashes;
  const Status coll_a = CollectReferencedHashes(base_doc, &base_hashes);
  if (!coll_a.ok()) return coll_a;
  const Status coll_b = CollectReferencedHashes(target_doc, &target_hashes);
  if (!coll_b.ok()) return coll_b;

  std::vector<std::string> delta_hashes;
  delta_hashes.reserve(target_hashes.size());
  for (const auto& h : target_hashes) {
    if (base_hashes.find(h) == base_hashes.end()) {
      delta_hashes.push_back(h);
    }
  }
  std::sort(delta_hashes.begin(), delta_hashes.end());

  const std::string chunks_path =
      (std::filesystem::path(repo_path) / "data" / "chunks").string();
  ChunkStore store(chunks_path);
  const Status open_st = OpenRepoChunkStore(repo_path, &store);
  if (!open_st.ok()) return open_st;

  EbBundleOptions enc_opts{};
  enc_opts.encrypt_bundle = options.encrypt_bundle;
  enc_opts.password = options.password;

  std::vector<EbbTocEntry> toc;
  std::vector<std::vector<uint8_t>> payloads;
  uint64_t offset = 0;
  uint64_t delta_bytes = 0;

  for (const auto& key : delta_hashes) {
    uint8_t hash[32];
    std::memcpy(hash, key.data(), 32);
    ChunkStore::ParsedHeader parsed{};
    std::vector<uint8_t> stored;
    const Status get_st = store.ReadRecordForHash(hash, &parsed, &stored);
    if (!get_st.ok()) return get_st;
    delta_bytes += stored.size();
    const std::string hex = BytesToHex(hash, 32);
    std::vector<uint8_t> bytes(sizeof(ChunkRecordHeader) + stored.size());
    std::memcpy(bytes.data(), &parsed.header, sizeof(parsed.header));
    if (!stored.empty()) {
      std::memcpy(bytes.data() + sizeof(ChunkRecordHeader), stored.data(),
                  stored.size());
    }
    const Status enc_st = MaybeEncryptPayload(enc_opts, &bytes);
    if (!enc_st.ok()) return enc_st;
    EbbTocEntry entry{};
    entry.relative_path = "chunks/" + hex;
    entry.offset = offset;
    entry.size = bytes.size();
    entry.crc32 = Crc32(bytes.data(), bytes.size());
    toc.push_back(entry);
    payloads.push_back(std::move(bytes));
    offset += entry.size;
  }

  std::vector<std::pair<std::string, std::string>> files;
  CollectRepoFiles(repo_path, &files);
  std::sort(files.begin(), files.end());
  for (const auto& [rel, abs] : files) {
    if (IsEbPackFile(rel)) continue;
    if (rel.rfind("chunks/", 0) == 0) continue;
    std::vector<uint8_t> bytes;
    const Status rd = ReadFileBytes(abs, &bytes);
    if (!rd.ok()) return rd;
    const Status enc_st = MaybeEncryptPayload(enc_opts, &bytes);
    if (!enc_st.ok()) return enc_st;
    EbbTocEntry entry{};
    entry.relative_path = rel;
    entry.offset = offset;
    entry.size = bytes.size();
    entry.crc32 = Crc32(bytes.data(), bytes.size());
    toc.push_back(entry);
    payloads.push_back(std::move(bytes));
    offset += entry.size;
  }

  EbbHeader hdr{};
  std::memcpy(hdr.magic, kEbbMagicV2, 4);
  hdr.version = kEbbVersionV2;
  hdr.base_txn_id = options.base_txn_id;
  hdr.target_txn_id = target_txn;
  hdr.count = static_cast<uint32_t>(toc.size());

  if (stats) {
    stats->base_txn_id = options.base_txn_id;
    stats->target_txn_id = target_txn;
    stats->delta_chunk_count = delta_hashes.size();
    stats->delta_bytes = delta_bytes;
    if (!target_hashes.empty()) {
      stats->reuse_ratio =
          100.0 * static_cast<double>(base_hashes.size()) /
          static_cast<double>(target_hashes.size());
    }
  }

  return WriteBundleFile(bundle_path, hdr, toc, payloads);
}

Status ImportBundleToRepo(const std::string& bundle_path,
                          const std::string& repo_path) {
  EbbHeader hdr{};
  std::vector<EbbTocEntry> toc;
  std::vector<std::vector<uint8_t>> payloads;
  const Status rd = ReadBundleFile(bundle_path, &hdr, &toc, &payloads);
  if (!rd.ok()) return rd;
  std::error_code ec;
  std::filesystem::create_directories(repo_path, ec);
  if (ec) return Status::IoError("cannot create repo: " + ec.message());
  return ApplyBundleEntries(repo_path, toc, payloads);
}

Status ApplyDeltaBundleToRepo(const std::string& delta_bundle_path,
                              const std::string& repo_path) {
  EbbHeader hdr{};
  std::vector<EbbTocEntry> toc;
  std::vector<std::vector<uint8_t>> payloads;
  const Status rd = ReadBundleFile(delta_bundle_path, &hdr, &toc, &payloads);
  if (!rd.ok()) return rd;
  if (hdr.version < kEbbVersionV2) {
    return Status::InvalidArgument("not a delta bundle (EBB v2 required)");
  }
  if (!std::filesystem::exists(repo_path)) {
    return Status::NotFound("repo not found");
  }
  return ApplyBundleEntries(repo_path, toc, payloads);
}

Status ImportBundleDeltaToRepo(const std::string& base_repo_or_bundle,
                               const std::string& delta_bundle_path,
                               const std::string& out_repo_path) {
  EbbHeader delta_hdr{};
  std::vector<EbbTocEntry> delta_toc;
  std::vector<std::vector<uint8_t>> delta_payloads;
  const Status delta_rd =
      ReadBundleFile(delta_bundle_path, &delta_hdr, &delta_toc, &delta_payloads);
  if (!delta_rd.ok()) return delta_rd;
  if (delta_hdr.version < kEbbVersionV2) {
    return Status::InvalidArgument("delta bundle must be EBB v2");
  }

  const bool base_is_bundle =
      base_repo_or_bundle.size() >= 4 &&
      base_repo_or_bundle.compare(base_repo_or_bundle.size() - 4, 4, ".ebb") == 0;

  if (base_is_bundle) {
    const Status imp_st = ImportBundleToRepo(base_repo_or_bundle, out_repo_path);
    if (!imp_st.ok()) return imp_st;
  } else {
    if (!std::filesystem::exists(base_repo_or_bundle)) {
      return Status::NotFound("base repo not found");
    }
    const Status copy_st = CopyRepoTree(base_repo_or_bundle, out_repo_path);
    if (!copy_st.ok()) return copy_st;
  }

  return ApplyBundleEntries(out_repo_path, delta_toc, delta_payloads);
}

}  // namespace ebbackup
