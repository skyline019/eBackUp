#include "ebbackup/winmeta/vss_volume_closure.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <winioctl.h>

#include <deque>
#include <set>
#include <vector>

#include "ebbackup/winmeta/vss_shadow_storage.h"
#include "ebbackup/common/path_encoding.h"
#include "ebbackup/common/path_util.h"

namespace ebbackup {
namespace winmeta {
namespace {

struct EbReparseDataBuffer {
  ULONG ReparseTag;
  USHORT ReparseDataLength;
  USHORT Reserved;
  USHORT SubstituteNameOffset;
  USHORT SubstituteNameLength;
  USHORT PrintNameOffset;
  USHORT PrintNameLength;
  WCHAR PathBuffer[1];
};

std::wstring NormalizeSubstituteName(std::wstring wpath) {
  if (wpath.rfind(L"\\??\\", 0) == 0) {
    wpath = wpath.substr(4);
  } else if (wpath.rfind(L"\\DosDevices\\", 0) == 0) {
    wpath = wpath.substr(12);
  }
  return wpath;
}

Status ReadJunctionTargetUtf8(const std::wstring& wide_path, std::string* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  HANDLE h = CreateFileW(
      wide_path.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return Status::IoError("CreateFile reparse failed");
  }
  std::vector<uint8_t> buf(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
  DWORD ret = 0;
  if (!DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, nullptr, 0, buf.data(),
                       static_cast<DWORD>(buf.size()), &ret, nullptr)) {
    CloseHandle(h);
    return Status::IoError("FSCTL_GET_REPARSE_POINT failed");
  }
  CloseHandle(h);
  const auto* rdb = reinterpret_cast<const EbReparseDataBuffer*>(buf.data());
  if (rdb->ReparseTag != IO_REPARSE_TAG_MOUNT_POINT) {
    return Status::Ok();
  }
  const auto* path_buf = reinterpret_cast<const uint8_t*>(rdb->PathBuffer);
  const auto* sub_name =
      reinterpret_cast<const WCHAR*>(path_buf + rdb->SubstituteNameOffset);
  const size_t sub_chars = rdb->SubstituteNameLength / sizeof(WCHAR);
  std::wstring substitute(sub_name, sub_chars);
  *out = WideToUtf8(NormalizeSubstituteName(std::move(substitute)));
  return Status::Ok();
}

void ProbeJunctionVolumes(const std::string& root,
                          const VssVolumeClosureOptions& opts,
                          std::set<std::string>* volume_keys,
                          std::vector<VssVolumeSpec>* out) {
  if (!volume_keys || !out || opts.junction_probe_depth == 0) return;
  struct Frame {
    std::wstring dir;
    uint32_t depth{0};
  };
  std::deque<Frame> q;
  q.push_back({Utf8ToWide(root), 0});
  while (!q.empty()) {
    const Frame frame = q.front();
    q.pop_front();
    if (frame.depth >= opts.junction_probe_depth) continue;

    std::wstring pattern = frame.dir;
    if (!pattern.empty() && pattern.back() != L'\\') pattern.push_back(L'\\');
    pattern += L'*';

    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) continue;
    do {
      if (data.cFileName[0] == L'.' &&
          (data.cFileName[1] == L'\0' ||
           (data.cFileName[1] == L'.' && data.cFileName[2] == L'\0'))) {
        continue;
      }
      std::wstring child = frame.dir;
      if (!child.empty() && child.back() != L'\\') child.push_back(L'\\');
      child += data.cFileName;

      if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
          const std::string child_utf8 = WideToUtf8(child);
          std::string target;
          if (ReadJunctionTargetUtf8(child, &target).ok() && !target.empty()) {
            VssVolumeSpec spec{};
            if (ResolveVolumeForPath(target, &spec).ok()) {
              if (volume_keys->insert(spec.volume_name_utf8).second) {
                out->push_back(std::move(spec));
              }
            }
          }
        } else {
          q.push_back({child, frame.depth + 1});
        }
      }
    } while (FindNextFileW(find, &data));
    FindClose(find);
  }
}

}  // namespace

Status ResolveVolumeForPath(const std::string& logical_path,
                            VssVolumeSpec* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->volume_name_utf8.clear();
  out->mount_point_utf8.clear();
  if (logical_path.empty()) {
    return Status::InvalidArgument("empty logical path");
  }

  wchar_t mount[MAX_PATH + 1]{};
  const std::wstring wide = Utf8ToWide(logical_path);
  if (!GetVolumePathNameW(wide.c_str(), mount, MAX_PATH)) {
    return Status::IoError("GetVolumePathNameW failed: " + logical_path);
  }
  wchar_t volume_name[MAX_PATH + 1]{};
  if (!GetVolumeNameForVolumeMountPointW(mount, volume_name, MAX_PATH)) {
    return Status::IoError("GetVolumeNameForVolumeMountPointW failed");
  }
  out->mount_point_utf8 = WideToUtf8(mount);
  out->volume_name_utf8 = WideToUtf8(volume_name);
  return Status::Ok();
}

Status ComputeVolumeClosure(const std::vector<std::string>& logical_roots,
                            const VssVolumeClosureOptions& opts,
                            std::vector<VssVolumeSpec>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  std::set<std::string> seen;
  for (const std::string& root : logical_roots) {
    if (root.empty()) continue;
    VssVolumeSpec spec{};
    const Status st = ResolveVolumeForPath(root, &spec);
    if (!st.ok()) return st;
    if (seen.insert(spec.volume_name_utf8).second) {
      out->push_back(std::move(spec));
    }
    if (opts.include_junction_volumes) {
      ProbeJunctionVolumes(root, opts, &seen, out);
    }
  }
  if (out->empty()) {
    return Status::InvalidArgument("no volumes in closure");
  }
  return Status::Ok();
}

Status CheckShadowStoragePreflight(const std::vector<VssVolumeSpec>& volumes,
                                   uint64_t min_free_bytes) {
  return CheckShadowStoragePreflightEx(volumes, min_free_bytes, nullptr);
}

}  // namespace winmeta
}  // namespace ebbackup

#endif
