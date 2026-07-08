#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ebbackup/common/digest.h"
#include "ebbackup/common/status.h"
#include "ebbackup/engine/manifest.h"

namespace ebbackup {
namespace catalog {

struct PathIndexRecord {
  std::string path;
  uint64_t txn_id{0};
  uint64_t size{0};
  std::string content_hash_hex;
  FileType file_type{FileType::kRegular};
  int64_t mtime_unix{0};
};

struct ManifestFilePage {
  uint64_t txn_id{0};
  uint64_t total{0};
  uint64_t offset{0};
  std::vector<ManifestFileEntry> files;
};

struct PathHistoryPage {
  uint64_t total{0};
  uint64_t offset{0};
  std::vector<PathIndexRecord> records;
};

std::string PathIndexFilePath(const std::string& repo_path);

Status AppendManifestToPathIndex(const std::string& repo_path,
                                 const ManifestDocument& doc);

Status BuildPathIndexFromSnapshots(
    const std::string& repo_path,
    const std::function<Status(uint64_t txn_id, ManifestDocument* out)>&
        load_manifest);

Status QueryPathHistory(const std::string& repo_path, const std::string& path,
                        std::vector<PathIndexRecord>* out);

Status QueryPathHistoryPage(const std::string& repo_path, const std::string& path,
                          uint64_t offset, uint64_t limit, PathHistoryPage* out);

Status ListManifestFilesPage(const ManifestDocument& doc,
                             const std::string& prefix, uint64_t offset,
                             uint64_t limit, ManifestFilePage* out);

std::string ComputeFileContentHashHex(const ManifestFileEntry& file,
                                      DigestAlgo algo);

std::string PathHistoryToJson(const std::vector<PathIndexRecord>& records);
std::string PathHistoryPageToJson(const PathHistoryPage& page);
std::string ManifestPageToJson(const ManifestFilePage& page,
                               const char* index_source = nullptr);

}  // namespace catalog
}  // namespace ebbackup
