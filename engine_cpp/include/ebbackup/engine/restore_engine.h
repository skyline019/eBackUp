#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "ebbackup/catalog/restore_acceptance.h"
#include "ebbackup/common/status.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/engine/restore_path_remap.h"
#include "ebbackup/report/backup_report.h"
#include "ebbackup/scan/backup_filter.h"
#include "ebbackup/store/chunk_store.h"
#include "ebbackup/winmeta/win_meta.h"

namespace ebbackup {

struct RestorePreviewReport {
  uint64_t file_count{0};
  uint64_t dir_count{0};
  uint64_t total_bytes{0};
};

struct RestoreInodeKey {
  uint64_t inode_id{0};
  std::string stream_name;
  bool operator==(const RestoreInodeKey& o) const {
    return inode_id == o.inode_id && stream_name == o.stream_name;
  }
};

struct RestoreInodeKeyHash {
  size_t operator()(const RestoreInodeKey& k) const {
    return std::hash<uint64_t>{}(k.inode_id) ^
           (std::hash<std::string>{}(k.stream_name) << 1);
  }
};

struct RestoreOptions {
  std::string encryption_password;
  std::string recovery_key;
  BackupFilterOptions filter;
  RestorePathRemap path_remap;
  SymlinkRemap symlink_remap;
  bool verify_subset_merkle{true};
  bool verify_restored_content{false};
  uint64_t snapshot_txn_id{0};
  winmeta::AclRestorePolicy acl_policy{};
  winmeta::ReparseRestorePolicy reparse_policy{};
  catalog::RestoreAcceptanceReport* acceptance_out{nullptr};
};

class RestoreEngine {
 public:
  RestoreEngine(std::string repo_path, ChunkStore* chunk_store);

  Status RunRestore(const std::string& dest_path,
                    const RestoreOptions& options = RestoreOptions{});

  Status RestorePlannedEntry(
      const std::filesystem::path& dest_root, const ManifestFileEntry& file,
      const std::string& dest_rel, const RestoreOptions& options,
      std::unordered_map<RestoreInodeKey, std::string, RestoreInodeKeyHash>*
          inode_canonical,
      std::vector<report::BackupPathIssue>* restore_issues);

 private:
  Status SetupEncryption(const RestoreOptions& options);
  Status RestoreEntry(const std::filesystem::path& dest_root,
                      const ManifestFileEntry& file,
                      const std::string& dest_rel,
                      const RestoreOptions& options,
                      std::unordered_map<RestoreInodeKey, std::string, RestoreInodeKeyHash>*
                          inode_canonical,
                      std::vector<report::BackupPathIssue>* restore_issues);

  std::string repo_path_;
  ChunkStore* chunk_store_{nullptr};
};

}  // namespace ebbackup
