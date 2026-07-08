#include "ebbackup/winmeta/vss_session.h"

#include <algorithm>

#include "ebbackup/common/path_util.h"

namespace ebbackup {
namespace winmeta {

namespace {

std::string NormalizePathPrefix(std::string path) {
  path = NormalizeRepoPath(std::move(path));
  while (path.size() > 1 && path.back() == '/') path.pop_back();
  if (!path.empty() && path.back() != '/') path.push_back('/');
  return path;
}

const VssVolumeMap* FindBestMap(const std::string& normalized_path,
                                const std::vector<VssVolumeMap>& maps,
                                bool use_shadow_prefix) {
  const VssVolumeMap* best = nullptr;
  size_t best_len = 0;
  for (const auto& map : maps) {
    const std::string prefix =
        use_shadow_prefix ? NormalizePathPrefix(map.shadow_prefix)
                          : NormalizePathPrefix(map.mount_prefix);
    if (normalized_path.size() >= prefix.size() &&
        normalized_path.compare(0, prefix.size(), prefix) == 0 &&
        prefix.size() > best_len) {
      best = &map;
      best_len = prefix.size();
    }
  }
  return best;
}

}  // namespace

std::string MapPathWithVolumeMaps(const std::string& path,
                                  const std::vector<VssVolumeMap>& maps,
                                  bool to_shadow) {
  if (path.empty() || maps.empty()) return path;
  const std::string normalized = NormalizePathPrefix(path);
  const VssVolumeMap* map =
      FindBestMap(normalized, maps, /*use_shadow_prefix=*/!to_shadow);
  if (!map) return path;
  const std::string from_prefix =
      NormalizePathPrefix(to_shadow ? map->mount_prefix : map->shadow_prefix);
  const std::string to_prefix =
      NormalizePathPrefix(to_shadow ? map->shadow_prefix : map->mount_prefix);
  if (normalized.size() < from_prefix.size()) return path;
  std::string suffix = normalized.substr(from_prefix.size());
  std::string out = to_prefix + suffix;
  if (!suffix.empty() && out.size() > 1 && out.back() == '/') out.pop_back();
  return out;
}

}  // namespace winmeta
}  // namespace ebbackup
