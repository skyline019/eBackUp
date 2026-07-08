#include "ebbackup/scan/scan_hint_options.h"

#include "ebbackup/common/path_encoding.h"
#include "ebbackup/common/path_util.h"

namespace ebbackup {
namespace {

std::string NormalizePathKey(const std::string& path) {
  std::string out = path;
  for (char& c : out) {
    if (c == '\\') c = '/';
  }
#ifdef _WIN32
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
#endif
  return out;
}

bool PathHasPrefix(const std::string& path, const std::string& prefix) {
  const std::string p = NormalizePathKey(path);
  const std::string pre = NormalizePathKey(prefix);
  if (p.size() < pre.size()) return false;
  if (p.compare(0, pre.size(), pre) != 0) return false;
  if (p.size() == pre.size()) return true;
  const char next = p[pre.size()];
  return next == '/';
}

}  // namespace

bool ShouldSkipScanPath(const std::string& absolute_path,
                        const ScanHintOptions& opts) {
  for (const auto& hint : opts.hints) {
    if (!hint.skip_subtree) continue;
    if (PathHasPrefix(absolute_path, hint.path_prefix)) return true;
  }
  return false;
}

}  // namespace ebbackup
