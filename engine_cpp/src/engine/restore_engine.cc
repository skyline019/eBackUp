#include "ebbackup/engine/restore_engine.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "ebbackup/audit/merkle.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/fsync.h"
#include "ebbackup/common/path_util.h"
#include "ebbackup/crypto/aes_gcm.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/io/file_meta.h"
#include "ebbackup/scan/backup_filter.h"

namespace ebbackup {

namespace {

std::string RepoJoin(const std::string& repo, const std::string& name) {
  return (std::filesystem::path(repo) / name).string();
}

int FileTypeOrder(FileType type) {
  if (type == FileType::kDirectory) return 0;
  return 1;
}

}  // namespace

RestoreEngine::RestoreEngine(std::string repo_path, ChunkStore* chunk_store)
    : repo_path_(std::move(repo_path)), chunk_store_(chunk_store) {}

Status RestoreEngine::SetupEncryption(const RestoreOptions& options) {
  const std::string salt_path = RepoJoin(repo_path_, "crypto.salt");
  if (!std::filesystem::exists(salt_path)) return Status::Ok();
  if (options.encryption_password.empty()) {
    return Status::InvalidArgument("encrypted repo requires password");
  }
  uint8_t salt[16];
  std::ifstream in(salt_path, std::ios::binary);
  if (!in) return Status::IoError("cannot open salt");
  in.read(reinterpret_cast<char*>(salt), 16);
  if (!in) return Status::Corrupt("salt read short");
  uint8_t key[32];
  const Status key_st =
      crypto::DeriveContentKey(options.encryption_password, salt, key);
  if (!key_st.ok()) return key_st;
  chunk_store_->SetContentKey(key);
  return Status::Ok();
}

Status RestoreEngine::RestoreEntry(const std::filesystem::path& dest_root,
                                   const ManifestFileEntry& file) {
  const std::string rel = NormalizeRepoPath(file.relative_path);
  const std::filesystem::path out_path = dest_root / rel;
  std::error_code ec;

  if (file.file_type == FileType::kDirectory) {
    std::filesystem::create_directories(out_path, ec);
    if (ec) {
      return Status::IoError("cannot create directory: " + ec.message());
    }
    return ApplyFileMeta(out_path.string(), file);
  }

  std::filesystem::create_directories(out_path.parent_path(), ec);
  if (ec) {
    return Status::IoError("cannot create parent dir: " + ec.message());
  }

  if (file.file_type == FileType::kSymlink) {
    if (std::filesystem::exists(out_path, ec)) {
      std::filesystem::remove(out_path, ec);
    }
    std::filesystem::create_symlink(file.symlink_target, out_path, ec);
    if (ec) {
      return Status::IoError("symlink restore failed: " + ec.message());
    }
    return ApplyFileMeta(out_path.string(), file);
  }

#ifndef _WIN32
  if (file.file_type == FileType::kFifo || file.file_type == FileType::kBlock ||
      file.file_type == FileType::kChar) {
    if (std::filesystem::exists(out_path, ec)) {
      std::filesystem::remove(out_path, ec);
    }
    const mode_t mode =
        file.mode != 0 ? static_cast<mode_t>(file.mode)
                       : (file.file_type == FileType::kFifo ? S_IFIFO | 0644
                                                          : S_IFBLK | 0600);
    if (file.file_type == FileType::kFifo) {
      if (mkfifo(out_path.c_str(), mode) != 0) {
        return Status::IoError("mkfifo failed: " + out_path.string());
      }
    } else {
      const dev_t dev = makedev(file.device_major, file.device_minor);
      if (mknod(out_path.c_str(), mode, dev) != 0) {
        return Status::IoError("mknod failed: " + out_path.string());
      }
    }
    return ApplyFileMeta(out_path.string(), file);
  }
#else
  if (file.file_type == FileType::kFifo || file.file_type == FileType::kBlock ||
      file.file_type == FileType::kChar) {
    return Status::Ok();
  }
#endif

  std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return Status::IoError("cannot write restore file: " + out_path.string());
  }

  uint64_t total = 0;
  for (const auto& hex : file.chunk_hashes_hex) {
    uint8_t hash[32];
    if (!HexToBytes(hex, hash, 32)) {
      return Status::Corrupt("invalid chunk hash in manifest");
    }
    std::vector<uint8_t> payload;
    const Status st = chunk_store_->Get(hash, &payload);
    if (!st.ok()) return st;
    out.write(reinterpret_cast<const char*>(payload.data()),
              static_cast<std::streamsize>(payload.size()));
    if (!out) {
      return Status::IoError("restore write failed: " + out_path.string());
    }
    total += payload.size();
  }
  out.close();
  if (total != file.size) {
    return Status::Corrupt("restored size mismatch for " + rel);
  }
  const Status fs = FsyncPath(out_path.string());
  if (!fs.ok()) return fs;
  return ApplyFileMeta(out_path.string(), file);
}

Status RestoreEngine::RunRestore(const std::string& dest_path,
                                 const RestoreOptions& options) {
  if (!chunk_store_) return Status::InvalidArgument("chunk_store is null");
  const Status enc_st = SetupEncryption(options);
  if (!enc_st.ok()) return enc_st;

  ManifestDocument doc;
  const Status rd = ReadManifestAuto(RepoJoin(repo_path_, "manifest"), &doc);
  if (!rd.ok()) return rd;

  std::error_code ec;
  std::filesystem::create_directories(dest_path, ec);
  if (ec) return Status::IoError("cannot create dest: " + ec.message());

  std::vector<ManifestFileEntry> files = doc.files;
  if (options.filter.HasAnyFilter()) {
    std::vector<ManifestFileEntry> filtered;
    const Status filter_st = ApplyManifestFilter(options.filter, files, &filtered);
    if (!filter_st.ok()) return filter_st;
    files = std::move(filtered);
  }

  uint8_t subset_root[32]{};
  const bool do_content_verify =
      options.verify_restored_content ||
      (options.verify_subset_merkle && options.filter.HasAnyFilter());
  if (do_content_verify) {
    const Status merkle_st = audit::ComputeMerkleRootForFiles(files, subset_root);
    if (!merkle_st.ok()) return merkle_st;
    for (const auto& file : files) {
      for (const auto& hex : file.chunk_hashes_hex) {
        uint8_t hash[32];
        if (!HexToBytes(hex, hash, 32)) {
          return Status::Corrupt("invalid chunk hash in subset manifest");
        }
        std::vector<uint8_t> payload;
        const Status get_st = chunk_store_->Get(hash, &payload);
        if (!get_st.ok()) return get_st;
      }
    }
  }

  std::stable_sort(files.begin(), files.end(),
                   [](const ManifestFileEntry& a, const ManifestFileEntry& b) {
                     const int oa = FileTypeOrder(a.file_type);
                     const int ob = FileTypeOrder(b.file_type);
                     if (oa != ob) return oa < ob;
                     return a.relative_path < b.relative_path;
                   });

  const std::filesystem::path dest_root(dest_path);
  for (const auto& file : files) {
    const Status st = RestoreEntry(dest_root, file);
    if (!st.ok()) return st;
  }

  if (do_content_verify) {
    uint8_t actual_root[32]{};
    const Status merkle_st = audit::ComputeMerkleRootFromRestoredFiles(
        dest_path, files, chunk_store_, actual_root);
    if (!merkle_st.ok()) return merkle_st;
    if (std::memcmp(subset_root, actual_root, 32) != 0) {
      return Status::Corrupt("restored content merkle mismatch");
    }
  }
  return Status::Ok();
}

}  // namespace ebbackup
