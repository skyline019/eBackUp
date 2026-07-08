#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"

namespace ebbackup {
namespace winmeta {

struct VssVolumeSpec {
  std::string volume_name_utf8;
  std::string mount_point_utf8;
};

struct VssVolumeClosureOptions {
  bool include_junction_volumes{true};
  uint32_t junction_probe_depth{2};
};

#ifdef _WIN32
Status ResolveVolumeForPath(const std::string& logical_path,
                            VssVolumeSpec* out);
Status ComputeVolumeClosure(const std::vector<std::string>& logical_roots,
                            const VssVolumeClosureOptions& opts,
                            std::vector<VssVolumeSpec>* out);
Status CheckShadowStoragePreflight(const std::vector<VssVolumeSpec>& volumes,
                                   uint64_t min_free_bytes);
#endif

}  // namespace winmeta
}  // namespace ebbackup
