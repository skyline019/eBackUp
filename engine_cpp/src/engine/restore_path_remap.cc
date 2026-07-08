#include "ebbackup/engine/restore_path_remap.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "ebbackup/common/path_util.h"

namespace ebbackup {

namespace {

bool StartsWithPath(const std::string& text, const std::string& prefix) {
  if (prefix.empty()) return true;
  if (text.size() < prefix.size()) return false;
  if (text.compare(0, prefix.size(), prefix) != 0) return false;
  if (text.size() == prefix.size()) return true;
  const char next = text[prefix.size()];
  return next == '/';
}

std::string BaseName(const std::string& path) {
  const auto pos = path.find_last_of('/');
  return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string JoinPath(const std::string& head, const std::string& tail) {
  if (head.empty()) return tail;
  if (tail.empty()) return head;
  if (head.back() == '/') return head + tail;
  return head + '/' + tail;
}

Status ValidateDestRelativePath(const std::string& rel) {
  if (rel.empty()) {
    return Status::InvalidArgument("restore dest path is empty");
  }
  if (rel[0] == '/') {
    return Status::InvalidArgument("restore dest path must be relative");
  }
  size_t i = 0;
  while (i < rel.size()) {
    const size_t slash = rel.find('/', i);
    const std::string segment =
        slash == std::string::npos ? rel.substr(i) : rel.substr(i, slash - i);
    if (segment == "..") {
      return Status::InvalidArgument("restore dest path contains ..");
    }
    if (segment.empty() && slash != std::string::npos) {
      return Status::InvalidArgument("restore dest path has empty segment");
    }
    if (slash == std::string::npos) break;
    i = slash + 1;
  }
  return Status::Ok();
}

}  // namespace

Status ResolveDestRelativePath(const std::string& manifest_rel,
                                 FileType file_type,
                                 const RestorePathRemap& remap,
                                 ResolvedDestPath* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->skip = false;
  out->path.clear();

  const std::string norm = NormalizeRepoPath(manifest_rel);
  if (remap.mode == RestoreLayoutMode::kKeep) {
    out->path = norm;
    return ValidateDestRelativePath(out->path);
  }

  if (remap.mode == RestoreLayoutMode::kStripPrefix) {
    if (remap.strip_prefix.empty()) {
      return Status::InvalidArgument("strip_prefix is required");
    }
    const std::string prefix = NormalizeRepoPath(remap.strip_prefix);
    if (!StartsWithPath(norm, prefix)) {
      return Status::InvalidArgument("path not under strip_prefix: " + norm);
    }
    std::string tail = norm.substr(prefix.size());
    while (!tail.empty() && tail[0] == '/') tail.erase(0, 1);
    if (tail.empty()) {
      if (file_type == FileType::kDirectory) {
        out->skip = true;
        return Status::Ok();
      }
      return Status::InvalidArgument("strip yields empty path");
    }
    out->path = tail;
    return ValidateDestRelativePath(out->path);
  }

  if (remap.mode == RestoreLayoutMode::kFlatten) {
    if (file_type == FileType::kDirectory) {
      out->skip = true;
      return Status::Ok();
    }
    const std::string base = BaseName(norm);
    if (base.empty()) {
      return Status::InvalidArgument("flatten yields empty basename");
    }
    out->path = base;
    return ValidateDestRelativePath(out->path);
  }

  if (remap.mode == RestoreLayoutMode::kRemapPrefix) {
    if (remap.map_from.empty()) {
      return Status::InvalidArgument("map_from is required");
    }
    const std::string from = NormalizeRepoPath(remap.map_from);
    if (!StartsWithPath(norm, from)) {
      out->path = norm;
      return ValidateDestRelativePath(out->path);
    }
    std::string tail = norm.substr(from.size());
    while (!tail.empty() && tail[0] == '/') tail.erase(0, 1);
    const std::string to = NormalizeRepoPath(remap.map_to);
    if (to.empty()) {
      out->path = tail;
    } else if (tail.empty()) {
      out->path = to;
    } else {
      out->path = JoinPath(to, tail);
    }
    return ValidateDestRelativePath(out->path);
  }

  return Status::InvalidArgument("unknown restore layout mode");
}

Status AssignDestPathWithConflict(
    const std::string& candidate, RestoreConflictPolicy conflict,
    std::unordered_map<std::string, uint32_t>* seen, std::string* assigned) {
  if (!seen || !assigned) return Status::InvalidArgument("null out param");
  const auto it = seen->find(candidate);
  if (it == seen->end()) {
    (*seen)[candidate] = 1;
    *assigned = candidate;
    return Status::Ok();
  }

  if (conflict == RestoreConflictPolicy::kFail) {
    return Status::Corrupt("restore path conflict: " + candidate);
  }
  if (conflict == RestoreConflictPolicy::kSkip) {
    assigned->clear();
    return Status::Ok();
  }

  const std::string base = BaseName(candidate);
  const std::string dir =
      candidate.size() > base.size() + 1
          ? candidate.substr(0, candidate.size() - base.size() - 1)
          : std::string();
  uint32_t n = ++(*seen)[candidate];
  const uint32_t suffix = n > 0 ? n - 1 : 0;
  std::string stem = base;
  std::string ext;
  const auto dot = base.find_last_of('.');
  if (dot != std::string::npos && dot > 0) {
    stem = base.substr(0, dot);
    ext = base.substr(dot);
  }
  std::string next = dir.empty() ? stem + "_" + std::to_string(suffix) + ext
                                 : JoinPath(dir, stem + "_" + std::to_string(suffix) + ext);
  while (seen->count(next) > 0) {
    ++n;
    const uint32_t bump = n > 0 ? n - 1 : 0;
    next = dir.empty() ? stem + "_" + std::to_string(bump) + ext
                       : JoinPath(dir, stem + "_" + std::to_string(bump) + ext);
  }
  (*seen)[candidate] = n;
  (*seen)[next] = 1;
  *assigned = next;
  return Status::Ok();
}

std::vector<std::string> CollapseIncludePaths(
    const std::vector<std::string>& paths) {
  std::set<std::string> unique;
  for (const auto& p : paths) {
    if (p.empty()) continue;
    unique.insert(NormalizeRepoPath(p));
  }
  std::vector<std::string> sorted(unique.begin(), unique.end());
  std::sort(sorted.begin(), sorted.end(), [](const std::string& a,
                                             const std::string& b) {
    const auto depth = [](const std::string& s) {
      return static_cast<int>(
          std::count(s.begin(), s.end(), static_cast<char>('/')) + 1);
    };
    const int da = depth(a);
    const int db = depth(b);
    if (da != db) return da < db;
    return a < b;
  });

  std::vector<std::string> result;
  for (const auto& p : sorted) {
    bool covered = false;
    for (const auto& q : result) {
      if (StartsWithPath(p, q)) {
        covered = true;
        break;
      }
    }
    if (covered) continue;
    result.erase(std::remove_if(result.begin(), result.end(),
                                [&](const std::string& q) {
                                  return StartsWithPath(q, p);
                                }),
                 result.end());
    result.push_back(p);
  }
  return result;
}

std::string ApplySymlinkTargetRemap(const std::string& target,
                                    const SymlinkRemap& remap) {
  if (!remap.HasRemap()) return target;
  std::string norm = target;
  for (char& c : norm) {
    if (c == '\\') c = '/';
  }
  std::string from = remap.map_from;
  for (char& c : from) {
    if (c == '\\') c = '/';
  }
  while (!from.empty() && from.back() == '/') from.pop_back();
  if (from.empty()) return target;
  if (!StartsWithPath(norm, from)) return target;
  std::string tail = norm.substr(from.size());
  while (!tail.empty() && tail[0] == '/') tail.erase(0, 1);
  std::string to = remap.map_to;
  for (char& c : to) {
    if (c == '\\') c = '/';
  }
  while (!to.empty() && to.back() == '/') to.pop_back();
  if (to.empty()) return tail;
  if (tail.empty()) return to;
  return JoinPath(to, tail);
}

}  // namespace ebbackup
