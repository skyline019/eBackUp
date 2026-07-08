#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"

namespace ebbackup {
namespace winmeta {

struct VssVolumeMap {
  std::string mount_prefix;
  std::string shadow_prefix;
};

enum class VssConsistencyMode { kCrash, kApp, kAuto };

struct VssBeginOptions {
  VssConsistencyMode mode{VssConsistencyMode::kCrash};
  bool include_junction_volumes{true};
  uint32_t junction_probe_depth{2};
  uint64_t shadow_storage_min_bytes{512ULL * 1024ULL * 1024ULL};
  bool skip_shadow_preflight{false};
};

struct VssWriterStatus {
  std::string id;
  std::string name;
  std::string state;
};

struct VssSessionInfo {
  std::string snapshot_set_id;
  std::string consistency;
  std::string vss_mode;
  std::vector<std::string> volumes;
  std::vector<VssWriterStatus> writers;
  bool cross_volume{false};
  bool shadow_storage_ok{true};
  uint64_t shadow_storage_bytes{0};
  std::vector<uint64_t> shadow_storage_bytes_by_volume;
};

std::string VssConsistencyModeToString(VssConsistencyMode mode);
bool ParseVssConsistencyMode(const std::string& text, VssConsistencyMode* out);

// Pure path remapping (testable without VSS service).
std::string MapPathWithVolumeMaps(const std::string& path,
                                  const std::vector<VssVolumeMap>& maps,
                                  bool to_shadow);

class VssSession {
 public:
  VssSession();
  ~VssSession();

  VssSession(const VssSession&) = delete;
  VssSession& operator=(const VssSession&) = delete;

  static Status CheckPrerequisites();

  Status Begin(const std::vector<std::string>& logical_roots,
               const VssBeginOptions& opts = VssBeginOptions{});
  Status FinishBackup();
  Status End();

  std::string MapToShadow(const std::string& logical_utf8) const;
  std::string MapToLogicalForReport(const std::string& path_utf8) const;
  const VssSessionInfo& info() const { return info_; }
  bool active() const { return active_; }
  bool backup_finished() const { return backup_finished_; }

 private:
  void ResetState();

  bool active_{false};
  bool backup_finished_{false};
  bool com_initialized_{false};
  VssSessionInfo info_;
  std::vector<VssVolumeMap> volume_maps_;
  VssConsistencyMode requested_mode_{VssConsistencyMode::kCrash};

#ifdef _WIN32
  struct Impl;
  std::unique_ptr<Impl> impl_;
#endif
};

}  // namespace winmeta
}  // namespace ebbackup
