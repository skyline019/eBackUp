#pragma once

#include <filesystem>
#include <string>

#include "ebbackup/common/status.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/scan/backup_filter.h"
#include "ebbackup/store/chunk_store.h"

namespace ebbackup {

struct RestoreOptions {
  std::string encryption_password;
  BackupFilterOptions filter;
  bool verify_subset_merkle{true};
  bool verify_restored_content{false};
  uint64_t snapshot_txn_id{0};
};

class RestoreEngine {
 public:
  RestoreEngine(std::string repo_path, ChunkStore* chunk_store);

  Status RunRestore(const std::string& dest_path,
                    const RestoreOptions& options = RestoreOptions{});

 private:
  Status SetupEncryption(const RestoreOptions& options);
  Status RestoreEntry(const std::filesystem::path& dest_root,
                      const ManifestFileEntry& file);

  std::string repo_path_;
  ChunkStore* chunk_store_{nullptr};
};

}  // namespace ebbackup
