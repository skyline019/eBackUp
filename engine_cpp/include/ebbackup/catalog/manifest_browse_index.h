#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ebbackup/catalog/path_index.h"
#include "ebbackup/common/status.h"
#include "ebbackup/engine/manifest.h"

namespace ebbackup {
namespace catalog {

struct ManifestBrowseRecord {
  std::string relative_path;
  uint64_t txn_id{0};
  uint64_t size{0};
  FileType file_type{FileType::kRegular};
  int64_t mtime_unix{0};
  uint32_t chunk_count{0};
};

std::string ManifestBrowseIndexPath(const std::string& repo_path, uint64_t txn_id);
std::string ManifestBrowseIndexDir(const std::string& repo_path);

Status AppendManifestBrowseIndex(const std::string& repo_path, uint64_t txn_id,
                                 const std::vector<ManifestFileEntry>& files);

Status WriteManifestBrowseIndex(const std::string& repo_path, uint64_t txn_id,
                                const std::vector<ManifestBrowseRecord>& records);

Status WriteManifestBrowseIndexToPath(const std::string& index_path, uint64_t txn_id,
                                      std::vector<ManifestBrowseRecord> records);

Status BuildManifestBrowseIndexFromSnapshots(
    const std::string& repo_path,
    const std::function<Status(uint64_t txn_id, std::string* manifest_path)>&
        resolve_manifest_path);

Status QueryManifestBrowsePage(const std::string& repo_path, uint64_t txn_id,
                               const std::string& prefix, uint64_t offset,
                               uint64_t limit, ManifestFilePage* out);

}  // namespace catalog
}  // namespace ebbackup
