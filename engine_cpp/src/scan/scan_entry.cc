#include "ebbackup/scan/scan_entry.h"

#include <chrono>
#include <filesystem>
#include <unordered_set>

#include "ebbackup/common/path_encoding.h"
#include "ebbackup/common/path_util.h"
#include "ebbackup/scan/scan_hint_options.h"
#include "ebbackup/winmeta/win_meta.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace ebbackup {

namespace {

constexpr int kMaxScanDepth = 64;

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

void RecordIssue(const std::string& path, const std::string& reason,
                 std::vector<report::BackupPathIssue>* issues) {
  if (!issues) return;
  issues->push_back({path, reason});
}

#ifdef _WIN32
bool IsReparseDirectory(const std::filesystem::path& path) {
  const DWORD attrs =
      GetFileAttributesW(Utf8ToWide(PathToUtf8(path)).c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return false;
  return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
         (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool IsLockedError(const std::error_code& ec) {
  return ec.value() == ERROR_SHARING_VIOLATION ||
         ec.value() == ERROR_LOCK_VIOLATION;
}
#endif

struct WalkFrame {
  std::filesystem::path dir;
  int depth{0};
};

Status PopulateEntryFields(const std::filesystem::directory_entry& entry,
                           const std::string& source_root, ScanEntry* item,
                           std::vector<report::BackupPathIssue>* issues) {
  item->absolute_path = PathToUtf8(entry.path());
#ifdef _WIN32
  const Status rel_st = RelativePathFromRootNoFollow(
      source_root, item->absolute_path, &item->relative_path);
#else
  const Status rel_st =
      RelativePathFromRoot(source_root, item->absolute_path, &item->relative_path);
#endif
  if (!rel_st.ok()) return rel_st;
  item->type = ClassifyEntry(entry);
#ifdef _WIN32
  if (IsReparseDirectory(entry.path())) {
    item->type = FileType::kDirectory;
  }
#endif
  FillStatFields(entry, item);

  if (item->type == FileType::kSymlink) {
    std::error_code lec;
    item->symlink_target =
        PathToUtf8(std::filesystem::read_symlink(entry.path(), lec));
    if (lec) {
      RecordIssue(item->absolute_path, "unreadable", issues);
      item->size = 0;
    } else {
      item->size = item->symlink_target.size();
    }
  } else if (item->type == FileType::kDirectory) {
    item->size = 0;
  } else if (item->type == FileType::kRegular) {
    std::error_code fec;
    item->size = entry.file_size(fec);
    if (fec) {
#ifdef _WIN32
      if (IsLockedError(fec)) {
        RecordIssue(item->absolute_path, "locked", issues);
      } else {
        RecordIssue(item->absolute_path, "unreadable", issues);
      }
#else
      RecordIssue(item->absolute_path, "unreadable", issues);
#endif
      item->size = 0;
    }
  } else {
    item->size = 0;
  }
  return Status::Ok();
}

}  // namespace

ManifestFileEntry ScanEntry::ToManifestSkeleton() const {
  ManifestFileEntry entry = ManifestEntryFromScanMeta(
      relative_path, type, size, mode, uid, gid, mtime_unix, atime_unix,
      symlink_target, device_major, device_minor);
  entry.security_descriptor_b64 = security_descriptor_b64;
  entry.inode_id = inode_id;
  entry.reparse_tag = reparse_tag;
  entry.reparse_target = reparse_target;
  entry.stream_name = stream_name;
  return entry;
}

Status ScanDirectory(const std::string& source_root, ScanResult* out,
                     const ScanHintOptions* hint_opts) {
  if (!out) return Status::InvalidArgument("out is null");
  out->entries.clear();
  out->issues.clear();

  std::error_code ec;
  const auto root = PathFromUtf8(source_root);
  if (!std::filesystem::exists(root, ec)) {
    return Status::NotFound("source path not found");
  }

  const ScanHintOptions empty_hints{};
  const ScanHintOptions& hints = hint_opts ? *hint_opts : empty_hints;

  const auto options =
      std::filesystem::directory_options::skip_permission_denied;

  std::filesystem::directory_entry root_entry(root, ec);
  if (!ec) {
    if (!ShouldSkipScanPath(PathToUtf8(root), hints)) {
      ScanEntry root_item{};
      const Status root_st =
          PopulateEntryFields(root_entry, source_root, &root_item, &out->issues);
      if (!root_st.ok()) return root_st;
      out->entries.push_back(std::move(root_item));
    }
  }

  std::vector<WalkFrame> stack;
  stack.push_back({root, 0});
  std::unordered_set<std::string> visited_dirs;

  while (!stack.empty()) {
    const WalkFrame frame = stack.back();
    stack.pop_back();

    const std::string frame_key = PathToUtf8(frame.dir);
    if (ShouldSkipScanPath(frame_key, hints)) continue;
    if (!visited_dirs.insert(frame_key).second) {
      RecordIssue(frame_key, "symlink_loop", &out->issues);
      continue;
    }

    std::filesystem::directory_iterator it(frame.dir, options, ec);
    if (ec) {
      if (ec == std::errc::permission_denied) {
        RecordIssue(frame_key, "permission_denied", &out->issues);
      } else {
        RecordIssue(frame_key, "unreadable", &out->issues);
      }
      continue;
    }

    for (const auto& entry : it) {
      const std::string abs = PathToUtf8(entry.path());
      if (ShouldSkipScanPath(abs, hints)) continue;
      ScanEntry item{};
      const Status pop_st =
          PopulateEntryFields(entry, source_root, &item, &out->issues);
      if (!pop_st.ok()) return pop_st;
      out->entries.push_back(std::move(item));
      ScanEntry& added = out->entries.back();

      if (added.type != FileType::kDirectory) continue;

#ifdef _WIN32
      if (IsReparseDirectory(entry.path())) {
        RecordIssue(added.absolute_path, "reparse_junction", &out->issues);
        const Status cap = winmeta::CaptureWinMetaFromPath(added.absolute_path, &added);
        if (!cap.ok()) {
          RecordIssue(added.absolute_path, "unreadable", &out->issues);
        }
        continue;
      }
#endif

      if (frame.depth + 1 >= kMaxScanDepth) {
        RecordIssue(added.absolute_path, "depth_exceeded", &out->issues);
        continue;
      }
      stack.push_back({entry.path(), frame.depth + 1});
    }
  }

#ifdef _WIN32
  const Status enrich_st = EnrichScanEntriesWinMeta(&out->entries, &out->issues);
  if (!enrich_st.ok()) return enrich_st;
#endif
  return Status::Ok();
}

#ifdef _WIN32
namespace {

std::string WideToUtf8StreamName(const wchar_t* name) {
  if (!name) return "";
  std::wstring w(name);
  if (w == L"::$DATA") return "";
  if (!w.empty() && w[0] == L':') {
    w = w.substr(1);
  }
  const std::wstring data_suffix = L":$DATA";
  if (w.size() > data_suffix.size() &&
      w.compare(w.size() - data_suffix.size(), data_suffix.size(),
                data_suffix) == 0) {
    w = w.substr(0, w.size() - data_suffix.size());
  }
  return WideToUtf8(w);
}

Status AppendAlternateStreams(const ScanEntry& base, std::vector<ScanEntry>* out,
                              std::vector<report::BackupPathIssue>* issues) {
  const std::wstring wide = Utf8ToWide(base.absolute_path);
  WIN32_FIND_STREAM_DATA data{};
  HANDLE h = FindFirstStreamW(wide.c_str(), FindStreamInfoStandard, &data, 0);
  if (h == INVALID_HANDLE_VALUE) return Status::Ok();

  do {
    const std::string stream = WideToUtf8StreamName(data.cStreamName);
    if (stream.empty()) continue;
    ScanEntry ads = base;
    ads.stream_name = stream;
    ads.relative_path = base.relative_path + ":" + stream;
    ads.absolute_path = base.absolute_path + ":" + stream;
    ads.size = (static_cast<uint64_t>(data.StreamSize.HighPart) << 32) |
               data.StreamSize.LowPart;
    ads.type = FileType::kRegular;
    const Status cap = winmeta::CaptureWinMetaFromPath(ads.absolute_path, &ads);
    if (!cap.ok()) {
      RecordIssue(ads.absolute_path, "unreadable", issues);
      continue;
    }
    out->push_back(std::move(ads));
  } while (FindNextStreamW(h, &data));

  FindClose(h);
  return Status::Ok();
}

}  // namespace

Status EnrichScanEntriesWinMeta(std::vector<ScanEntry>* entries,
                                std::vector<report::BackupPathIssue>* issues) {
  if (!entries) return Status::InvalidArgument("entries is null");
  const size_t base_count = entries->size();
  for (size_t i = 0; i < base_count; ++i) {
    ScanEntry& e = (*entries)[i];
    const Status cap = winmeta::CaptureWinMetaFromPath(e.absolute_path, &e);
    if (!cap.ok()) {
      RecordIssue(e.absolute_path, "unreadable", issues);
      continue;
    }
    if (e.type == FileType::kRegular && e.stream_name.empty()) {
      const Status ads_st = AppendAlternateStreams(e, entries, issues);
      if (!ads_st.ok()) return ads_st;
    }
  }
  return Status::Ok();
}
#endif

}  // namespace ebbackup
