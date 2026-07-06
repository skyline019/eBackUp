#include "ebbackup/io/file_meta.h"

#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#endif

namespace ebbackup {

Status ApplyFileMeta(const std::string& path, const ManifestFileEntry& entry) {
  if (entry.mode == 0 && entry.mtime_unix == 0 && entry.atime_unix == 0) {
    return Status::Ok();
  }
#ifdef _WIN32
  DWORD attrs = GetFileAttributesA(path.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return Status::IoError("GetFileAttributes failed: " + path);
  }
  if (entry.mode != 0) {
    const bool readonly = (entry.mode & 0200) == 0;
    if (readonly) {
      attrs |= FILE_ATTRIBUTE_READONLY;
    } else {
      attrs &= ~FILE_ATTRIBUTE_READONLY;
    }
    if (!SetFileAttributesA(path.c_str(), attrs)) {
      return Status::IoError("SetFileAttributes failed: " + path);
    }
  }
  if (entry.mtime_unix != 0) {
    HANDLE h = CreateFileA(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) {
      return Status::IoError("CreateFile for times failed: " + path);
    }
    FILETIME ft;
    const int64_t windows_ticks = (entry.mtime_unix + 11644473600LL) * 10000000LL;
    ft.dwLowDateTime = static_cast<DWORD>(windows_ticks & 0xFFFFFFFF);
    ft.dwHighDateTime = static_cast<DWORD>(windows_ticks >> 32);
    if (!SetFileTime(h, nullptr, nullptr, &ft)) {
      CloseHandle(h);
      return Status::IoError("SetFileTime failed: " + path);
    }
    CloseHandle(h);
  }
#else
  if (entry.mode != 0) {
    if (chmod(path.c_str(), static_cast<mode_t>(entry.mode)) != 0) {
      return Status::IoError("chmod failed: " + path);
    }
  }
  if (entry.mtime_unix != 0 || entry.atime_unix != 0) {
    struct utimbuf times {};
    times.actime = entry.atime_unix != 0 ? entry.atime_unix : entry.mtime_unix;
    times.modtime = entry.mtime_unix;
    if (utime(path.c_str(), &times) != 0) {
      return Status::IoError("utime failed: " + path);
    }
  }
  if (entry.uid != 0xFFFFFFFFu || entry.gid != 0xFFFFFFFFu) {
    const uid_t uid = entry.uid != 0xFFFFFFFFu ? entry.uid : static_cast<uid_t>(-1);
    const gid_t gid = entry.gid != 0xFFFFFFFFu ? entry.gid : static_cast<gid_t>(-1);
    if (chown(path.c_str(), uid, gid) != 0) {
      return Status::IoError("chown failed: " + path);
    }
  }
#endif
  return Status::Ok();
}

}  // namespace ebbackup
