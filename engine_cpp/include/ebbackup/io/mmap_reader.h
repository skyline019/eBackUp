#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "ebbackup/common/status.h"

namespace ebbackup {

class MmapReader {
 public:
  MmapReader() = default;
  ~MmapReader();

  MmapReader(const MmapReader&) = delete;
  MmapReader& operator=(const MmapReader&) = delete;
  MmapReader(MmapReader&& other) noexcept;
  MmapReader& operator=(MmapReader&& other) noexcept;

  Status Open(const std::string& path);
  void Close();

  const uint8_t* data() const { return data_; }
  size_t size() const { return size_; }
  bool is_open() const { return data_ != nullptr; }

 private:
  const uint8_t* data_{nullptr};
  size_t size_{0};
#ifdef _WIN32
  void* file_handle_{nullptr};
  void* mapping_handle_{nullptr};
#else
  int fd_{-1};
#endif
};

}  // namespace ebbackup
