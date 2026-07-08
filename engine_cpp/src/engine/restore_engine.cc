#include "ebbackup/engine/restore_engine.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <unordered_map>

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "ebbackup/audit/merkle.h"
#include "ebbackup/catalog/restore_acceptance.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/fsync.h"
#include "ebbackup/common/path_encoding.h"
#include "ebbackup/common/path_util.h"
#include "ebbackup/crypto/aes_gcm.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/engine/restore_plan.h"
#include "ebbackup/engine/restore_path_remap.h"
#include "ebbackup/io/file_meta.h"
#include "ebbackup/scan/backup_filter.h"
#include "ebbackup/store/snapshot_store.h"
#include "ebbackup/winmeta/win_meta.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace ebbackup {

namespace {

#ifdef _WIN32
Status WritePathBytesWin32(const std::string& path, const uint8_t* data, size_t len) {
  const std::wstring wide = Utf8ToWide(path);
  HANDLE h = CreateFileW(wide.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return Status::IoError("CreateFile for restore write failed: " + path);
  }
  DWORD written = 0;
  const BOOL ok =
      len == 0 || WriteFile(h, data, static_cast<DWORD>(len), &written, nullptr);
  CloseHandle(h);
  if (!ok || written != len) {
    return Status::IoError("restore write failed: " + path);
  }
  return Status::Ok();
}

Status AppendPathBytesWin32(HANDLE handle, const uint8_t* data, size_t len) {
  if (handle == INVALID_HANDLE_VALUE) {
    return Status::InvalidArgument("invalid restore write handle");
  }
  if (len == 0) return Status::Ok();
  DWORD written = 0;
  const BOOL ok =
      WriteFile(handle, data, static_cast<DWORD>(len), &written, nullptr);
  if (!ok || written != len) {
    return Status::IoError("restore append write failed");
  }
  return Status::Ok();
}

Status OpenPathForRestoreWriteWin32(const std::string& path, HANDLE* out_handle) {
  if (!out_handle) return Status::InvalidArgument("out_handle is null");
  const std::wstring wide = Utf8ToWide(path);
  HANDLE h = CreateFileW(wide.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return Status::IoError("CreateFile for restore write failed: " + path);
  }
  *out_handle = h;
  return Status::Ok();
}
#endif

std::string RepoJoin(const std::string& repo, const std::string& name) {
  return PathToUtf8(PathFromUtf8(repo) / PathFromUtf8(name));
}

int FileTypeOrder(const ManifestFileEntry& file) {
  if (file.file_type == FileType::kDirectory) {
#ifdef _WIN32
    if (file.reparse_tag != 0) return 2;
#endif
    return 0;
  }
  return 1;
}

Status ApplyWinMetaAfterRestore(const std::string& path,
                                const ManifestFileEntry& file,
                                const winmeta::AclRestorePolicy& policy,
                                std::string* soft_issue_reason) {
  const Status meta = ApplyFileMeta(path, file);
  if (!meta.ok()) return meta;
  return winmeta::ApplyWinMetaOnRestore(path, file, policy, soft_issue_reason);
}

void RecordRestoreIssue(const std::string& path, const std::string& reason,
                        std::vector<report::BackupPathIssue>* issues) {
  if (!issues || reason.empty()) return;
  issues->push_back({path, reason});
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
  std::ifstream in(PathFromUtf8(salt_path), std::ios::binary);
  if (!in) return Status::IoError("cannot open salt");
  in.read(reinterpret_cast<char*>(salt), 16);
  if (!in) return Status::Corrupt("salt read short");
  uint8_t key[32];
  const Status key_st = crypto::DeriveContentKey(
      options.encryption_password, salt, key, chunk_store_->digest_algo());
  if (!key_st.ok()) return key_st;
  chunk_store_->SetContentKey(key);
  return Status::Ok();
}

Status RestoreEngine::RestorePlannedEntry(
    const std::filesystem::path& dest_root, const ManifestFileEntry& file,
    const std::string& dest_rel, const RestoreOptions& options,
    std::unordered_map<RestoreInodeKey, std::string, RestoreInodeKeyHash>*
        inode_canonical,
    std::vector<report::BackupPathIssue>* restore_issues) {
  return RestoreEntry(dest_root, file, dest_rel, options, inode_canonical,
                      restore_issues);
}

Status RestoreEngine::RestoreEntry(
    const std::filesystem::path& dest_root, const ManifestFileEntry& file,
    const std::string& dest_rel, const RestoreOptions& options,
    std::unordered_map<RestoreInodeKey, std::string, RestoreInodeKeyHash>* inode_canonical,
    std::vector<report::BackupPathIssue>* restore_issues) {
  std::string base_rel = dest_rel;
  std::string stream_name = file.stream_name;
#ifdef _WIN32
  const size_t colon = base_rel.find(':');
  if (colon != std::string::npos && colon > 0) {
    if (stream_name.empty()) {
      stream_name = base_rel.substr(colon + 1);
    }
    base_rel = base_rel.substr(0, colon);
  }
#endif
  const std::filesystem::path out_path = dest_root / PathFromUtf8(base_rel);
  std::string write_path = PathToUtf8(out_path);
#ifdef _WIN32
  if (!stream_name.empty()) {
    write_path += ":" + stream_name;
  }
#endif
  std::error_code ec;

#ifdef _WIN32
  if (file.file_type == FileType::kDirectory && file.reparse_tag != 0) {
    if (options.reparse_policy.mode == winmeta::ReparseRestorePolicy::Mode::kRecreate) {
      const Status rp_st = winmeta::RecreateReparsePoint(write_path, file);
      if (!rp_st.ok()) return rp_st;
    }
    std::string soft_issue;
    const Status meta_st =
        ApplyWinMetaAfterRestore(write_path, file, options.acl_policy, &soft_issue);
    if (!meta_st.ok()) return meta_st;
    RecordRestoreIssue(write_path, soft_issue, restore_issues);
    return Status::Ok();
  }
#endif

  if (file.file_type == FileType::kDirectory) {
    std::filesystem::create_directories(out_path, ec);
    if (ec) {
      return Status::IoError("cannot create directory: " + ec.message());
    }
    std::string soft_issue;
    const Status meta_st =
        ApplyWinMetaAfterRestore(write_path, file, options.acl_policy, &soft_issue);
    if (!meta_st.ok()) return meta_st;
    RecordRestoreIssue(write_path, soft_issue, restore_issues);
    return Status::Ok();
  }

  std::filesystem::create_directories(out_path.parent_path(), ec);
  if (ec) {
    return Status::IoError("cannot create parent dir: " + ec.message());
  }

  if (file.file_type == FileType::kSymlink) {
    if (std::filesystem::exists(out_path, ec)) {
      std::filesystem::remove(out_path, ec);
    }
    const std::string link_target =
        ApplySymlinkTargetRemap(file.symlink_target, options.symlink_remap);
    std::filesystem::create_symlink(PathFromUtf8(link_target), out_path, ec);
    if (ec) {
      return Status::IoError("symlink restore failed: " + ec.message());
    }
    std::string soft_issue;
    const Status meta_st =
        ApplyWinMetaAfterRestore(PathToUtf8(out_path), file, options.acl_policy,
                                 &soft_issue);
    if (!meta_st.ok()) return meta_st;
    RecordRestoreIssue(PathToUtf8(out_path), soft_issue, restore_issues);
    return Status::Ok();
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
    std::string soft_issue;
    const Status meta_st =
        ApplyWinMetaAfterRestore(PathToUtf8(out_path), file, options.acl_policy,
                                 &soft_issue);
    if (!meta_st.ok()) return meta_st;
    RecordRestoreIssue(PathToUtf8(out_path), soft_issue, restore_issues);
    return Status::Ok();
  }
#else
  if (file.file_type == FileType::kFifo || file.file_type == FileType::kBlock ||
      file.file_type == FileType::kChar) {
    return Status::Ok();
  }
#endif

#ifdef _WIN32
  if (file.file_type == FileType::kRegular && file.inode_id != 0 && inode_canonical) {
    const RestoreInodeKey key{file.inode_id, file.stream_name};
    const auto found = inode_canonical->find(key);
    if (found != inode_canonical->end()) {
      if (std::filesystem::exists(out_path, ec)) {
        std::filesystem::remove(out_path, ec);
      }
      const Status hl_st = winmeta::CreateHardLinkUtf8(found->second, write_path);
      if (!hl_st.ok()) return hl_st;
      std::string soft_issue;
      const Status meta_st =
          ApplyWinMetaAfterRestore(write_path, file, options.acl_policy, &soft_issue);
      if (!meta_st.ok()) return meta_st;
      RecordRestoreIssue(write_path, soft_issue, restore_issues);
      return Status::Ok();
    }
  }
#endif

  std::vector<uint8_t> payload;
  size_t bytes_written = 0;

#ifdef _WIN32
  const bool win_stream_path =
      !file.stream_name.empty() || write_path.find(':') != std::string::npos;
  HANDLE win_handle = INVALID_HANDLE_VALUE;
  if (win_stream_path) {
    const Status open_st = OpenPathForRestoreWriteWin32(write_path, &win_handle);
    if (!open_st.ok()) return open_st;
  }
#endif

  std::unique_ptr<std::ofstream> out_file;
#ifndef _WIN32
  {
    out_file = std::make_unique<std::ofstream>(
        PathFromUtf8(write_path), std::ios::binary | std::ios::trunc);
    if (!*out_file) {
      return Status::IoError("cannot write restore file: " + write_path);
    }
  }
#else
  if (!win_stream_path) {
    out_file = std::make_unique<std::ofstream>(
        PathFromUtf8(write_path), std::ios::binary | std::ios::trunc);
    if (!*out_file) {
      return Status::IoError("cannot write restore file: " + write_path);
    }
  }
#endif

  for (const auto& hex : file.chunk_hashes_hex) {
    uint8_t hash[32];
    if (!HexToBytes(hex, hash, 32)) {
      return Status::Corrupt("invalid chunk hash in manifest");
    }
    payload.clear();
    const Status st = chunk_store_->Get(hash, &payload);
    if (!st.ok()) return st;
#ifdef _WIN32
    if (win_stream_path) {
      const Status wr = AppendPathBytesWin32(win_handle, payload.data(), payload.size());
      if (!wr.ok()) {
        CloseHandle(win_handle);
        return wr;
      }
    } else
#endif
    {
      out_file->write(reinterpret_cast<const char*>(payload.data()),
                      static_cast<std::streamsize>(payload.size()));
      if (!*out_file) {
        return Status::IoError("restore write failed: " + write_path);
      }
    }
    bytes_written += payload.size();
  }

#ifdef _WIN32
  if (win_stream_path) {
    CloseHandle(win_handle);
  } else
#endif
  {
    out_file->close();
  }

  if (bytes_written != file.size) {
    return Status::Corrupt("restored size mismatch for " + dest_rel);
  }

  const Status fs = FsyncPath(write_path);
  if (!fs.ok()) return fs;

#ifdef _WIN32
  if (file.file_type == FileType::kRegular && file.inode_id != 0 && inode_canonical) {
    (*inode_canonical)[RestoreInodeKey{file.inode_id, file.stream_name}] = write_path;
  }
#endif

  std::string soft_issue;
  const Status meta_st =
      ApplyWinMetaAfterRestore(write_path, file, options.acl_policy, &soft_issue);
  if (!meta_st.ok()) return meta_st;
  RecordRestoreIssue(write_path, soft_issue, restore_issues);
  return Status::Ok();
}

Status RestoreEngine::RunRestore(const std::string& dest_path,
                                 const RestoreOptions& options) {
  if (!chunk_store_) return Status::InvalidArgument("chunk_store is null");
  const Status enc_st = SetupEncryption(options);
  if (!enc_st.ok()) return enc_st;

  ManifestDocument doc;
  Status rd;
  if (options.snapshot_txn_id != 0) {
    rd = LoadSnapshotManifest(repo_path_, options.snapshot_txn_id, &doc);
  } else {
    rd = ReadManifestAuto(RepoJoin(repo_path_, "manifest"), &doc);
  }
  if (!rd.ok()) return rd;

  std::error_code ec;
  std::filesystem::create_directories(PathFromUtf8(dest_path), ec);
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
    const Status merkle_st = audit::ComputeMerkleRootForFiles(
        files, subset_root, chunk_store_->digest_algo());
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

  RestorePlanBuildResult plan{};
  const Status plan_st = BuildRestorePlan(files, options, &plan);
  if (!plan_st.ok()) return plan_st;
  const auto& restore_plan = plan.entries;
  const auto& dest_rel_by_manifest = plan.dest_rel_by_manifest;

  const std::filesystem::path dest_root = PathFromUtf8(dest_path);

  std::unordered_map<RestoreInodeKey, std::string, RestoreInodeKeyHash> inode_canonical;
  std::vector<report::BackupPathIssue> restore_issues;
  for (const auto& [file, dest_rel] : restore_plan) {
    const Status st = RestoreEntry(dest_root, file, dest_rel, options,
                                   &inode_canonical, &restore_issues);
    if (!st.ok()) return st;
  }

  if (do_content_verify) {
    uint8_t actual_root[32]{};
    const std::unordered_map<std::string, std::string>* override_ptr =
        options.path_remap.HasRemap() ? &dest_rel_by_manifest : nullptr;
    const Status merkle_st = audit::ComputeMerkleRootFromRestoredFiles(
        dest_path, files, chunk_store_, actual_root, override_ptr);
    if (!merkle_st.ok()) return merkle_st;
    if (std::memcmp(subset_root, actual_root, 32) != 0) {
      return Status::Corrupt("restored content merkle mismatch");
    }
  }

  if (options.acceptance_out) {
    std::vector<std::string> paths;
    uint64_t total_bytes = 0;
    std::vector<std::pair<std::string, ManifestFileEntry>> restored_files;
    for (const auto& [file, dest_rel] : restore_plan) {
      paths.push_back(dest_rel);
      if (file.file_type == FileType::kRegular) {
        total_bytes += file.size;
        restored_files.emplace_back(dest_rel, file);
      }
    }
    std::sort(paths.begin(), paths.end());
    std::string merkle_hex;
    if (do_content_verify) {
      merkle_hex = BytesToHex(subset_root, 32);
    } else {
      uint8_t root[32]{};
      const Status merkle_st = audit::ComputeMerkleRootForFiles(
          files, root, chunk_store_->digest_algo());
      if (merkle_st.ok()) merkle_hex = BytesToHex(root, 32);
    }
    (void)catalog::BuildRestoreAcceptanceReportWithFiles(
        restored_files, merkle_hex, doc.txn_id, total_bytes, do_content_verify,
        chunk_store_->digest_algo(), options.acceptance_out);
    options.acceptance_out->issues = std::move(restore_issues);
  }
  return Status::Ok();
}

}  // namespace ebbackup
