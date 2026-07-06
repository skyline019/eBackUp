#include "ebbackup/scan/scan_entry.h"

#include <chrono>
#include <filesystem>

#include "ebbackup/common/path_util.h"

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace ebbackup {

namespace {

FileType ClassifyEntry(const std::filesystem::directory_entry& entry) {
  std::error_code ec;
  if (entry.is_directory(ec)) return FileType::kDirectory;
  if (entry.is_symlink(ec)) return FileType::kSymlink;
  if (entry.is_regular_file(ec)) return FileType::kRegular;
#ifndef _WIN32
  if (entry.is_fifo(ec)) return FileType::kFifo;
  if (entry.is_block_file(ec)) return FileType::kBlock;
  if (entry.is_character_file(ec)) return FileType::kChar;
#endif
  return FileType::kRegular;
}

int64_t FileTimeToUnix(const std::filesystem::file_time_type& ft) {
  const auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
      ft - std::filesystem::file_time_type::clock::now() +
      std::chrono::system_clock::now());
  return sctp.time_since_epoch().count();
}

void FillStatFields(const std::filesystem::directory_entry& entry,
                    ScanEntry* out) {
  std::error_code ec;
  const auto status = entry.symlink_status(ec);
  if (!ec) {
    out->mode = static_cast<uint32_t>(status.permissions());
  }
  const auto ftime = entry.last_write_time(ec);
  if (!ec) {
    out->mtime_unix = FileTimeToUnix(ftime);
  }
#ifndef _WIN32
  struct stat st {};
  if (stat(entry.path().string().c_str(), &st) == 0) {
    out->mode = static_cast<uint32_t>(st.st_mode);
    out->uid = static_cast<uint32_t>(st.st_uid);
    out->gid = static_cast<uint32_t>(st.st_gid);
    out->mtime_unix = st.st_mtime;
    out->atime_unix = st.st_atime;
#if defined(major) && defined(minor)
    if (S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode)) {
      out->device_major = static_cast<uint32_t>(major(st.st_rdev));
      out->device_minor = static_cast<uint32_t>(minor(st.st_rdev));
    }
#endif
  }
#else
  (void)status;
#endif
}

}  // namespace

ManifestFileEntry ScanEntry::ToManifestSkeleton() const {
  return ManifestEntryFromScanMeta(relative_path, type, size, mode, uid, gid,
                                   mtime_unix, atime_unix, symlink_target,
                                   device_major, device_minor);
}

Status ScanDirectory(const std::string& source_root,
                     std::vector<ScanEntry>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  std::error_code ec;
  const auto options = std::filesystem::directory_options::skip_permission_denied;
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(source_root, options, ec)) {
    if (ec) return Status::IoError("scan failed: " + ec.message());
    ScanEntry item{};
    item.absolute_path = entry.path().string();
    const Status rel_st =
        RelativePathFromRoot(source_root, item.absolute_path, &item.relative_path);
    if (!rel_st.ok()) return rel_st;
    item.type = ClassifyEntry(entry);
    FillStatFields(entry, &item);

    if (item.type == FileType::kSymlink) {
      std::error_code lec;
      item.symlink_target =
          std::filesystem::read_symlink(entry.path(), lec).string();
      if (lec) return Status::IoError("read symlink failed: " + item.absolute_path);
      item.size = item.symlink_target.size();
    } else if (item.type == FileType::kDirectory) {
      item.size = 0;
    } else if (item.type == FileType::kRegular) {
      std::error_code fec;
      item.size = entry.file_size(fec);
      if (fec) return Status::IoError("file size failed: " + item.absolute_path);
    } else {
      item.size = 0;
    }
    out->push_back(std::move(item));
  }
  return Status::Ok();
}

}  // namespace ebbackup
