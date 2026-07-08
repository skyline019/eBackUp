#pragma once

#include <cstdint>
#include <string>

#include "ebbackup/common/status.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/scan/scan_entry.h"

namespace ebbackup {
namespace winmeta {

constexpr uint32_t kManifestMetaV5 = 0x01u;

struct WinMetaExtension {
  std::string security_descriptor_b64;
  uint64_t inode_id{0};
  uint32_t reparse_tag{0};
  std::string reparse_target;
  std::string stream_name;
};

struct AclRestorePolicy {
  enum class Mode : uint8_t {
    kInherit = 0,
    kPreserve = 1,
    kSkip = 2,
    kBestEffort = 3,
  };
  Mode mode{Mode::kInherit};
};

struct ReparseRestorePolicy {
  enum class Mode : uint8_t { kSkip = 0, kRecreate = 1 };
  Mode mode{Mode::kSkip};
};

void CopyWinMetaToManifest(const WinMetaExtension& win, ManifestFileEntry* entry);
void CopyWinMetaFromManifest(const ManifestFileEntry& entry, WinMetaExtension* out);

Status CaptureWinMetaFromPath(const std::string& absolute_path, ScanEntry* entry);
Status ReadWinMetaFromEntry(const ManifestFileEntry& entry,
                            WinMetaExtension* out);
Status ApplyWinMetaOnRestore(const std::string& dest_path,
                             const ManifestFileEntry& entry,
                             AclRestorePolicy policy,
                             std::string* soft_issue_reason = nullptr);
Status CreateHardLinkUtf8(const std::string& existing_path,
                          const std::string& link_path);
Status RecreateReparsePoint(const std::string& reparse_path,
                            const ManifestFileEntry& entry);

}  // namespace winmeta
}  // namespace ebbackup
