#include "ebbackup/scan/backup_filter.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>

namespace ebbackup {

namespace {

bool GlobMatch(const std::string& pattern, const std::string& text) {
  size_t pi = 0;
  size_t ti = 0;
  size_t star_pi = std::string::npos;
  size_t star_ti = 0;
  while (ti < text.size()) {
    if (pi < pattern.size() &&
        (pattern[pi] == '?' || pattern[pi] == text[ti])) {
      ++pi;
      ++ti;
      continue;
    }
    if (pi < pattern.size() && pattern[pi] == '*') {
      star_pi = ++pi;
      star_ti = ti;
      continue;
    }
    if (star_pi != std::string::npos) {
      pi = star_pi;
      ti = ++star_ti;
      continue;
    }
    return false;
  }
  while (pi < pattern.size() && pattern[pi] == '*') ++pi;
  return pi == pattern.size();
}

std::string BaseName(const std::string& path) {
  const auto pos = path.find_last_of("/\\");
  return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string ExtensionOf(const std::string& path) {
  const std::string base = BaseName(path);
  const auto pos = base.find_last_of('.');
  if (pos == std::string::npos || pos + 1 >= base.size()) return "";
  return base.substr(pos + 1);
}

bool StartsWithPath(const std::string& text, const std::string& prefix) {
  if (prefix.empty()) return true;
  if (text.size() < prefix.size()) return false;
  if (text.compare(0, prefix.size(), prefix) != 0) return false;
  if (text.size() == prefix.size()) return true;
  const char next = text[prefix.size()];
  return next == '/' || next == '\\';
}

std::string Trim(const std::string& s) {
  size_t start = 0;
  while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) ++start;
  size_t end = s.size();
  while (end > start &&
         (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) {
    --end;
  }
  return s.substr(start, end - start);
}

std::string NormalizePathSlashes(const std::string& path) {
  std::string out = path;
  for (char& c : out) {
    if (c == '\\') c = '/';
  }
  return out;
}

bool PatternUsesFullPath(const std::string& pattern) {
  return pattern.find('/') != std::string::npos ||
         pattern.find('\\') != std::string::npos;
}

bool MatchPathGlob(const std::string& pattern, const std::string& relative_path) {
  const std::string norm_pat = NormalizePathSlashes(pattern);
  if (PatternUsesFullPath(pattern)) {
    return GlobMatch(norm_pat, NormalizePathSlashes(relative_path));
  }
  return GlobMatch(pattern, BaseName(relative_path));
}

const std::vector<std::string>& EffectiveExcludeGlobs(
    const BackupFilterOptions& filter) {
  if (!filter.exclude_globs.empty()) return filter.exclude_globs;
  return filter.name_globs;
}

}  // namespace

bool BackupFilterOptions::HasAnyFilter() const {
  return !include_paths.empty() || !exclude_paths.empty() ||
         !include_globs.empty() || !exclude_globs.empty() ||
         !name_globs.empty() || !extensions.empty() || min_size > 0 ||
         max_size != UINT64_MAX || mtime_after > 0 ||
         mtime_before != INT64_MAX || uid_filter != 0;
}

Status LoadBackupFilterFromFile(const std::string& path, BackupFilterOptions* out) {
  if (!out) return Status::InvalidArgument("out is null");
  std::ifstream in(path);
  if (!in) return Status::IoError("cannot open filter file: " + path);
  BackupFilterOptions filter{};
  std::string line;
  while (std::getline(in, line)) {
    line = Trim(line);
    if (line.empty() || line[0] == '#') continue;
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = Trim(line.substr(0, eq));
    const std::string value = Trim(line.substr(eq + 1));
    if (key == "include_path") {
      filter.include_paths.push_back(value);
    } else if (key == "exclude_path") {
      filter.exclude_paths.push_back(value);
    } else if (key == "include_glob") {
      filter.include_globs.push_back(value);
    } else if (key == "exclude_glob" || key == "name_glob") {
      filter.exclude_globs.push_back(value);
    } else if (key == "ext" || key == "extension") {
      filter.extensions.push_back(value);
    } else if (key == "min_size") {
      filter.min_size = std::strtoull(value.c_str(), nullptr, 10);
    } else if (key == "max_size") {
      filter.max_size = std::strtoull(value.c_str(), nullptr, 10);
    } else if (key == "mtime_after") {
      filter.mtime_after = std::strtoll(value.c_str(), nullptr, 10);
    } else if (key == "mtime_before") {
      filter.mtime_before = std::strtoll(value.c_str(), nullptr, 10);
    } else if (key == "uid") {
      filter.uid_filter = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
    }
  }
  *out = filter;
  return Status::Ok();
}

Status ApplyBackupFilter(const BackupFilterOptions& filter,
                         std::vector<ScanEntry>* entries) {
  if (!entries) return Status::InvalidArgument("entries is null");
  if (!filter.HasAnyFilter()) return Status::Ok();

  const auto& exclude_globs = EffectiveExcludeGlobs(filter);
  std::vector<ScanEntry> kept;
  kept.reserve(entries->size());
  for (const auto& e : *entries) {
    if (!filter.include_paths.empty()) {
      bool matched = false;
      for (const auto& p : filter.include_paths) {
        if (StartsWithPath(e.relative_path, p)) {
          matched = true;
          break;
        }
      }
      if (!matched) continue;
    }
    bool excluded = false;
    for (const auto& p : filter.exclude_paths) {
      if (StartsWithPath(e.relative_path, p)) {
        excluded = true;
        break;
      }
    }
    if (excluded) continue;

    if (!filter.include_globs.empty()) {
      bool matched = false;
      for (const auto& g : filter.include_globs) {
        if (MatchPathGlob(g, e.relative_path)) {
          matched = true;
          break;
        }
      }
      if (!matched) continue;
    }

    if (!exclude_globs.empty()) {
      bool glob_excluded = false;
      for (const auto& g : exclude_globs) {
        if (MatchPathGlob(g, e.relative_path)) {
          glob_excluded = true;
          break;
        }
      }
      if (glob_excluded) continue;
    }

    if (!filter.extensions.empty()) {
      const std::string ext = ExtensionOf(e.relative_path);
      bool matched = false;
      for (const auto& want : filter.extensions) {
        std::string w = want;
        if (!w.empty() && w[0] == '.') w = w.substr(1);
        if (w.empty()) continue;
        std::string got = ext;
        for (auto& c : got) c = static_cast<char>(std::tolower(c));
        for (auto& c : w) c = static_cast<char>(std::tolower(c));
        if (got == w) {
          matched = true;
          break;
        }
      }
      if (!matched) continue;
    }

    if (e.type == FileType::kRegular) {
      if (e.size < filter.min_size || e.size > filter.max_size) continue;
    }
    if (e.mtime_unix < filter.mtime_after || e.mtime_unix > filter.mtime_before) {
      continue;
    }
    if (filter.uid_filter != 0 && e.uid != filter.uid_filter) continue;

    kept.push_back(e);
  }
  *entries = std::move(kept);
  return Status::Ok();
}

ScanEntry ManifestEntryToScanEntry(const ManifestFileEntry& entry) {
  ScanEntry se{};
  se.relative_path = entry.relative_path;
  se.type = entry.file_type;
  se.size = entry.size;
  se.mode = entry.mode;
  se.uid = entry.uid;
  se.gid = entry.gid;
  se.mtime_unix = entry.mtime_unix;
  se.atime_unix = entry.atime_unix;
  se.symlink_target = entry.symlink_target;
  se.device_major = entry.device_major;
  se.device_minor = entry.device_minor;
  return se;
}

namespace {

void AddAncestorPaths(const std::string& rel, std::set<std::string>* paths) {
  size_t pos = rel.find_last_of('/');
  while (pos != std::string::npos) {
    const std::string parent = rel.substr(0, pos);
    if (parent.empty()) break;
    paths->insert(parent);
    pos = parent.find_last_of('/');
  }
}

}  // namespace

Status ApplyManifestFilter(const BackupFilterOptions& filter,
                           const std::vector<ManifestFileEntry>& all_files,
                           std::vector<ManifestFileEntry>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  if (!filter.HasAnyFilter()) {
    *out = all_files;
    return Status::Ok();
  }

  std::vector<ScanEntry> entries;
  entries.reserve(all_files.size());
  for (const auto& file : all_files) {
    entries.push_back(ManifestEntryToScanEntry(file));
  }
  const Status st = ApplyBackupFilter(filter, &entries);
  if (!st.ok()) return st;

  std::set<std::string> kept_paths;
  for (const auto& e : entries) {
    kept_paths.insert(e.relative_path);
    AddAncestorPaths(e.relative_path, &kept_paths);
  }

  std::vector<ManifestFileEntry> filtered;
  filtered.reserve(kept_paths.size());
  for (const auto& file : all_files) {
    if (kept_paths.count(file.relative_path) > 0) {
      filtered.push_back(file);
    }
  }
  *out = std::move(filtered);
  return Status::Ok();
}

}  // namespace ebbackup
