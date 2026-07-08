#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"
#include "ebbackup/winmeta/vss_volume_closure.h"

namespace ebbackup {
namespace winmeta {

struct VssShadowStorageInfo {
  std::string volume;
  std::string diff_volume;
  uint64_t used_bytes{0};
  uint64_t max_bytes{0};
  uint64_t allocated_bytes{0};
};

#ifdef _WIN32
Status QueryShadowStorage(std::vector<VssShadowStorageInfo>* out);
Status CheckShadowStoragePreflightEx(const std::vector<VssVolumeSpec>& volumes,
                                     uint64_t min_free_bytes,
                                     std::vector<VssShadowStorageInfo>* storage_out);
std::string FormatShadowStorageStatusJson(
    const std::vector<VssShadowStorageInfo>& entries);
#endif

}  // namespace winmeta
}  // namespace ebbackup
