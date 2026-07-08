#include "ebsync/sync_state.h"

#include "ebsync/pds/pds_client.h"

#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ebsync {
namespace {

void SkipWs(const std::string& s, size_t* i) {
  while (*i < s.size() && std::isspace(static_cast<unsigned char>(s[*i]))) ++(*i);
}

bool ParseJsonString(const std::string& s, size_t* i, std::string* out) {
  SkipWs(s, i);
  if (*i >= s.size() || s[*i] != '"') return false;
  ++(*i);
  std::string value;
  while (*i < s.size()) {
    const char c = s[*i];
    if (c == '"') {
      ++(*i);
      *out = std::move(value);
      return true;
    }
    if (c == '\\') {
      ++(*i);
      if (*i >= s.size()) return false;
      value += s[(*i)++];
      continue;
    }
    value += c;
    ++(*i);
  }
  return false;
}

bool ParseJsonUint64(const std::string& s, size_t* i, uint64_t* out) {
  SkipWs(s, i);
  if (*i >= s.size()) return false;
  size_t start = *i;
  while (*i < s.size() && std::isdigit(static_cast<unsigned char>(s[*i]))) ++(*i);
  if (*i == start) return false;
  try {
    *out = std::stoull(s.substr(start, *i - start));
    return true;
  } catch (...) {
    return false;
  }
}

bool ParseJsonInt64(const std::string& s, size_t* i, int64_t* out) {
  SkipWs(s, i);
  if (*i >= s.size()) return false;
  size_t start = *i;
  if (s[*i] == '-') ++(*i);
  while (*i < s.size() && std::isdigit(static_cast<unsigned char>(s[*i]))) ++(*i);
  if (*i == start || (start + 1 == *i && s[start] == '-')) return false;
  try {
    *out = std::stoll(s.substr(start, *i - start));
    return true;
  } catch (...) {
    return false;
  }
}

bool ParseJsonBool(const std::string& s, size_t* i, bool* out) {
  SkipWs(s, i);
  if (s.compare(*i, 4, "true") == 0) {
    *i += 4;
    *out = true;
    return true;
  }
  if (s.compare(*i, 5, "false") == 0) {
    *i += 5;
    *out = false;
    return true;
  }
  return false;
}

void JsonEscape(const std::string& s, std::string* out) {
  *out += '"';
  for (char c : s) {
    switch (c) {
      case '"':
        *out += "\\\"";
        break;
      case '\\':
        *out += "\\\\";
        break;
      default:
        *out += c;
        break;
    }
  }
  *out += '"';
}

bool ExtractObjectField(const std::string& json, const std::string& key,
                        std::string* value_out) {
  const std::string needle = "\"" + key + "\"";
  const size_t pos = json.find(needle);
  if (pos == std::string::npos) return false;
  size_t i = pos + needle.size();
  SkipWs(json, &i);
  if (i >= json.size() || json[i] != ':') return false;
  ++i;
  SkipWs(json, &i);
  if (i < json.size() && json[i] == '"') {
    return ParseJsonString(json, &i, value_out);
  }
  size_t end = i;
  while (end < json.size() && json[end] != ',' && json[end] != '}') ++end;
  *value_out = json.substr(i, end - i);
  return true;
}

void ApplyS3FromJson(const std::string& json, S3RemoteConfig* s3) {
  if (!s3) return;
  std::string v;
  if (ExtractObjectField(json, "endpoint", &v)) s3->endpoint = v;
  if (ExtractObjectField(json, "region", &v)) s3->region = v;
  if (ExtractObjectField(json, "bucket", &v)) s3->bucket = v;
  if (ExtractObjectField(json, "prefix", &v)) s3->prefix = v;
  if (ExtractObjectField(json, "access_key", &v)) s3->access_key = v;
  if (ExtractObjectField(json, "secret_key", &v)) s3->secret_key = v;
  if (ExtractObjectField(json, "path_style", &v)) {
    bool b = false;
    size_t i = 0;
    if (ParseJsonBool(v, &i, &b) || v == "true") s3->path_style = b || v == "true";
  }
}

void ApplyEnvOverrides(S3RemoteConfig* s3) {
  if (!s3) return;
  if (const char* v = std::getenv("EBSYNC_S3_ENDPOINT"); v && *v) s3->endpoint = v;
  if (const char* v = std::getenv("EBSYNC_S3_REGION"); v && *v) s3->region = v;
  if (const char* v = std::getenv("EBSYNC_S3_BUCKET"); v && *v) s3->bucket = v;
  if (const char* v = std::getenv("EBSYNC_S3_PREFIX"); v && *v) s3->prefix = v;
  if (const char* v = std::getenv("EBSYNC_S3_ACCESS_KEY"); v && *v) s3->access_key = v;
  if (const char* v = std::getenv("EBSYNC_S3_SECRET_KEY"); v && *v) s3->secret_key = v;
  if (const char* v = std::getenv("EBSYNC_S3_PATH_STYLE"); v && *v) {
    s3->path_style = (std::string(v) == "1" || std::string(v) == "true");
  }
}

void ApplyPdsFromJson(const std::string& json, PdsRemoteConfig* pds) {
  if (!pds) return;
  std::string v;
  if (ExtractObjectField(json, "domain_id", &v)) pds->domain_id = v;
  if (ExtractObjectField(json, "api_endpoint", &v)) pds->api_endpoint = v;
  if (ExtractObjectField(json, "client_id", &v)) pds->client_id = v;
  if (ExtractObjectField(json, "client_secret", &v)) pds->client_secret = v;
  if (ExtractObjectField(json, "redirect_uri", &v)) pds->redirect_uri = v;
  if (ExtractObjectField(json, "drive_id", &v)) pds->drive_id = v;
  if (ExtractObjectField(json, "root_prefix", &v)) pds->root_prefix = v;
  if (ExtractObjectField(json, "refresh_token", &v)) pds->refresh_token = v;
  if (ExtractObjectField(json, "access_token", &v)) pds->access_token = v;
  if (ExtractObjectField(json, "oauth_scope", &v)) pds->oauth_scope = v;
  if (ExtractObjectField(json, "login_type", &v)) pds->login_type = v;
  int64_t i64 = 0;
  if (ExtractObjectField(json, "access_token_expires_unix", &v)) {
    size_t i = 0;
    if (ParseJsonInt64(v, &i, &i64)) pds->access_token_expires_unix = i64;
  }
}

void ApplyPdsEnvOverrides(PdsRemoteConfig* pds) {
  if (!pds) return;
  if (const char* v = std::getenv("EBSYNC_PDS_DOMAIN"); v && *v) pds->domain_id = v;
  if (const char* v = std::getenv("EBSYNC_PDS_API_ENDPOINT"); v && *v) pds->api_endpoint = v;
  if (const char* v = std::getenv("EBSYNC_PDS_CLIENT_ID"); v && *v) pds->client_id = v;
  if (const char* v = std::getenv("EBSYNC_PDS_CLIENT_SECRET"); v && *v) pds->client_secret = v;
  if (const char* v = std::getenv("EBSYNC_PDS_REFRESH_TOKEN"); v && *v) pds->refresh_token = v;
  if (const char* v = std::getenv("EBSYNC_PDS_DRIVE_ID"); v && *v) pds->drive_id = v;
}

}  // namespace

bool IsSyncInitialized(const SyncState& state) {
  return !state.remote_type.empty();
}

std::string SyncStatePath(const std::string& repo_path) {
  return repo_path + "/catalog/sync_state.json";
}

std::string SyncConfigPath(const std::string& repo_path) {
  return repo_path + "/sync.json";
}

std::string SyncStateToJson(const SyncState& state) {
  std::string o;
  o += "{\n";
  o += "  \"remote_type\": ";
  JsonEscape(state.remote_type.empty() ? "s3" : state.remote_type, &o);
  o += ",\n  \"local_mirror_root\": ";
  JsonEscape(state.local_mirror_root, &o);
  o += ",\n  \"remote\": {\n";
  o += "    \"type\": ";
  JsonEscape(state.remote_type.empty() ? "s3" : state.remote_type, &o);
  o += ",\n    \"endpoint\": ";
  JsonEscape(state.s3.endpoint, &o);
  o += ",\n    \"region\": ";
  JsonEscape(state.s3.region, &o);
  o += ",\n    \"bucket\": ";
  JsonEscape(state.s3.bucket, &o);
  o += ",\n    \"prefix\": ";
  JsonEscape(state.s3.prefix, &o);
  o += ",\n    \"path_style\": ";
  o += state.s3.path_style ? "true" : "false";
  o += "\n  },\n";
  o += "  \"synced_txn\": " + std::to_string(state.synced_txn) + ",\n";
  o += "  \"pending_txn\": " + std::to_string(state.pending_txn) + ",\n";
  o += "  \"last_export_base_txn\": " + std::to_string(state.last_export_base_txn) + ",\n";
  o += "  \"last_ferry_target_txn\": " + std::to_string(state.last_ferry_target_txn) + ",\n";
  o += "  \"generation\": " + std::to_string(state.generation) + ",\n";
  o += "  \"last_success_unix\": " + std::to_string(state.last_success_unix) + ",\n";
  o += "  \"backoff_until_unix\": " + std::to_string(state.backoff_until_unix) + ",\n";
  o += "  \"pending_chunk_count\": " + std::to_string(state.pending_chunk_count) + ",\n";
  o += "  \"last_error\": ";
  JsonEscape(state.last_error, &o);
  o += "\n}\n";
  return o;
}

bool LoadSyncState(const std::string& repo_path, SyncState* out) {
  if (!out) return false;
  *out = SyncState{};
  const std::string path = SyncStatePath(repo_path);
  std::ifstream in(path);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  const std::string json = ss.str();
  const size_t remote_pos = json.find("\"remote\"");
  if (remote_pos != std::string::npos) {
    const size_t brace = json.find('{', remote_pos);
    if (brace != std::string::npos) {
      int depth = 0;
      size_t end = brace;
      for (; end < json.size(); ++end) {
        if (json[end] == '{') ++depth;
        else if (json[end] == '}') {
          --depth;
          if (depth == 0) {
            ++end;
            break;
          }
        }
      }
      const std::string remote_obj = json.substr(brace, end - brace);
      ApplyS3FromJson(remote_obj, &out->s3);
      std::string remote_type;
      if (ExtractObjectField(remote_obj, "type", &remote_type) && out->remote_type.empty()) {
        out->remote_type = remote_type;
      }
    }
  }
  std::string v;
  uint64_t u = 0;
  int64_t i64 = 0;
  if (ExtractObjectField(json, "synced_txn", &v)) {
    size_t i = 0;
    if (ParseJsonUint64(v, &i, &u)) out->synced_txn = u;
  }
  if (ExtractObjectField(json, "pending_txn", &v)) {
    size_t i = 0;
    u = 0;
    if (ParseJsonUint64(v, &i, &u)) out->pending_txn = u;
  }
  if (ExtractObjectField(json, "last_export_base_txn", &v)) {
    size_t i = 0;
    u = 0;
    if (ParseJsonUint64(v, &i, &u)) out->last_export_base_txn = u;
  }
  if (ExtractObjectField(json, "last_ferry_target_txn", &v)) {
    size_t i = 0;
    u = 0;
    if (ParseJsonUint64(v, &i, &u)) out->last_ferry_target_txn = u;
  }
  if (ExtractObjectField(json, "remote_type", &v)) out->remote_type = v;
  if (ExtractObjectField(json, "local_mirror_root", &v)) out->local_mirror_root = v;
  if (ExtractObjectField(json, "generation", &v)) {
    size_t i = 0;
    u = 0;
    if (ParseJsonUint64(v, &i, &u)) out->generation = static_cast<uint32_t>(u);
  }
  if (ExtractObjectField(json, "last_success_unix", &v)) {
    size_t i = 0;
    if (ParseJsonInt64(v, &i, &i64)) out->last_success_unix = i64;
  }
  if (ExtractObjectField(json, "backoff_until_unix", &v)) {
    size_t i = 0;
    if (ParseJsonInt64(v, &i, &i64)) out->backoff_until_unix = i64;
  }
  if (ExtractObjectField(json, "pending_chunk_count", &v)) {
    size_t i = 0;
    u = 0;
    if (ParseJsonUint64(v, &i, &u)) out->pending_chunk_count = u;
  }
  if (ExtractObjectField(json, "last_error", &v)) out->last_error = v;
  ApplyEnvOverrides(&out->s3);
  ApplyPdsEnvOverrides(&out->pds);
  return true;
}

bool SaveSyncState(const std::string& repo_path, const SyncState& state) {
  const std::string path = SyncStatePath(repo_path);
  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path(path).parent_path(), ec);
  std::ofstream out(path, std::ios::trunc);
  if (!out) return false;
  out << SyncStateToJson(state);
  return static_cast<bool>(out);
}

bool LoadSyncConfig(const std::string& repo_path, SyncState* out) {
  if (!out) return false;
  const std::string path = SyncConfigPath(repo_path);
  std::ifstream in(path);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  const std::string json = ss.str();
  ApplyS3FromJson(json, &out->s3);
  ApplyPdsFromJson(json, &out->pds);
  std::string v;
  if (ExtractObjectField(json, "remote_type", &v)) out->remote_type = v;
  if (ExtractObjectField(json, "local_mirror_root", &v)) out->local_mirror_root = v;
  if (ExtractObjectField(json, "access_key", &v)) out->s3.access_key = v;
  if (ExtractObjectField(json, "secret_key", &v)) out->s3.secret_key = v;
  ApplyEnvOverrides(&out->s3);
  ApplyPdsEnvOverrides(&out->pds);
  if (!out->pds.domain_id.empty() && out->pds.api_endpoint.empty()) {
    out->pds.api_endpoint = "https://" + out->pds.domain_id + ".api.aliyunfile.com";
  }
  if (out->pds.redirect_uri.empty() && !out->pds.domain_id.empty()) {
    out->pds.redirect_uri = DefaultPdsRedirectUri(out->pds.domain_id);
  }
  return true;
}

bool SaveSyncConfig(const std::string& repo_path, const SyncState& cfg) {
  const std::string path = SyncConfigPath(repo_path);
  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path(path).parent_path(), ec);
  std::string body = "{\n  \"remote_type\": ";
  JsonEscape(cfg.remote_type, &body);
  body += ",\n  \"local_mirror_root\": ";
  JsonEscape(cfg.local_mirror_root, &body);
  body += ",\n  \"endpoint\": ";
  JsonEscape(cfg.s3.endpoint, &body);
  body += ",\n  \"region\": ";
  JsonEscape(cfg.s3.region, &body);
  body += ",\n  \"bucket\": ";
  JsonEscape(cfg.s3.bucket, &body);
  body += ",\n  \"prefix\": ";
  JsonEscape(cfg.s3.prefix, &body);
  body += ",\n  \"access_key\": ";
  JsonEscape(cfg.s3.access_key, &body);
  body += ",\n  \"secret_key\": ";
  JsonEscape(cfg.s3.secret_key, &body);
  body += ",\n  \"path_style\": ";
  body += cfg.s3.path_style ? "true" : "false";
  body += ",\n  \"domain_id\": ";
  JsonEscape(cfg.pds.domain_id, &body);
  body += ",\n  \"api_endpoint\": ";
  JsonEscape(cfg.pds.api_endpoint, &body);
  body += ",\n  \"client_id\": ";
  JsonEscape(cfg.pds.client_id, &body);
  body += ",\n  \"client_secret\": ";
  JsonEscape(cfg.pds.client_secret, &body);
  body += ",\n  \"redirect_uri\": ";
  JsonEscape(cfg.pds.redirect_uri, &body);
  body += ",\n  \"drive_id\": ";
  JsonEscape(cfg.pds.drive_id, &body);
  body += ",\n  \"root_prefix\": ";
  JsonEscape(cfg.pds.root_prefix, &body);
  body += ",\n  \"refresh_token\": ";
  JsonEscape(cfg.pds.refresh_token, &body);
  body += ",\n  \"access_token\": ";
  JsonEscape(cfg.pds.access_token, &body);
  body += ",\n  \"access_token_expires_unix\": ";
  body += std::to_string(cfg.pds.access_token_expires_unix);
  body += ",\n  \"oauth_scope\": ";
  JsonEscape(cfg.pds.oauth_scope, &body);
  body += ",\n  \"login_type\": ";
  JsonEscape(cfg.pds.login_type, &body);
  body += "\n}\n";
  std::ofstream out(path, std::ios::trunc);
  if (!out) return false;
  out << body;
  return static_cast<bool>(out);
}

}  // namespace ebsync
