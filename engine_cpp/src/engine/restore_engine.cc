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
#include "ebbackup/crypto/envelope.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/engine/restore_plan.h"
#include "ebbackup/engine/restore_path_remap.h"
#include "ebbackup/io/file_meta.h"
#include "ebbackup/scan/backup_filter.h"
#include "ebbackup/store/snapshot_store.h"
#include "ebbackup/winmeta/win_meta.h"
#include "ebbackup/winmeta/efs_key.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
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

Status WritePathBytesAtWin32(HANDLE handle, uint64_t offset, const uint8_t* data,
                             size_t len) {
  if (handle == INVALID_HANDLE_VALUE) {
    return Status::InvalidArgument("invalid restore write handle");
  }
  if (len == 0) return Status::Ok();
  LARGE_INTEGER pos{};
  pos.QuadPart = static_cast<LONGLONG>(offset);
  if (!SetFilePointerEx(handle, pos, nullptr, FILE_BEGIN)) {
    return Status::IoError("restore seek failed");
  }
  DWORD written = 0;
  const BOOL ok = WriteFile(handle, data, static_cast<DWORD>(len), &written, nullptr);
  if (!ok || written != len) return Status::IoError("restore sparse write failed");
  return Status::Ok();
}

Status PrepareSparseRestoreFileWin32(HANDLE handle, uint64_t logical_size) {
  if (handle == INVALID_HANDLE_VALUE) return Status::InvalidArgument("invalid handle");
  DWORD bytes = 0;
  if (!DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &bytes,
                       nullptr)) {
    return Status::IoError("FSCTL_SET_SPARSE failed");
  }
  if (logical_size == 0) return Status::Ok();
  LARGE_INTEGER pos{};
  pos.QuadPart = static_cast<LONGLONG>(logical_size - 1);
  if (!SetFilePointerEx(handle, pos, nullptr, FILE_BEGIN)) {
    return Status::IoError("sparse SetEnd seek failed");
  }
  uint8_t zero = 0;
  DWORD written = 0;
  if (!WriteFile(handle, &zero, 1, &written, nullptr) || written != 1) {
    return Status::IoError("sparse SetEnd write failed");
  }
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
  const bool has_envelope = crypto::EnvelopeExists(repo_path_);
  if (!has_envelope && !std::filesystem::exists(salt_path)) return Status::Ok();
  if (has_envelope) {
    if (!options.recovery_key.empty()) {
      uint8_t key[32];
      const Status st = crypto::UnwrapMasterKeyWithRecoveryKey(
          repo_path_, options.recovery_key, key, chunk_store_->digest_algo());
      if (!st.ok()) return st;
      chunk_store_->SetContentKey(key);
      return Status::Ok();
    }
    if (options.encryption_password.empty()) {
      return Status::InvalidArgument("encrypted repo requires password");
    }
    uint8_t key[32];
    const Status st = crypto::UnwrapMasterKeyWithPassword(
        repo_path_, options.encryption_password, key, chunk_store_->digest_algo());
    if (!st.ok()) return st;
    chunk_store_->SetContentKey(key);
    return Status::Ok();
  }
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
  if (file.file_type == FileType::kRegular && file.efs_encrypted) {
    if (std::filesystem::exists(out_path, ec)) {
      std::filesystem::remove(out_path, ec);
    }
    if (!file.efs_key_blob_b64.empty()) {
      const Status imp = winmeta::ImportEfsKeyBlob(write_path, file.efs_key_blob_b64);
      if (!imp.ok()) {
        RecordRestoreIssue(write_path, "efs_key_import_failed", restore_issues);
        return imp;
      }
      RecordRestoreIssue(write_path, "efs_restored", restore_issues);
    } else {
      std::ofstream placeholder(PathFromUtf8(write_path), std::ios::binary | std::ios::trunc);
      if (!placeholder) {
        return Status::IoError("cannot create EFS placeholder: " + write_path);
      }
      placeholder.close();
      RecordRestoreIssue(write_path, "efs_encrypted_skipped_restore", restore_issues);
    }
    std::string soft_issue;
    const Status meta_st =
        ApplyWinMetaAfterRestore(write_path, file, options.acl_policy, &soft_issue);
    if (!meta_st.ok()) return meta_st;
    RecordRestoreIssue(write_path, soft_issue, restore_issues);
    return Status::Ok();
  }

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
  const bool sparse_restore =
      file.sparse && !file.sparse_chunk_offsets.empty() && file.file_type == FileType::kRegular;
  HANDLE win_handle = INVALID_HANDLE_VALUE;
  if (win_stream_path || sparse_restore) {
    const Status open_st = OpenPathForRestoreWriteWin32(write_path, &win_handle);
    if (!open_st.ok()) return open_st;
    if (sparse_restore) {
      const Status prep = PrepareSparseRestoreFileWin32(win_handle, file.size);
      if (!prep.ok()) {
        CloseHandle(win_handle);
        return prep;
      }
    }
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
  if (!win_stream_path && !sparse_restore) {
    out_file = std::make_unique<std::ofstream>(
        PathFromUtf8(write_path), std::ios::binary | std::ios::trunc);
    if (!*out_file) {
      return Status::IoError("cannot write restore file: " + write_path);
    }
  }
#endif

  for (size_t ci = 0; ci < file.chunk_hashes_hex.size(); ++ci) {
    const auto& hex = file.chunk_hashes_hex[ci];
    uint8_t hash[32];
    if (!HexToBytes(hex, hash, 32)) {
      return Status::Corrupt("invalid chunk hash in manifest");
    }
    payload.clear();
    const Status st = chunk_store_->Get(hash, &payload);
    if (!st.ok()) return st;
#ifdef _WIN32
    if (sparse_restore) {
      const uint64_t off = ci < file.sparse_chunk_offsets.size()
                               ? file.sparse_chunk_offsets[ci]
                               : 0;
      const Status wr =
          WritePathBytesAtWin32(win_handle, off, payload.data(), payload.size());
      if (!wr.ok()) {
        CloseHandle(win_handle);
        return wr;
      }
    } else if (win_stream_path) {
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
  if (win_stream_path || sparse_restore) {
    CloseHandle(win_handle);
  } else
#endif
  {
    out_file->close();
  }

  if (!sparse_restore && bytes_written != file.size) {
    return Status::Corrupt("restored size mismatch for " + dest_rel);
  }
#ifdef _WIN32
  if (sparse_restore) {
    uint64_t run_total = 0;
    for (const auto& run : file.sparse_runs) run_total += run.second;
    if (bytes_written != run_total) {
      return Status::Corrupt("restored sparse data size mismatch for " + dest_rel);
    }
    const std::wstring wide = Utf8ToWide(write_path);
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(wide.c_str(), GetFileExInfoStandard, &fad)) {
      return Status::IoError("cannot stat restored sparse file");
    }
    const uint64_t logical =
        (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    if (logical != file.size) {
      return Status::Corrupt("restored sparse logical size mismatch for " + dest_rel);
    }
  }
#endif

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
