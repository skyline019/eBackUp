#include "ebbackup/io/fs_watch.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace ebbackup {

namespace {

int64_t LatestWriteUnix(const std::string& root) {
  int64_t latest = 0;
  std::error_code ec;
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(root, ec)) {
    if (ec) break;
    const auto ft = entry.last_write_time(ec);
    if (ec) continue;
    const auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
        ft - std::filesystem::file_time_type::clock::now() +
        std::chrono::system_clock::now());
    latest = (std::max)(latest, sctp.time_since_epoch().count());
  }
  return latest;
}

}  // namespace

FsWatch::FsWatch(std::string path) : path_(std::move(path)) {}

FsWatch::~FsWatch() {
  stop_.store(true);
#ifdef _WIN32
  if (dir_handle_ && dir_handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(static_cast<HANDLE>(dir_handle_));
  }
#else
  if (watch_fd_ >= 0 && inotify_fd_ >= 0) {
    inotify_rm_watch(inotify_fd_, watch_fd_);
  }
  if (inotify_fd_ >= 0) close(inotify_fd_);
#endif
}

Status FsWatch::Open() {
#ifdef _WIN32
  HANDLE h = CreateFileA(path_.c_str(), FILE_LIST_DIRECTORY,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return Status::IoError("CreateFile for watch failed: " + path_);
  }
  dir_handle_ = h;
  return Status::Ok();
#else
  inotify_fd_ = inotify_init1(IN_NONBLOCK);
  if (inotify_fd_ < 0) return Status::IoError("inotify_init failed");
  watch_fd_ = inotify_add_watch(
      inotify_fd_, path_.c_str(),
      IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_TO | IN_MOVED_FROM |
          IN_CLOSE_WRITE);
  if (watch_fd_ < 0) {
    close(inotify_fd_);
    inotify_fd_ = -1;
    return Status::IoError("inotify_add_watch failed");
  }
  return Status::Ok();
#endif
}

Status FsWatch::WaitForChangePoll(int debounce_ms) {
  int64_t last_seen = LatestWriteUnix(path_);
  while (!stop_.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    const int64_t now = LatestWriteUnix(path_);
    if (now <= last_seen) continue;
    std::this_thread::sleep_for(std::chrono::milliseconds(debounce_ms));
    const int64_t settled = LatestWriteUnix(path_);
    if (settled != now) {
      last_seen = settled;
      continue;
    }
    last_seen = settled;
    return Status::Ok();
  }
  return Status::Internal("watch stopped");
}

Status FsWatch::WaitForChangeNative(int debounce_ms) {
#ifdef _WIN32
  if (!dir_handle_ || dir_handle_ == INVALID_HANDLE_VALUE) {
    return WaitForChangePoll(debounce_ms);
  }
  char buffer[16 * 1024];
  DWORD bytes = 0;
  while (!stop_.load()) {
    bytes = 0;
    const BOOL ok = ReadDirectoryChangesW(
        static_cast<HANDLE>(dir_handle_), buffer, sizeof(buffer), TRUE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
        &bytes, nullptr, nullptr);
    if (!ok) {
      return WaitForChangePoll(debounce_ms);
    }
    if (bytes > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(debounce_ms));
      return Status::Ok();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  return Status::Internal("watch stopped");
#else
  if (inotify_fd_ < 0) return WaitForChangePoll(debounce_ms);
  while (!stop_.load()) {
    char buffer[4096];
    const ssize_t len = read(inotify_fd_, buffer, sizeof(buffer));
    if (len > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(debounce_ms));
      return Status::Ok();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  return Status::Internal("watch stopped");
#endif
}

Status FsWatch::WaitForChange(int debounce_ms) {
  return WaitForChangeNative(debounce_ms);
}

}  // namespace ebbackup
