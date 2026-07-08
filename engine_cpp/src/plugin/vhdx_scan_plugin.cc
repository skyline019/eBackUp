#include "ebbackup/plugin/backup_plugin.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "ebbackup/common/hook_runner.h"
#include "ebbackup/common/path_util.h"

namespace ebbackup {
namespace plugin {
namespace {

#ifdef _WIN32
bool EndsWithVhdx(const std::string& path) {
  if (path.size() < 5) return false;
  std::string tail = path.substr(path.size() - 5);
  for (char& c : tail) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return tail == ".vhdx";
}

void CollectVhdxPaths(const std::filesystem::path& root,
                      std::vector<std::string>* out) {
  if (!out) return;
  std::error_code ec;
  for (auto it = std::filesystem::recursive_directory_iterator(
           root, std::filesystem::directory_options::skip_permission_denied, ec);
       it != std::filesystem::recursive_directory_iterator(); ++it) {
    std::error_code fec;
    if (!it->is_regular_file(fec)) continue;
    const std::string abs = PathToUtf8(it->path());
    if (EndsWithVhdx(abs)) out->push_back(abs);
  }
}

std::string EscapePsSingleQuoted(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '\'') out += "''";
    else out.push_back(c);
  }
  return out;
}
#endif

class VhdxScanPlugin : public IBackupPlugin {
 public:
  const char* id() const override { return "vhdx_scan"; }

  Status Quiesce() override {
#ifdef _WIN32
    mounts_.clear();
    hints_.clear();
    failures_.clear();
    if (ctx_.source_path.empty()) return Status::Ok();

    std::vector<std::string> vhdx_paths;
    CollectVhdxPaths(PathFromUtf8(ctx_.source_path), &vhdx_paths);
    size_t idx = 0;
    for (const std::string& vhdx : vhdx_paths) {
      const std::string mount_dir =
          RepoJoinUtf8(ctx_.source_path, ".ebbackup/vhdx/" + std::to_string(idx++));
      std::error_code ec;
      std::filesystem::create_directories(PathFromUtf8(mount_dir), ec);

      const std::string ps =
          "powershell -NoProfile -Command \"$ErrorActionPreference='Stop'; "
          "Mount-Vhd -Path '" +
          EscapePsSingleQuoted(vhdx) + "' -DestinationPath '" +
          EscapePsSingleQuoted(mount_dir) + "' -ReadOnly\"";
      int rc = 0;
      const Status st = RunShellCommand(ps, &rc);
      if (!st.ok() || rc != 0) {
        failures_.push_back(vhdx);
        continue;
      }
      mounts_.push_back({vhdx, mount_dir});
      ScanHint skip{};
      skip.path_prefix = vhdx;
      skip.skip_subtree = true;
      hints_.push_back(skip);
    }
#endif
    return Status::Ok();
  }

  Status Thaw() override {
#ifdef _WIN32
    for (const auto& m : mounts_) {
      const std::string ps =
          "powershell -NoProfile -Command \"$ErrorActionPreference='SilentlyContinue'; "
          "Dismount-Vhd -Path '" +
          EscapePsSingleQuoted(m.vhdx_path) + "'\"";
      int rc = 0;
      (void)RunShellCommand(ps, &rc);
    }
    mounts_.clear();
#endif
    return Status::Ok();
  }

  Status ScanHints(std::vector<ScanHint>* out) override {
    if (!out) return Status::InvalidArgument("out is null");
    out->insert(out->end(), hints_.begin(), hints_.end());
    return Status::Ok();
  }

  Status ExtraScanRoots(std::vector<std::string>* out) override {
    if (!out) return Status::Ok();
#ifdef _WIN32
    for (const auto& m : mounts_) out->push_back(m.mount_dir);
#endif
    return Status::Ok();
  }

  std::string PluginReportJson() const override {
    return std::string("{\"id\":\"vhdx_scan\",\"mounted\":") +
           std::to_string(mounts_.size()) + ",\"failed\":" +
           std::to_string(failures_.size()) + "}";
  }

 private:
  struct MountRecord {
    std::string vhdx_path;
    std::string mount_dir;
  };
  std::vector<MountRecord> mounts_;
  std::vector<ScanHint> hints_;
  std::vector<std::string> failures_;
};

}  // namespace

std::unique_ptr<IBackupPlugin> MakeVhdxScanPlugin() {
  return std::make_unique<VhdxScanPlugin>();
}

}  // namespace plugin
}  // namespace ebbackup
