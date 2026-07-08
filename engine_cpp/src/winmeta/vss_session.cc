#include "ebbackup/winmeta/vss_session.h"

#include "ebbackup/winmeta/vss_shadow_storage.h"

#include <algorithm>
#include <cctype>

namespace ebbackup {
namespace winmeta {

std::string VssConsistencyModeToString(VssConsistencyMode mode) {
  switch (mode) {
    case VssConsistencyMode::kApp:
      return "app";
    case VssConsistencyMode::kAuto:
      return "auto";
    case VssConsistencyMode::kCrash:
    default:
      return "crash";
  }
}

bool ParseVssConsistencyMode(const std::string& text, VssConsistencyMode* out) {
  if (!out) return false;
  std::string lower = text;
  for (char& c : lower) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  if (lower == "app") {
    *out = VssConsistencyMode::kApp;
    return true;
  }
  if (lower == "auto") {
    *out = VssConsistencyMode::kAuto;
    return true;
  }
  if (lower == "crash") {
    *out = VssConsistencyMode::kCrash;
    return true;
  }
  return false;
}

}  // namespace winmeta
}  // namespace ebbackup

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shlobj.h>

#include <initguid.h>
#include <vss.h>
#include <vswriter.h>
#include <vsbackup.h>

#include <comdef.h>

#include <memory>
#include <set>
#include <vector>

#include "ebbackup/common/path_encoding.h"
#include "ebbackup/common/path_util.h"

namespace ebbackup {
namespace winmeta {
namespace {

std::string GuidToString(const GUID& guid) {
  wchar_t wbuf[64]{};
  if (StringFromGUID2(guid, wbuf, static_cast<int>(std::size(wbuf))) <= 0) {
    return {};
  }
  return WideToUtf8(wbuf);
}

std::string HresultMessage(HRESULT hr) {
  _com_error err(hr);
  const TCHAR* msg = err.ErrorMessage();
  if (!msg) return "HRESULT " + std::to_string(static_cast<unsigned long>(hr));
#ifdef UNICODE
  return WideToUtf8(msg);
#else
  return std::string(msg);
#endif
}

std::string NormalizeVolumeMountPrefix(std::wstring mount) {
  if (mount.empty()) return {};
  if (mount.back() != L'\\') mount.push_back(L'\\');
  return NormalizeRepoPath(WideToUtf8(mount));
}

std::string NormalizeDevicePrefix(const wchar_t* device) {
  if (!device || !device[0]) return {};
  std::wstring w(device);
  if (w.back() != L'\\') w.push_back(L'\\');
  return NormalizeRepoPath(WideToUtf8(w));
}

bool EnablePrivilege(LPCWSTR name) {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(),
                        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
    return false;
  }
  LUID luid{};
  if (!LookupPrivilegeValueW(nullptr, name, &luid)) {
    CloseHandle(token);
    return false;
  }
  TOKEN_PRIVILEGES tp{};
  tp.PrivilegeCount = 1;
  tp.Privileges[0].Luid = luid;
  tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
  const BOOL ok =
      AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
  const DWORD err = GetLastError();
  CloseHandle(token);
  return ok && err == ERROR_SUCCESS;
}

std::string WriterStateToString(VSS_WRITER_STATE state) {
  switch (state) {
    case VSS_WS_STABLE:
      return "stable";
    case VSS_WS_WAITING_FOR_FREEZE:
      return "waiting_for_freeze";
    case VSS_WS_WAITING_FOR_THAW:
      return "waiting_for_thaw";
    case VSS_WS_WAITING_FOR_POST_SNAPSHOT:
      return "waiting_for_post_snapshot";
    case VSS_WS_WAITING_FOR_BACKUP_COMPLETE:
      return "waiting_for_backup_complete";
    case VSS_WS_FAILED_AT_IDENTIFY:
      return "failed_at_identify";
    case VSS_WS_FAILED_AT_PREPARE_BACKUP:
      return "failed_at_prepare_backup";
    case VSS_WS_FAILED_AT_PREPARE_SNAPSHOT:
      return "failed_at_prepare_snapshot";
    case VSS_WS_FAILED_AT_FREEZE:
      return "failed_at_freeze";
    case VSS_WS_FAILED_AT_THAW:
      return "failed_at_thaw";
    case VSS_WS_FAILED_AT_POST_SNAPSHOT:
      return "failed_at_post_snapshot";
    case VSS_WS_FAILED_AT_BACKUP_COMPLETE:
      return "failed_at_backup_complete";
    default:
      return "unknown";
  }
}

bool IsWriterStateStable(VSS_WRITER_STATE state) {
  return state == VSS_WS_STABLE;
}

Status WaitAsync(IVssAsync* async, const char* label) {
  if (!async) return Status::Internal(std::string(label) + ": null async");
  std::unique_ptr<IVssAsync, void (*)(IVssAsync*)> guard(
      async, [](IVssAsync* p) {
        if (p) p->Release();
      });
  const HRESULT wait_hr = async->Wait();
  if (FAILED(wait_hr)) {
    return Status::Internal(std::string(label) + " wait failed: " +
                            HresultMessage(wait_hr));
  }
  return Status::Ok();
}

}  // namespace

struct VolumeSnapshotEntry {
  VssVolumeSpec spec;
  VSS_ID snapshot_id{GUID_NULL};
};

struct VssSession::Impl {
  IVssBackupComponents* backup{nullptr};
  VSS_ID snapshot_set_id{GUID_NULL};
  std::vector<VolumeSnapshotEntry> volumes;

  ~Impl() { Reset(); }

  void Reset() {
    volumes.clear();
    snapshot_set_id = GUID_NULL;
    if (backup) {
      backup->Release();
      backup = nullptr;
    }
  }

  Status Initialize() {
    Reset();
    IVssBackupComponents* raw = nullptr;
    const HRESULT hr = CreateVssBackupComponents(&raw);
    if (FAILED(hr) || !raw) {
      return Status::Internal("CreateVssBackupComponents failed: " +
                              HresultMessage(hr));
    }
    backup = raw;
    const HRESULT init_hr = backup->InitializeForBackup();
    if (FAILED(init_hr)) {
      Reset();
      return Status::Internal("InitializeForBackup failed: " +
                              HresultMessage(init_hr));
    }
    return Status::Ok();
  }

  Status GatherWriterMetadata() {
    if (!backup) return Status::Internal("VSS not initialized");
    IVssAsync* async = nullptr;
    const HRESULT hr = backup->GatherWriterMetadata(&async);
    if (FAILED(hr) || !async) {
      return Status::Internal("GatherWriterMetadata failed: " + HresultMessage(hr));
    }
    return WaitAsync(async, "GatherWriterMetadata");
  }

  Status StartSnapshotSet() {
    if (!backup) return Status::Internal("VSS not initialized");
    const HRESULT hr = backup->StartSnapshotSet(&snapshot_set_id);
    if (FAILED(hr)) {
      return Status::Internal("StartSnapshotSet failed: " + HresultMessage(hr));
    }
    return Status::Ok();
  }

  Status AddVolume(const VssVolumeSpec& spec, VSS_ID* snapshot_id) {
    if (!backup || !snapshot_id) {
      return Status::InvalidArgument("invalid AddVolume args");
    }
    const std::wstring volume = Utf8ToWide(spec.volume_name_utf8);
    const HRESULT hr =
        backup->AddToSnapshotSet(const_cast<LPWSTR>(volume.c_str()), GUID_NULL,
                                 snapshot_id);
    if (FAILED(hr)) {
      return Status::Internal("AddToSnapshotSet failed for " +
                              spec.volume_name_utf8 + ": " + HresultMessage(hr));
    }
    return Status::Ok();
  }

  Status DoSnapshotSet() {
    if (!backup) return Status::Internal("VSS not initialized");
    IVssAsync* async = nullptr;
    const HRESULT hr = backup->DoSnapshotSet(&async);
    if (FAILED(hr) || !async) {
      return Status::Internal("DoSnapshotSet failed: " + HresultMessage(hr));
    }
    return WaitAsync(async, "DoSnapshotSet");
  }

  Status GetSnapshotDeviceObject(const VSS_ID& snapshot_id,
                                 std::wstring* device_out) {
    if (!backup || !device_out) {
      return Status::InvalidArgument("invalid GetSnapshotDeviceObject args");
    }
    VSS_SNAPSHOT_PROP prop{};
    const HRESULT hr = backup->GetSnapshotProperties(snapshot_id, &prop);
    if (FAILED(hr)) {
      return Status::Internal("GetSnapshotProperties failed: " +
                              HresultMessage(hr));
    }
    if (!prop.m_pwszSnapshotDeviceObject) {
      VssFreeSnapshotProperties(&prop);
      return Status::Internal("snapshot device object missing");
    }
    *device_out = prop.m_pwszSnapshotDeviceObject;
    VssFreeSnapshotProperties(&prop);
    return Status::Ok();
  }

  Status GatherWriterStatus(std::vector<VssWriterStatus>* writers,
                            bool* any_unstable) {
    if (!backup) return Status::Internal("VSS not initialized");
    if (writers) writers->clear();
    if (any_unstable) *any_unstable = false;

    IVssAsync* async = nullptr;
    const HRESULT hr = backup->GatherWriterStatus(&async);
    if (FAILED(hr) || !async) {
      return Status::Internal("GatherWriterStatus failed: " + HresultMessage(hr));
    }
    const Status wait_st = WaitAsync(async, "GatherWriterStatus");
    if (!wait_st.ok()) return wait_st;

    UINT count = 0;
    const HRESULT cnt_hr = backup->GetWriterStatusCount(&count);
    if (FAILED(cnt_hr)) {
      return Status::Internal("GetWriterStatusCount failed: " +
                              HresultMessage(cnt_hr));
    }
    for (UINT i = 0; i < count; ++i) {
      VSS_ID instance_id = GUID_NULL;
      VSS_ID writer_id = GUID_NULL;
      BSTR writer_name = nullptr;
      VSS_WRITER_STATE state = VSS_WS_UNKNOWN;
      HRESULT writer_hr = S_OK;
      const HRESULT st_hr = backup->GetWriterStatus(
          i, &instance_id, &writer_id, &writer_name, &state, &writer_hr);
      if (FAILED(st_hr)) continue;
      VssWriterStatus row{};
      row.id = GuidToString(writer_id);
      if (writer_name) {
        row.name = WideToUtf8(writer_name);
        SysFreeString(writer_name);
      }
      row.state = WriterStateToString(state);
      if (writers) writers->push_back(std::move(row));
      if (any_unstable && !IsWriterStateStable(state)) {
        *any_unstable = true;
      }
    }
    return Status::Ok();
  }

  Status BackupComplete() {
    if (!backup) return Status::Ok();
    IVssAsync* async = nullptr;
    const HRESULT hr = backup->BackupComplete(&async);
    if (FAILED(hr)) {
      return Status::Internal("BackupComplete failed: " + HresultMessage(hr));
    }
    if (async) {
      const Status wait_st = WaitAsync(async, "BackupComplete");
      if (!wait_st.ok()) return wait_st;
    }
    return Status::Ok();
  }

  Status DeleteSnapshots() {
    if (!backup || snapshot_set_id == GUID_NULL) return Status::Ok();
    LONG deleted = 0;
    VSS_ID non_deleted_id = GUID_NULL;
    const HRESULT hr =
        backup->DeleteSnapshots(snapshot_set_id, VSS_OBJECT_SNAPSHOT_SET, TRUE,
                                &deleted, &non_deleted_id);
    if (FAILED(hr)) {
      return Status::Internal("DeleteSnapshots failed: " + HresultMessage(hr));
    }
    return Status::Ok();
  }
};

VssSession::VssSession() : impl_(std::make_unique<Impl>()) {}

VssSession::~VssSession() { (void)End(); }

void VssSession::ResetState() {
  active_ = false;
  backup_finished_ = false;
  if (com_initialized_) {
    CoUninitialize();
    com_initialized_ = false;
  }
  if (impl_) impl_->Reset();
  volume_maps_.clear();
  info_ = {};
}

Status VssSession::CheckPrerequisites() {
  if (IsUserAnAdmin()) return Status::Ok();
  if (EnablePrivilege(L"SeBackupPrivilege")) return Status::Ok();
  if (EnablePrivilege(L"SeRestorePrivilege")) return Status::Ok();
  return Status::InvalidArgument(
      "VSS requires Administrator or SeBackupPrivilege/SeRestorePrivilege");
}

Status VssSession::Begin(const std::vector<std::string>& logical_roots,
                         const VssBeginOptions& opts) {
  if (active_) {
    const Status end_st = End();
    if (!end_st.ok()) return end_st;
  }
  if (logical_roots.empty()) {
    return Status::InvalidArgument("VSS requires at least one logical root");
  }

  const Status prereq = CheckPrerequisites();
  if (!prereq.ok()) return prereq;

  const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(com_hr) && com_hr != RPC_E_CHANGED_MODE) {
    return Status::Internal("CoInitializeEx failed: " + HresultMessage(com_hr));
  }
  com_initialized_ = SUCCEEDED(com_hr);

  requested_mode_ = opts.mode;
  info_ = {};
  info_.vss_mode = VssConsistencyModeToString(opts.mode);
  volume_maps_.clear();
  backup_finished_ = false;

  Status st = impl_->Initialize();
  if (!st.ok()) {
    ResetState();
    return st;
  }

  st = impl_->GatherWriterMetadata();
  if (!st.ok()) {
    ResetState();
    return st;
  }

  VssVolumeClosureOptions closure_opts{};
  closure_opts.include_junction_volumes = opts.include_junction_volumes;
  closure_opts.junction_probe_depth = opts.junction_probe_depth;
  std::vector<VssVolumeSpec> closure;
  st = ComputeVolumeClosure(logical_roots, closure_opts, &closure);
  if (!st.ok()) {
    ResetState();
    return st;
  }

  info_.cross_volume = closure.size() > 1;
  if (!opts.skip_shadow_preflight) {
    std::vector<VssShadowStorageInfo> storage;
    st = CheckShadowStoragePreflightEx(closure, opts.shadow_storage_min_bytes,
                                       &storage);
    info_.shadow_storage_ok = st.ok();
    info_.shadow_storage_bytes = 0;
    info_.shadow_storage_bytes_by_volume.clear();
    for (const auto& s : storage) {
      info_.shadow_storage_bytes_by_volume.push_back(s.used_bytes);
      info_.shadow_storage_bytes += s.used_bytes;
    }
    if (!st.ok()) {
      ResetState();
      return st;
    }
  } else {
    info_.shadow_storage_ok = true;
  }

  st = impl_->StartSnapshotSet();
  if (!st.ok()) {
    ResetState();
    return st;
  }

  impl_->volumes.clear();
  for (const auto& spec : closure) {
    VolumeSnapshotEntry entry{};
    entry.spec = spec;
    st = impl_->AddVolume(spec, &entry.snapshot_id);
    if (!st.ok()) {
      ResetState();
      return st;
    }
    impl_->volumes.push_back(std::move(entry));
  }

  st = impl_->DoSnapshotSet();
  if (!st.ok()) {
    ResetState();
    return st;
  }

  info_.snapshot_set_id = GuidToString(impl_->snapshot_set_id);
  info_.consistency =
      (opts.mode == VssConsistencyMode::kApp) ? "app" : "crash";

  for (const auto& vol : impl_->volumes) {
    std::wstring device;
    st = impl_->GetSnapshotDeviceObject(vol.snapshot_id, &device);
    if (!st.ok()) {
      ResetState();
      return st;
    }
    VssVolumeMap map{};
    map.mount_prefix = NormalizeVolumeMountPrefix(Utf8ToWide(vol.spec.mount_point_utf8));
    map.shadow_prefix = NormalizeDevicePrefix(device.c_str());
    if (map.mount_prefix.empty() || map.shadow_prefix.empty()) {
      ResetState();
      return Status::Internal("failed to normalize VSS volume map");
    }
    volume_maps_.push_back(std::move(map));
    info_.volumes.push_back(vol.spec.volume_name_utf8);
  }

  active_ = true;
  return Status::Ok();
}

Status VssSession::FinishBackup() {
  if (!active_ || backup_finished_) return Status::Ok();
  if (!impl_) return Status::Internal("VSS session missing");

  const bool wants_app = requested_mode_ == VssConsistencyMode::kApp ||
                         requested_mode_ == VssConsistencyMode::kAuto;
  if (wants_app) {
    bool any_unstable = false;
    const Status ws_st = impl_->GatherWriterStatus(&info_.writers, &any_unstable);
    if (!ws_st.ok() && requested_mode_ == VssConsistencyMode::kApp) {
      return ws_st;
    }
    if (any_unstable || !ws_st.ok()) {
      if (requested_mode_ == VssConsistencyMode::kAuto) {
        info_.consistency = "crash";
      } else {
        info_.consistency = "app_degraded";
      }
    } else {
      info_.consistency = "app";
    }
  }

  const Status bc_st = impl_->BackupComplete();
  if (!bc_st.ok()) return bc_st;
  backup_finished_ = true;
  return Status::Ok();
}

std::string VssSession::MapToShadow(const std::string& logical_utf8) const {
  if (!active_ || volume_maps_.empty()) return logical_utf8;
  return MapPathWithVolumeMaps(logical_utf8, volume_maps_, true);
}

std::string VssSession::MapToLogicalForReport(const std::string& path_utf8) const {
  if (!active_ || volume_maps_.empty()) return path_utf8;
  return MapPathWithVolumeMaps(path_utf8, volume_maps_, false);
}

Status VssSession::End() {
  if (!active_) {
    ResetState();
    return Status::Ok();
  }

  if (!backup_finished_) {
    (void)FinishBackup();
  }

  Status st = impl_->DeleteSnapshots();
  ResetState();
  return st;
}

}  // namespace winmeta
}  // namespace ebbackup

#endif
