#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ebbackup/common/status.h"

namespace ebbackup {
namespace catalog {

struct SnapshotMetaRecord {
  uint64_t txn_id{0};
  std::string job_id;
  uint32_t retention_tag{0};
  int64_t immutable_until_unix{0};
};

std::string SnapshotMetaPath(const std::string& repo_path);
Status AppendSnapshotMeta(const std::string& repo_path,
                          const SnapshotMetaRecord& record);
Status LoadSnapshotMeta(const std::string& repo_path,
                        std::vector<SnapshotMetaRecord>* out);
Status LoadSnapshotMetaMap(const std::string& repo_path,
                           std::unordered_map<uint64_t, SnapshotMetaRecord>* out);
Status FindSnapshotMeta(const std::string& repo_path, uint64_t txn_id,
                        SnapshotMetaRecord* out);

}  // namespace catalog
}  // namespace ebbackup
