#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ebbackup/common/status.h"

namespace ebbackup {
namespace winmeta {

struct SparseRun {
  uint64_t offset{0};
  uint64_t length{0};
};

#ifdef _WIN32
bool IsSparseFilePath(const std::string& path_utf8);
Status QuerySparseRuns(const std::string& path_utf8, uint64_t* logical_size,
                       std::vector<SparseRun>* runs);
Status ReadSparseFileBytes(const std::string& path_utf8,
                           const std::vector<SparseRun>& runs,
                           std::vector<uint8_t>* out);
#endif

}  // namespace winmeta
}  // namespace ebbackup
