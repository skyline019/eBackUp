#include "ebbackup/common/fsync.h"

#include <cstdio>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ebbackup {

Status FsyncPath(const std::string& path) {
#ifdef _WIN32
  int fd = -1;
  if (_sopen_s(&fd, path.c_str(), _O_RDWR | _O_BINARY, _SH_DENYNO,
               _S_IREAD | _S_IWRITE) != 0) {
    if (_sopen_s(&fd, path.c_str(), _O_RDONLY | _O_BINARY, _SH_DENYNO,
                 _S_IREAD) != 0) {
      return Status::IoError("cannot open for fsync: " + path);
    }
  }
  if (_commit(fd) != 0) {
    _close(fd);
    return Status::IoError("fsync failed: " + path);
  }
  _close(fd);
#else
  int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0) return Status::IoError("cannot open for fsync: " + path);
  if (fsync(fd) != 0) {
    close(fd);
    return Status::IoError("fsync failed: " + path);
  }
  close(fd);
#endif
  return Status::Ok();
}

}  // namespace ebbackup
