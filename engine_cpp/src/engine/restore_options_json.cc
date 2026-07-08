#include "ebbackup/engine/restore_options_json.h"

#include <cctype>
#include <cstring>
#include <vector>

namespace ebbackup {

namespace {

void SkipWs(const std::string& s, size_t* i) {
  while (*i < s.size() && std::isspace(static_cast<unsigned char>(s[*i]))) ++(*i);
}

bool MatchLiteral(const std::string& s, size_t* i, const char* lit) {
  const size_t n = std::strlen(lit);
  if (s.compare(*i, n, lit) != 0) return false;
  *i += n;
  return true;
}

Status ParseJsonString(const std::string& s, size_t* i, std::string* out) {
  SkipWs(s, i);
  if (*i >= s.size() || s[*i] != '"') {
    return Status::InvalidArgument("expected string in json");
  }
  ++(*i);
  std::string value;
  while (*i < s.size()) {
    const char c = s[*i];
    if (c == '"') {
      ++(*i);
      *out = std::move(value);
      return Status::Ok();
    }
    if (c == '\\') {
      ++(*i);
      if (*i >= s.size()) return Status::InvalidArgument("bad json escape");
      const char e = s[(*i)++];
      switch (e) {
        case '"':
        case '\\':
        case '/':
          value += e;
          break;
        case 'n':
          value += '\n';
          break;
        case 'r':
          value += '\r';
          break;
        case 't':
          value += '\t';
          break;
        default:
          return Status::InvalidArgument("unsupported json escape");
      }
      continue;
    }
    value += c;
    ++(*i);
  }
  return Status::InvalidArgument("unterminated json string");
}

Status ParseStringArray(const std::string& s, size_t* i,
                        std::vector<std::string>* out) {
  SkipWs(s, i);
  if (*i >= s.size() || s[*i] != '[') {
    return Status::InvalidArgument("expected json array");
  }
  ++(*i);
  SkipWs(s, i);
  if (*i < s.size() && s[*i] == ']') {
    ++(*i);
    return Status::Ok();
  }
  while (*i < s.size()) {
    std::string item;
    const Status st = ParseJsonString(s, i, &item);
    if (!st.ok()) return st;
    out->push_back(item);
    SkipWs(s, i);
    if (*i < s.size() && s[*i] == ',') {
      ++(*i);
      continue;
    }
    if (*i < s.size() && s[*i] == ']') {
      ++(*i);
      return Status::Ok();
    }
    return Status::InvalidArgument("bad json array");
  }
  return Status::InvalidArgument("unterminated json array");
}

bool FindJsonKey(const std::string& s, const char* key, size_t* pos) {
  const std::string needle = std::string("\"") + key + "\":";
  size_t p = 0;
  while ((p = s.find(needle, p)) != std::string::npos) {
    if (p == 0 || s[p - 1] == '{' || s[p - 1] == ',' || std::isspace(static_cast<unsigned char>(s[p - 1]))) {
      *pos = p;
      return true;
    }
    p += needle.size();
  }
  return false;
}

Status ReadStringField(const std::string& s, const char* key, std::string* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  size_t pos = 0;
  if (!FindJsonKey(s, key, &pos)) return Status::Ok();
  pos += std::string("\"").size() + std::strlen(key) + 2;  // skip "key":
  return ParseJsonString(s, &pos, out);
}

Status ReadStringArrayField(const std::string& s, const char* key,
                            std::vector<std::string>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  size_t pos = 0;
  if (!FindJsonKey(s, key, &pos)) return Status::Ok();
  pos += std::string("\"").size() + std::strlen(key) + 2;
  return ParseStringArray(s, &pos, out);
}

RestoreConflictPolicy ParseConflict(const std::string& s) {
  if (s == "skip") return RestoreConflictPolicy::kSkip;
  if (s == "suffix") return RestoreConflictPolicy::kSuffix;
  return RestoreConflictPolicy::kFail;
}

RestoreLayoutMode ParseLayoutMode(const std::string& s) {
  if (s == "strip_prefix") return RestoreLayoutMode::kStripPrefix;
  if (s == "flatten") return RestoreLayoutMode::kFlatten;
  if (s == "remap_prefix") return RestoreLayoutMode::kRemapPrefix;
  return RestoreLayoutMode::kKeep;
}

winmeta::AclRestorePolicy::Mode ParseAclMode(const std::string& s) {
  if (s == "preserve") return winmeta::AclRestorePolicy::Mode::kPreserve;
  if (s == "skip") return winmeta::AclRestorePolicy::Mode::kSkip;
  return winmeta::AclRestorePolicy::Mode::kInherit;
}

}  // namespace

Status ReadJsonStringField(const std::string& s, const char* key, std::string* out) {
  return ReadStringField(s, key, out);
}

Status ReadJsonStringArrayField(const std::string& s, const char* key,
                                std::vector<std::string>* out) {
  return ReadStringArrayField(s, key, out);
}

bool ParseU64Field(const std::string& json, const char* key, uint64_t* out) {
  if (!out) return false;
  const std::string needle = std::string("\"") + key + "\":";
  const size_t pos = json.find(needle);
  if (pos == std::string::npos) return false;
  size_t i = pos + needle.size();
  while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
  size_t j = i;
  while (j < json.size() && (std::isdigit(static_cast<unsigned char>(json[j])) ||
                             json[j] == '.' || json[j] == '-')) {
    ++j;
  }
  *out = static_cast<uint64_t>(std::strtoull(json.substr(i, j - i).c_str(), nullptr, 10));
  return true;
}

bool ParseIntField(const std::string& json, const char* key, int* out) {
  if (!out) return false;
  const std::string needle = std::string("\"") + key + "\":";
  const size_t pos = json.find(needle);
  if (pos == std::string::npos) return false;
  size_t i = pos + needle.size();
  while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
  size_t j = i;
  while (j < json.size() &&
         (std::isdigit(static_cast<unsigned char>(json[j])) || json[j] == '-' ||
          json[j] == '+')) {
    ++j;
  }
  *out = std::atoi(json.substr(i, j - i).c_str());
  return true;
}

bool ParseBoolField(const std::string& json, const char* key, bool* out) {
  if (!out) return false;
  const std::string needle = std::string("\"") + key + "\":";
  const size_t pos = json.find(needle);
  if (pos == std::string::npos) return false;
  size_t i = pos + needle.size();
  while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
  if (json.compare(i, 4, "true") == 0) {
    *out = true;
    return true;
  }
  if (json.compare(i, 5, "false") == 0) {
    *out = false;
    return true;
  }
  return false;
}

Status ReadJsonU64Field(const std::string& json, const char* key, uint64_t* out) {
  if (!out) return Status::InvalidArgument("out is null");
  if (!ParseU64Field(json, key, out)) return Status::Ok();
  return Status::Ok();
}

Status ReadJsonIntField(const std::string& json, const char* key, int* out) {
  if (!out) return Status::InvalidArgument("out is null");
  if (!ParseIntField(json, key, out)) return Status::Ok();
  return Status::Ok();
}

Status ReadJsonBoolField(const std::string& json, const char* key, bool* out) {
  if (!out) return Status::InvalidArgument("out is null");
  if (!ParseBoolField(json, key, out)) return Status::Ok();
  return Status::Ok();
}

bool TryReadJsonU64Field(const std::string& json, const char* key, uint64_t* out) {
  return ParseU64Field(json, key, out);
}

bool TryReadJsonIntField(const std::string& json, const char* key, int* out) {
  return ParseIntField(json, key, out);
}

Status ParseBackupFilterJson(const std::string& json, BackupFilterOptions* out) {
  if (!out) return Status::InvalidArgument("out is null");
  BackupFilterOptions filter{};
  const Status st1 = ReadStringArrayField(json, "include_paths", &filter.include_paths);
  if (!st1.ok()) return st1;
  const Status st2 = ReadStringArrayField(json, "exclude_paths", &filter.exclude_paths);
  if (!st2.ok()) return st2;
  const Status st3 = ReadStringArrayField(json, "include_globs", &filter.include_globs);
  if (!st3.ok()) return st3;
  const Status st4 = ReadStringArrayField(json, "exclude_globs", &filter.exclude_globs);
  if (!st4.ok()) return st4;
  const Status st5 = ReadStringArrayField(json, "extensions", &filter.extensions);
  if (!st5.ok()) return st5;
  *out = std::move(filter);
  return Status::Ok();
}

Status ParseRestoreRemapJson(const std::string& json, RestorePathRemap* out) {
  if (!out) return Status::InvalidArgument("out is null");
  RestorePathRemap remap{};
  std::string mode;
  std::string conflict;
  const Status st1 = ReadStringField(json, "mode", &mode);
  if (!st1.ok()) return st1;
  const Status st2 = ReadStringField(json, "strip_prefix", &remap.strip_prefix);
  if (!st2.ok()) return st2;
  const Status st3 = ReadStringField(json, "map_from", &remap.map_from);
  if (!st3.ok()) return st3;
  const Status st4 = ReadStringField(json, "map_to", &remap.map_to);
  if (!st4.ok()) return st4;
  const Status st5 = ReadStringField(json, "conflict", &conflict);
  if (!st5.ok()) return st5;
  remap.mode = ParseLayoutMode(mode);
  remap.conflict = ParseConflict(conflict);
  *out = std::move(remap);
  return Status::Ok();
}

Status ParseSymlinkRemapJson(const std::string& json, SymlinkRemap* out) {
  if (!out) return Status::InvalidArgument("out is null");
  SymlinkRemap remap{};
  const Status st1 = ReadJsonStringField(json, "symlink_remap_from", &remap.map_from);
  if (!st1.ok()) return st1;
  const Status st2 = ReadJsonStringField(json, "symlink_remap_to", &remap.map_to);
  if (!st2.ok()) return st2;
  if (remap.map_from.empty()) {
    const Status st3 = ReadJsonStringField(json, "map_from", &remap.map_from);
    if (!st3.ok()) return st3;
    const Status st4 = ReadJsonStringField(json, "map_to", &remap.map_to);
    if (!st4.ok()) return st4;
  }
  *out = std::move(remap);
  return Status::Ok();
}

Status ParseRestoreAclPolicyJson(const std::string& json,
                                 winmeta::AclRestorePolicy* out) {
  if (!out) return Status::InvalidArgument("out is null");
  std::string acl;
  const Status st = ReadJsonStringField(json, "acl_policy", &acl);
  if (!st.ok()) return st;
  if (!acl.empty()) {
    if (acl == "preserve") {
      out->mode = winmeta::AclRestorePolicy::Mode::kPreserve;
    } else if (acl == "skip") {
      out->mode = winmeta::AclRestorePolicy::Mode::kSkip;
    } else if (acl == "best_effort") {
      out->mode = winmeta::AclRestorePolicy::Mode::kBestEffort;
    } else {
      out->mode = winmeta::AclRestorePolicy::Mode::kInherit;
    }
  }
  return Status::Ok();
}

Status ParseRestoreReparsePolicyJson(const std::string& json,
                                     winmeta::ReparseRestorePolicy* out) {
  if (!out) return Status::InvalidArgument("out is null");
  std::string reparse;
  const Status st = ReadJsonStringField(json, "reparse_policy", &reparse);
  if (!st.ok()) return st;
  if (reparse == "recreate") {
    out->mode = winmeta::ReparseRestorePolicy::Mode::kRecreate;
  } else {
    out->mode = winmeta::ReparseRestorePolicy::Mode::kSkip;
  }
  return Status::Ok();
}

}  // namespace ebbackup
