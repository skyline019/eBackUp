#include "ebbackup/io/mmap_reader.h"

#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ebbackup {

MmapReader::~MmapReader() { Close(); }

Status MmapReader::Open(const std::string& path) {
  Close();
#ifdef _WIN32
  HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return Status::IoError("mmap open failed: " + path);
  }
  LARGE_INTEGER file_size{};
  if (!GetFileSizeEx(file, &file_size)) {
    CloseHandle(file);
    return Status::IoError("mmap stat failed: " + path);
  }
  if (file_size.QuadPart == 0) {
    CloseHandle(file);
    size_ = 0;
    data_ = reinterpret_cast<const uint8_t*>("");
    return Status::Ok();
  }
  HANDLE mapping =
      CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
  if (!mapping) {
    CloseHandle(file);
    return Status::IoError("CreateFileMapping failed: " + path);
  }
  const void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
  if (!view) {
    CloseHandle(mapping);
    CloseHandle(file);
    return Status::IoError("MapViewOfFile failed: " + path);
  }
  file_handle_ = file;
  mapping_handle_ = mapping;
  data_ = static_cast<const uint8_t*>(view);
  size_ = static_cast<size_t>(file_size.QuadPart);
#else
  const int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0) return Status::IoError("mmap open failed: " + path);
  struct stat st {};
  if (fstat(fd, &st) != 0) {
    close(fd);
    return Status::IoError("mmap stat failed: " + path);
  }
  if (st.st_size == 0) {
    close(fd);
    size_ = 0;
    data_ = reinterpret_cast<const uint8_t*>("");
    return Status::Ok();
  }
  void* mapped =
      mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
  if (mapped == MAP_FAILED) {
    close(fd);
    return Status::IoError("mmap failed: " + path);
  }
  fd_ = fd;
  data_ = static_cast<const uint8_t*>(mapped);
  size_ = static_cast<size_t>(st.st_size);
#endif
  return Status::Ok();
}

void MmapReader::Close() {
#ifdef _WIN32
  if (data_ && size_ > 0) {
    UnmapViewOfFile(data_);
  }
  if (mapping_handle_) {
    CloseHandle(static_cast<HANDLE>(mapping_handle_));
    mapping_handle_ = nullptr;
  }
  if (file_handle_) {
    CloseHandle(static_cast<HANDLE>(file_handle_));
    file_handle_ = nullptr;
  }
#else
  if (data_ && size_ > 0) {
    munmap(const_cast<uint8_t*>(data_), size_);
  }
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
#endif
  data_ = nullptr;
  size_ = 0;
}

}  // namespace ebbackup
