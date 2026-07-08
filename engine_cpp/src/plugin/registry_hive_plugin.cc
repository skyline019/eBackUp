#include "ebbackup/plugin/backup_plugin.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "ebbackup/common/path_encoding.h"
#include "ebbackup/common/path_util.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ebbackup {
namespace plugin {
namespace {

#ifdef _WIN32
std::string RegistryStagingDir(const std::string& source_path) {
  return RepoJoinUtf8(source_path, ".ebbackup/registry");
}

Status ExportHive(HKEY root, const wchar_t* subkey, const std::filesystem::path& out_file,
                  std::vector<std::string>* failures) {
  HKEY opened = nullptr;
  LONG rc = RegOpenKeyExW(root, subkey, 0, KEY_READ, &opened);
  if (rc != ERROR_SUCCESS) {
    if (failures) failures->push_back(WideToUtf8(subkey));
    return Status::Internal("RegOpenKeyEx failed");
  }
  rc = RegSaveKeyW(opened, out_file.wstring().c_str(), nullptr);
  RegCloseKey(opened);
  if (rc != ERROR_SUCCESS) {
    if (failures) failures->push_back(WideToUtf8(subkey));
    return Status::Internal("RegSaveKey failed");
  }
  return Status::Ok();
}
#endif

class RegistryHivePlugin : public IBackupPlugin {
 public:
  const char* id() const override { return "registry_hive"; }

  Status Quiesce() override {
#ifdef _WIN32
    staging_dir_ = RegistryStagingDir(ctx_.source_path);
    std::error_code ec;
    std::filesystem::create_directories(PathFromUtf8(staging_dir_), ec);
    exported_.clear();
    failures_.clear();

    struct HiveSpec {
      HKEY root;
      const wchar_t* subkey;
      const char* filename;
    };
    const HiveSpec specs[] = {
        {HKEY_LOCAL_MACHINE, L"SYSTEM", "SYSTEM.hive"},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE", "SOFTWARE.hive"},
    };
    for (const auto& spec : specs) {
      const std::filesystem::path out =
          PathFromUtf8(RepoJoinUtf8(staging_dir_, spec.filename));
      const Status st = ExportHive(spec.root, spec.subkey, out, &failures_);
      if (st.ok()) exported_.push_back(PathToUtf8(out));
    }

    HKEY hkcu = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"", 0, KEY_READ, &hkcu) == ERROR_SUCCESS) {
      const std::filesystem::path out =
          PathFromUtf8(RepoJoinUtf8(staging_dir_, "NTUSER.DAT"));
      const LONG rc = RegSaveKeyW(hkcu, out.wstring().c_str(), nullptr);
      RegCloseKey(hkcu);
      if (rc == ERROR_SUCCESS) {
        exported_.push_back(PathToUtf8(out));
      } else {
        failures_.push_back("HKCU");
      }
    } else {
      failures_.push_back("HKCU");
    }
#endif
    return Status::Ok();
  }

  Status ExtraScanRoots(std::vector<std::string>* out) override {
    if (!out || staging_dir_.empty()) return Status::Ok();
#ifdef _WIN32
    out->push_back(staging_dir_);
#endif
    return Status::Ok();
  }

  std::string PluginReportJson() const override {
    return std::string("{\"id\":\"registry_hive\",\"exported\":") +
           std::to_string(exported_.size()) + ",\"failed\":" +
           std::to_string(failures_.size()) + "}";
  }

 private:
  std::string staging_dir_;
  std::vector<std::string> exported_;
  std::vector<std::string> failures_;
};

}  // namespace

std::unique_ptr<IBackupPlugin> MakeRegistryHivePlugin() {
  return std::make_unique<RegistryHivePlugin>();
}

}  // namespace plugin
}  // namespace ebbackup
