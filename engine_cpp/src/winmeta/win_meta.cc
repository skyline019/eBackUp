#include "ebbackup/winmeta/win_meta.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

#include "ebbackup/common/path_encoding.h"
#include "ebbackup/scan/scan_entry.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <aclapi.h>
#include <winioctl.h>

#ifndef IO_REPARSE_TAG_MOUNT_POINT
#define IO_REPARSE_TAG_MOUNT_POINT (0xA0000003L)
#endif

#ifndef MAXIMUM_REPARSE_DATA_BUFFER_SIZE
#define MAXIMUM_REPARSE_DATA_BUFFER_SIZE (16 * 1024)
#endif

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
#endif

namespace ebbackup {
namespace winmeta {

namespace {

#ifdef _WIN32
std::string Base64Encode(const uint8_t* data, size_t len) {
  static const char kTable[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3) {
    const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                       ((i + 1 < len ? data[i + 1] : 0) << 8) |
                       (i + 2 < len ? data[i + 2] : 0);
    out.push_back(kTable[(n >> 18) & 63]);
    out.push_back(kTable[(n >> 12) & 63]);
    out.push_back(i + 1 < len ? kTable[(n >> 6) & 63] : '=');
    out.push_back(i + 2 < len ? kTable[n & 63] : '=');
  }
  return out;
}

std::vector<uint8_t> Base64Decode(const std::string& in) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::vector<uint8_t> out;
  int buf = 0;
  int bits = 0;
  for (char c : in) {
    if (c == '=' || c == '\n' || c == '\r') continue;
    const int v = val(c);
    if (v < 0) continue;
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<uint8_t>((buf >> bits) & 0xFF));
    }
  }
  return out;
}

Status ReadSecurityDescriptorB64(const std::wstring& wide, std::string* out_b64) {
  if (!out_b64) return Status::InvalidArgument("out_b64 is null");
  out_b64->clear();
  const SECURITY_INFORMATION si =
      OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;
  DWORD len = 0;
  GetFileSecurityW(wide.c_str(), si, nullptr, 0, &len);
  if (len == 0) return Status::IoError("GetFileSecurity size query failed");
  std::vector<uint8_t> buf(len);
  if (!GetFileSecurityW(wide.c_str(), si, buf.data(), len, &len)) {
    return Status::IoError("GetFileSecurity failed");
  }
  *out_b64 = Base64Encode(buf.data(), len);
  return Status::Ok();
}

Status ApplySecurityDescriptorB64(const std::wstring& wide, const std::string& b64) {
  if (b64.empty()) return Status::Ok();
  std::vector<uint8_t> raw = Base64Decode(b64);
  if (raw.empty()) return Status::Corrupt("invalid security descriptor base64");
  if (!SetFileSecurityW(
          wide.c_str(),
          OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
          reinterpret_cast<PSECURITY_DESCRIPTOR>(raw.data()))) {
    return Status::IoError("SetFileSecurity failed");
  }
  return Status::Ok();
}

std::wstring NormalizeSubstituteName(std::wstring wpath) {
  if (wpath.rfind(L"\\??\\", 0) == 0) {
    wpath = wpath.substr(4);
  } else if (wpath.rfind(L"\\DosDevices\\", 0) == 0) {
    wpath = wpath.substr(12);
  }
  return wpath;
}

Status CaptureReparseTarget(HANDLE h, uint32_t reparse_tag, std::string* out_utf8) {
  if (!out_utf8) return Status::InvalidArgument("out_utf8 is null");
  out_utf8->clear();
  if (reparse_tag == 0) return Status::Ok();

  std::vector<uint8_t> buf(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
  DWORD ret = 0;
  if (!DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, nullptr, 0, buf.data(),
                       static_cast<DWORD>(buf.size()), &ret, nullptr)) {
    return Status::IoError("FSCTL_GET_REPARSE_POINT failed");
  }
  const auto* rdb = reinterpret_cast<const EbReparseDataBuffer*>(buf.data());
  if (rdb->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
    const auto* path_buf = reinterpret_cast<const uint8_t*>(rdb->PathBuffer);
    const auto* sub_name =
        reinterpret_cast<const WCHAR*>(path_buf + rdb->SubstituteNameOffset);
    const size_t sub_chars = rdb->SubstituteNameLength / sizeof(WCHAR);
    std::wstring substitute(sub_name, sub_chars);
    *out_utf8 = WideToUtf8(NormalizeSubstituteName(std::move(substitute)));
  }
  return Status::Ok();
}

std::wstring ToAbsoluteWide(const std::wstring& path) {
  if (path.size() >= 3 && path[1] == L':' && path[2] == L'\\') {
    return path;
  }
  wchar_t buf[MAX_PATH]{};
  const DWORD n = GetFullPathNameW(path.c_str(), MAX_PATH, buf, nullptr);
  if (n == 0 || n >= MAX_PATH) return path;
  return std::wstring(buf);
}
#endif

}  // namespace

void CopyWinMetaToManifest(const WinMetaExtension& win, ManifestFileEntry* entry) {
  if (!entry) return;
  entry->security_descriptor_b64 = win.security_descriptor_b64;
  entry->inode_id = win.inode_id;
  entry->reparse_tag = win.reparse_tag;
  entry->reparse_target = win.reparse_target;
  entry->stream_name = win.stream_name;
}

void CopyWinMetaFromManifest(const ManifestFileEntry& entry, WinMetaExtension* out) {
  if (!out) return;
  out->security_descriptor_b64 = entry.security_descriptor_b64;
  out->inode_id = entry.inode_id;
  out->reparse_tag = entry.reparse_tag;
  out->reparse_target = entry.reparse_target;
  out->stream_name = entry.stream_name;
}

Status ReadWinMetaFromEntry(const ManifestFileEntry& entry,
                            WinMetaExtension* out) {
  if (!out) return Status::InvalidArgument("out is null");
  CopyWinMetaFromManifest(entry, out);
  return Status::Ok();
}

Status CaptureWinMetaFromPath(const std::string& absolute_path, ScanEntry* entry) {
  if (!entry) return Status::InvalidArgument("entry is null");
#ifdef _WIN32
  const std::wstring wide = Utf8ToWide(absolute_path);
  HANDLE h = CreateFileW(
      wide.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return Status::IoError("CreateFile for win meta failed: " + absolute_path);
  }

  BY_HANDLE_FILE_INFORMATION info{};
  if (GetFileInformationByHandle(h, &info)) {
    entry->inode_id =
        (static_cast<uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
  }

  FILE_ATTRIBUTE_TAG_INFO tag_info{};
  if (GetFileInformationByHandleEx(h, FileAttributeTagInfo, &tag_info,
                                   sizeof(tag_info))) {
    if (tag_info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
      entry->reparse_tag = tag_info.ReparseTag;
      const Status target_st =
          CaptureReparseTarget(h, entry->reparse_tag, &entry->reparse_target);
      CloseHandle(h);
      if (!target_st.ok()) return target_st;
      (void)ReadSecurityDescriptorB64(wide, &entry->security_descriptor_b64);
      return Status::Ok();
    }
  }
  CloseHandle(h);

  return ReadSecurityDescriptorB64(wide, &entry->security_descriptor_b64);
#else
  (void)absolute_path;
#endif
  return Status::Ok();
}

Status ApplyWinMetaOnRestore(const std::string& dest_path,
                             const ManifestFileEntry& entry,
                             AclRestorePolicy policy,
                             std::string* soft_issue_reason) {
#ifdef _WIN32
  WinMetaExtension win{};
  CopyWinMetaFromManifest(entry, &win);
  const std::wstring wide = Utf8ToWide(dest_path);

  const bool apply_sd =
      (policy.mode == AclRestorePolicy::Mode::kPreserve ||
       policy.mode == AclRestorePolicy::Mode::kBestEffort) &&
      !win.security_descriptor_b64.empty();
  if (apply_sd) {
    const Status sd_st = ApplySecurityDescriptorB64(wide, win.security_descriptor_b64);
    if (!sd_st.ok()) {
      if (policy.mode == AclRestorePolicy::Mode::kBestEffort) {
        if (soft_issue_reason) *soft_issue_reason = "acl_apply_failed";
        return Status::Ok();
      }
      return sd_st;
    }
  }
#else
  (void)dest_path;
  (void)entry;
  (void)policy;
  (void)soft_issue_reason;
#endif
  return Status::Ok();
}

Status CreateHardLinkUtf8(const std::string& existing_path,
                          const std::string& link_path) {
#ifdef _WIN32
  const std::wstring existing = Utf8ToWide(existing_path);
  const std::wstring link = Utf8ToWide(link_path);
  if (!CreateHardLinkW(link.c_str(), existing.c_str(), nullptr)) {
    return Status::IoError("CreateHardLink failed");
  }
  return Status::Ok();
#else
  (void)existing_path;
  (void)link_path;
  return Status::Internal("hard links are Windows-only");
#endif
}

Status RecreateReparsePoint(const std::string& reparse_path,
                            const ManifestFileEntry& entry) {
#ifdef _WIN32
  if (entry.reparse_tag == 0 || entry.reparse_target.empty()) {
    return Status::InvalidArgument("reparse entry missing tag or target");
  }
  if (entry.reparse_tag != IO_REPARSE_TAG_MOUNT_POINT) {
    return Status::Internal("only junction mount points are supported");
  }

  const std::wstring wide = Utf8ToWide(reparse_path);
  std::wstring target = Utf8ToWide(entry.reparse_target);
  if (target.rfind(L"\\??\\", 0) == 0) {
    target = target.substr(4);
  }
  if (target.size() >= 3 && target[1] == L':') {
    const auto restore_parent = std::filesystem::path(wide).parent_path();
    const auto target_name = std::filesystem::path(target).filename();
    target = ToAbsoluteWide((restore_parent / target_name).wstring());
  } else {
    const auto restore_parent = std::filesystem::path(wide).parent_path();
    target = ToAbsoluteWide((restore_parent / std::filesystem::path(entry.reparse_target)).wstring());
  }

  std::error_code ec;
  const std::filesystem::path parent = std::filesystem::path(wide).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) return Status::IoError("cannot create reparse parent: " + ec.message());
  }
  if (std::filesystem::exists(wide, ec)) {
    std::filesystem::remove(wide, ec);
  }

  auto cmd_path = [](std::string p) {
    for (char& c : p) {
      if (c == '/') c = '\\';
    }
    return p;
  };
  const std::string target_utf8 = WideToUtf8(target);
  const std::string cmd =
      "cmd /c mklink /J \"" + cmd_path(reparse_path) + "\" \"" +
      cmd_path(target_utf8) + "\"";
  if (std::system(cmd.c_str()) == 0) {
    return Status::Ok();
  }
  if (!std::filesystem::is_directory(target_utf8)) {
    return Status::IoError("junction target missing: " + target_utf8);
  }

  if (!CreateDirectoryW(wide.c_str(), nullptr)) {
    const DWORD err = GetLastError();
    if (err != ERROR_ALREADY_EXISTS) {
      return Status::IoError("CreateDirectory for reparse failed");
    }
  }

  const std::wstring target_abs = target;
  const std::wstring substitute = L"\\??\\" + target_abs;
  const std::wstring print_name = target_abs;

  const USHORT sub_bytes = static_cast<USHORT>(substitute.size() * sizeof(WCHAR));
  const USHORT print_bytes = static_cast<USHORT>(print_name.size() * sizeof(WCHAR));
  const USHORT path_buffer_bytes =
      sub_bytes + sizeof(WCHAR) + print_bytes + sizeof(WCHAR);
  const USHORT reparse_data_length =
      static_cast<USHORT>(8 + path_buffer_bytes);

  std::vector<uint8_t> buf(16 + path_buffer_bytes);
  auto* rdb = reinterpret_cast<EbReparseDataBuffer*>(buf.data());
  rdb->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
  rdb->ReparseDataLength = reparse_data_length;
  rdb->Reserved = 0;
  rdb->SubstituteNameOffset = 0;
  rdb->SubstituteNameLength = sub_bytes;
  rdb->PrintNameOffset = static_cast<USHORT>(sub_bytes + sizeof(WCHAR));
  rdb->PrintNameLength = print_bytes;

  auto* path_buf = rdb->PathBuffer;
  memcpy(path_buf, substitute.c_str(), sub_bytes);
  path_buf[sub_bytes / sizeof(WCHAR)] = L'\0';
  memcpy(reinterpret_cast<uint8_t*>(path_buf) + sub_bytes + sizeof(WCHAR),
         print_name.c_str(), print_bytes);
  path_buf[(sub_bytes + sizeof(WCHAR)) / sizeof(WCHAR) + print_bytes / sizeof(WCHAR)] =
      L'\0';

  HANDLE h = CreateFileW(wide.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                         FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                         nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return Status::IoError("CreateFile for reparse restore failed");
  }

  DWORD ret = 0;
  const BOOL ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, buf.data(),
                                  static_cast<DWORD>(buf.size()), nullptr, 0, &ret,
                                  nullptr);
  CloseHandle(h);
  if (!ok) {
    return Status::IoError(
        "junction recreate failed (mklink and FSCTL_SET_REPARSE_POINT)");
  }
  return Status::Ok();
#else
  (void)reparse_path;
  (void)entry;
  return Status::Internal("reparse restore is Windows-only");
#endif
}

}  // namespace winmeta
}  // namespace ebbackup
