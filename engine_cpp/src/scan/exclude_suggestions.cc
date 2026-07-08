#include "ebbackup/scan/exclude_suggestions.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <sstream>
#include <unordered_map>

#include "ebbackup/common/path_encoding.h"
#include "ebbackup/common/path_util.h"
#include "ebbackup/engine/restore_options_json.h"

namespace ebbackup {

namespace {

std::string NormalizeSlashes(const std::string& path) {
  std::string out = path;
  for (char& c : out) {
    if (c == '\\') c = '/';
  }
  return out;
}

std::string BaseName(const std::string& path) {
  const auto pos = path.find_last_of("/\\");
  return pos == std::string::npos ? path : path.substr(pos + 1);
}

bool StartsWithPath(const std::string& text, const std::string& prefix) {
  const std::string t = NormalizeSlashes(text);
  const std::string p = NormalizeSlashes(prefix);
  if (p.empty()) return true;
  if (t.size() < p.size()) return false;
  if (t.compare(0, p.size(), p) != 0) return false;
  if (t.size() == p.size()) return true;
  return t[p.size()] == '/';
}

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

bool PatternUsesFullPath(const std::string& pattern) {
  return pattern.find('/') != std::string::npos ||
         pattern.find('\\') != std::string::npos;
}

bool MatchPathGlob(const std::string& pattern, const std::string& relative_path) {
  const std::string norm_pat = NormalizeSlashes(pattern);
  if (PatternUsesFullPath(pattern)) {
    return GlobMatch(norm_pat, NormalizeSlashes(relative_path));
  }
  return GlobMatch(pattern, BaseName(relative_path));
}

bool EqualsIgnoreCase(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

struct CatalogEntry {
  const char* name;
  const char* kind;
  const char* reason;
  bool is_dir;
  bool skip_descend;
};

const CatalogEntry kDirCatalog[] = {
    {".git", "vcs_metadata", "Version control object database; rarely needed in backup",
     true, true},
    {".svn", "vcs_metadata", "Subversion metadata directory", true, true},
    {".hg", "vcs_metadata", "Mercurial metadata directory", true, true},
    {"node_modules", "package_cache", "Package manager cache; usually restored via lockfile",
     true, true},
    {"__pycache__", "language_cache", "Python bytecode cache; regenerated on run", true,
     true},
    {".pytest_cache", "language_cache", "Pytest cache directory", true, true},
    {".mypy_cache", "language_cache", "Mypy cache directory", true, true},
    {".venv", "virtual_env", "Python virtual environment", true, true},
    {"venv", "virtual_env", "Python virtual environment", true, true},
    {"target", "build_output", "Rust/Java build output directory", true, true},
    {"build", "build_output", "Build output directory", true, true},
    {"dist", "build_output", "Distribution/build artifacts", true, true},
    {"out", "build_output", "Build output directory", true, true},
    {".next", "build_output", "Next.js build cache and output", true, true},
    {".turbo", "build_output", "Turborepo cache directory", true, true},
    {".nuxt", "build_output", "Nuxt build output directory", true, true},
    {".output", "build_output", "Nuxt/Nitro output directory", true, true},
    {".gradle", "build_output", "Gradle cache and build directory", true, true},
    {".cargo", "build_output", "Rust cargo cache directory", true, true},
    {"bin", "build_output", "Compiled binary output directory", true, true},
    {"obj", "build_output", "Object file build directory", true, true},
    {".cache", "language_cache", "Tooling cache directory", true, true},
    {"coverage", "language_cache", "Test coverage output directory", true, true},
    {".idea", "ide_metadata", "JetBrains IDE project metadata", true, true},
    {".vscode", "ide_metadata", "VS Code workspace metadata", true, false},
};

const CatalogEntry kFileCatalog[] = {
    {"Thumbs.db", "os_metadata", "Windows thumbnail cache", false, false},
    {".DS_Store", "os_metadata", "macOS folder metadata", false, false},
    {"desktop.ini", "os_metadata", "Windows folder customization", false, false},
};

const CatalogEntry* FindDirCatalog(const std::string& name, bool include_ide) {
  for (const auto& e : kDirCatalog) {
    if (!include_ide && (std::string(e.kind) == "ide_metadata")) continue;
    if (EqualsIgnoreCase(name, e.name)) return &e;
  }
  return nullptr;
}

const CatalogEntry* FindFileCatalog(const std::string& name) {
  for (const auto& e : kFileCatalog) {
    if (EqualsIgnoreCase(name, e.name)) return &e;
  }
  return nullptr;
}

bool IsCoveredByExisting(const BackupFilterOptions* existing,
                         const std::string& apply_as,
                         const std::string& pattern,
                         const std::string& example_path) {
  if (!existing) return false;
  if (apply_as == "exclude_path") {
    for (const auto& p : existing->exclude_paths) {
      if (StartsWithPath(example_path, p) || StartsWithPath(pattern, p) ||
          StartsWithPath(p, pattern)) {
        return true;
      }
    }
  }
  for (const auto& g : existing->exclude_globs) {
    if (MatchPathGlob(g, example_path)) return true;
  }
  for (const auto& g : existing->name_globs) {
    if (MatchPathGlob(g, example_path)) return true;
  }
  return false;
}

void JsonEscape(const std::string& s, std::string* out) {
  if (!out) return;
  out->clear();
  for (char c : s) {
    switch (c) {
      case '\\':
        out->append("\\\\");
        break;
      case '"':
        out->append("\\\"");
        break;
      case '\n':
        out->append("\\n");
        break;
      case '\r':
        out->append("\\r");
        break;
      case '\t':
        out->append("\\t");
        break;
      default:
        out->push_back(c);
        break;
    }
  }
}

struct SuggestionKey {
  std::string apply_as;
  std::string pattern;
  bool operator==(const SuggestionKey& o) const {
    return apply_as == o.apply_as && pattern == o.pattern;
  }
};

struct SuggestionKeyHash {
  size_t operator()(const SuggestionKey& k) const {
    return std::hash<std::string>()(k.apply_as + '\0' + k.pattern);
  }
};

void AddSuggestion(ExcludeFilterSuggestions* out,
                   std::unordered_map<SuggestionKey, size_t, SuggestionKeyHash>* index,
                   const SuggestExcludeFiltersOptions& opts,
                   const std::string& apply_as, const std::string& pattern,
                   const std::string& kind, const std::string& reason,
                   const std::string& example_path) {
  if (!out || !index) return;
  if (IsCoveredByExisting(opts.existing, apply_as, pattern, example_path)) {
    return;
  }
  const SuggestionKey key{apply_as, pattern};
  const auto it = index->find(key);
  if (it != index->end()) {
    out->items[it->second].hit_count += 1;
    return;
  }
  if (out->items.size() >= opts.max_suggestions) return;

  ExcludeFilterSuggestion item{};
  item.apply_as = apply_as;
  item.pattern = pattern;
  item.kind = kind;
  item.reason = reason;
  item.example_path = example_path;
  item.hit_count = 1;
  (*index)[key] = out->items.size();
  out->items.push_back(std::move(item));

  if (apply_as == "exclude_path") {
    if (std::find(out->recommended.exclude_paths.begin(),
                  out->recommended.exclude_paths.end(),
                  pattern) == out->recommended.exclude_paths.end()) {
      out->recommended.exclude_paths.push_back(pattern);
    }
  } else if (std::find(out->recommended.exclude_globs.begin(),
                         out->recommended.exclude_globs.end(),
                         pattern) == out->recommended.exclude_globs.end()) {
    out->recommended.exclude_globs.push_back(pattern);
  }
}

void WalkShallow(const std::filesystem::path& root,
                 const std::filesystem::path& current, int depth,
                 const SuggestExcludeFiltersOptions& opts,
                 ExcludeFilterSuggestions* out,
                 std::unordered_map<SuggestionKey, size_t, SuggestionKeyHash>* index,
                 size_t* dirs_visited,
                 std::set<std::string>* visited) {
  if (!out || depth > opts.max_depth) return;
  if (*dirs_visited >= opts.max_dirs_visited) return;

  std::error_code ec;
  if (!std::filesystem::exists(current, ec)) return;

  const std::string rel =
      NormalizeSlashes(PathToUtf8(std::filesystem::relative(current, root, ec)));
  if (ec) return;

  if (std::filesystem::is_symlink(current, ec)) {
    const auto canon = std::filesystem::weakly_canonical(current, ec);
    if (!ec) {
      const std::string canon_str = PathToUtf8(canon);
      if (visited->count(canon_str)) return;
      visited->insert(canon_str);
    }
  }

  if (std::filesystem::is_regular_file(current, ec)) {
    const std::string name = BaseName(rel);
    if (const CatalogEntry* cat = FindFileCatalog(name)) {
      AddSuggestion(out, index, opts, "exclude_glob", cat->name, cat->kind,
                    cat->reason, rel);
    }
    if (EqualsIgnoreCase(name, ".pyc") ||
        (rel.find("__pycache__") != std::string::npos &&
         name.size() >= 4 && name.substr(name.size() - 4) == ".pyc")) {
      AddSuggestion(out, index, opts, "exclude_glob", "*.pyc", "language_cache",
                    "Python compiled bytecode", rel);
    }
    if (name.size() >= 4 &&
        EqualsIgnoreCase(name.substr(name.size() - 4), ".log")) {
      AddSuggestion(out, index, opts, "exclude_glob", "*.log", "temp_artifact",
                    "Log files; usually regenerated", rel);
    }
    return;
  }

  if (!std::filesystem::is_directory(current, ec)) return;
  ++(*dirs_visited);

  const std::string dir_name = BaseName(rel.empty() ? PathToUtf8(current) : rel);
  bool skip_descend = false;
  if (const CatalogEntry* cat = FindDirCatalog(dir_name, opts.include_ide_dirs)) {
    const std::string pattern = rel.empty() ? cat->name : rel;
    AddSuggestion(out, index, opts, "exclude_path", pattern, cat->kind, cat->reason,
                  pattern);
    skip_descend = cat->skip_descend;
  }

  if (skip_descend || depth >= opts.max_depth) return;

  for (const auto& entry :
       std::filesystem::directory_iterator(
           current, std::filesystem::directory_options::skip_permission_denied,
           ec)) {
    if (ec) continue;
    WalkShallow(root, entry.path(), depth + 1, opts, out, index, dirs_visited,
                visited);
    if (out->items.size() >= opts.max_suggestions) return;
  }
}

}  // namespace

Status SuggestExcludeFilters(const std::string& source_path,
                             const SuggestExcludeFiltersOptions& opts,
                             ExcludeFilterSuggestions* out) {
  if (!out) return Status::InvalidArgument("out is null");
  if (source_path.empty()) return Status::InvalidArgument("source_path empty");
  out->items.clear();
  out->recommended = BackupFilterOptions{};
  if (opts.existing) {
    out->recommended.exclude_paths = opts.existing->exclude_paths;
    out->recommended.exclude_globs = opts.existing->exclude_globs;
  }

  std::error_code ec;
  const std::filesystem::path root = PathFromUtf8(source_path);
  if (!std::filesystem::exists(root, ec)) {
    return Status::NotFound("source path not found: " + source_path);
  }
  if (!std::filesystem::is_directory(root, ec)) {
    return Status::InvalidArgument("source path is not a directory");
  }

  size_t dirs_visited = 0;
  std::set<std::string> visited;
  std::unordered_map<SuggestionKey, size_t, SuggestionKeyHash> index;
  WalkShallow(root, root, 0, opts, out, &index, &dirs_visited, &visited);
  return Status::Ok();
}

std::string ExcludeFilterSuggestionsToJson(const ExcludeFilterSuggestions& s) {
  std::ostringstream out;
  std::string esc;
  out << "{\"items\":[";
  for (size_t i = 0; i < s.items.size(); ++i) {
    if (i) out << ',';
    out << '{';
    JsonEscape(s.items[i].apply_as, &esc);
    out << "\"apply_as\":\"" << esc << "\"";
    JsonEscape(s.items[i].pattern, &esc);
    out << ",\"pattern\":\"" << esc << "\"";
    JsonEscape(s.items[i].kind, &esc);
    out << ",\"kind\":\"" << esc << "\"";
    JsonEscape(s.items[i].reason, &esc);
    out << ",\"reason\":\"" << esc << "\"";
    JsonEscape(s.items[i].example_path, &esc);
    out << ",\"example_path\":\"" << esc << "\"";
    out << ",\"hit_count\":" << s.items[i].hit_count << '}';
  }
  out << "],\"recommended\":{";
  out << "\"exclude_paths\":[";
  for (size_t i = 0; i < s.recommended.exclude_paths.size(); ++i) {
    if (i) out << ',';
    JsonEscape(s.recommended.exclude_paths[i], &esc);
    out << '"' << esc << '"';
  }
  out << "],\"exclude_globs\":[";
  for (size_t i = 0; i < s.recommended.exclude_globs.size(); ++i) {
    if (i) out << ',';
    JsonEscape(s.recommended.exclude_globs[i], &esc);
    out << '"' << esc << '"';
  }
  out << "]}}";
  return out.str();
}

}  // namespace ebbackup
