#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ebbackup/chunk/cfi_index.h"
#include "ebbackup/chunk/chunk_descriptor.h"
#include "ebbackup/common/status.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/store/chunk_store.h"

namespace ebbackup {

struct BackupPipelineOptions {
  bool use_lz4{false};
  bool use_encryption{false};
  const uint8_t* content_key{nullptr};
  bool use_mmap{true};
  size_t queue_depth{8};
};

struct BackupPipelineResult {
  std::vector<ManifestFileEntry> manifest_files;
};

Status RunBackupPipeline(const std::vector<std::string>& file_paths,
                           const std::string& source_root, BackupMode mode,
                           const ManifestDocument* prior_manifest,
                           ChunkStore* chunk_store, BackupStats* stats,
                           const BackupPipelineOptions& options,
                           BackupPipelineResult* result,
                           ProgressCallback progress_cb = nullptr,
                           void* progress_user = nullptr);

}  // namespace ebbackup
