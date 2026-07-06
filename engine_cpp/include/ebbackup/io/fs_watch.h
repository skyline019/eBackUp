#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

#include "ebbackup/common/status.h"

namespace ebbackup {

class FsWatch {
 public:
  explicit FsWatch(std::string path);
  ~FsWatch();

  FsWatch(const FsWatch&) = delete;
  FsWatch& operator=(const FsWatch&) = delete;

  Status Open();
  Status WaitForChange(int debounce_ms);

 private:
  Status WaitForChangeNative(int debounce_ms);
  Status WaitForChangePoll(int debounce_ms);

  std::string path_;
#ifdef _WIN32
  void* dir_handle_{nullptr};
#else
  int inotify_fd_{-1};
  int watch_fd_{-1};
#endif
  std::atomic<bool> stop_{false};
};

}  // namespace ebbackup
