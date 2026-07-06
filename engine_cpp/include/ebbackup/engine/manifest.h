#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/chunk/cfi_index.h"
#include "ebbackup/common/status.h"

namespace ebbackup {

enum class FileType : uint8_t {
  kRegular = 0,
  kDirectory = 1,
  kSymlink = 2,
  kFifo = 3,
  kBlock = 4,
  kChar = 5,
};

constexpr uint32_t kBackupFeatureMeta = 0x01;
constexpr uint32_t kBackupFeatureSpecialFiles = 0x02;
constexpr uint32_t kBackupFeatureEncrypted = 0x04;

struct ManifestFileEntry {
  std::string relative_path;
  uint64_t size{0};
  std::vector<std::string> chunk_hashes_hex;
  CfiIndex cfi;

  FileType file_type{FileType::kRegular};
  uint32_t mode{0};
  uint32_t uid{0xFFFFFFFFu};
  uint32_t gid{0xFFFFFFFFu};
  int64_t mtime_unix{0};
  int64_t atime_unix{0};
  std::string symlink_target;
  uint32_t device_major{0};
  uint32_t device_minor{0};

  bool has_extended_meta() const {
    return file_type != FileType::kRegular || mode != 0 || mtime_unix != 0 ||
           atime_unix != 0 || uid != 0xFFFFFFFFu || gid != 0xFFFFFFFFu;
  }

  bool needs_chunking() const { return file_type == FileType::kRegular; }
};

struct ManifestDocument {
  uint64_t txn_id{0};
  std::vector<ManifestFileEntry> files;
};

const char* FileTypeToString(FileType type);
FileType FileTypeFromString(const std::string& s);

Status WriteManifestV2(const std::string& path, const ManifestDocument& doc);
Status WriteManifestV3(const std::string& path, const ManifestDocument& doc);
Status WriteManifestAuto(const std::string& path, const ManifestDocument& doc);
Status ReadManifestAuto(const std::string& path, ManifestDocument* out);
Status ComputeManifestBodyCrc32(const std::string& body, uint32_t* out);

ManifestFileEntry ManifestEntryFromScanMeta(const std::string& relative_path,
                                            FileType type, uint64_t size,
                                            uint32_t mode, uint32_t uid,
                                            uint32_t gid, int64_t mtime,
                                            int64_t atime,
                                            const std::string& symlink_target,
                                            uint32_t dev_major,
                                            uint32_t dev_minor);

}  // namespace ebbackup
