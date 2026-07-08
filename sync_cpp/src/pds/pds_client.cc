#include "ebsync/pds/pds_client.h"

#include <chrono>
#include <cctype>
#include <fstream>
#include <sstream>

#include "ebsync/sync_state.h"
#include "ebsync/transport/pds_http.h"

namespace ebsync {
namespace {

int64_t NowUnix() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string JsonGetString(const std::string& json, const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  const size_t pos = json.find(needle);
  if (pos == std::string::npos) return {};
  size_t i = pos + needle.size();
  while (i < json.size() && (json[i] == ' ' || json[i] == ':' || json[i] == '\t')) ++i;
  if (i >= json.size() || json[i] != '"') return {};
  ++i;
  std::string out;
  while (i < json.size()) {
    if (json[i] == '\\') {
      ++i;
      if (i < json.size()) out += json[i++];
      continue;
    }
    if (json[i] == '"') break;
    out += json[i++];
  }
  return out;
}

int64_t JsonGetInt64(const std::string& json, const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  const size_t pos = json.find(needle);
  if (pos == std::string::npos) return 0;
  size_t i = pos + needle.size();
  while (i < json.size() && (json[i] == ' ' || json[i] == ':' || json[i] == '\t')) ++i;
  size_t start = i;
  while (i < json.size() && (std::isdigit(static_cast<unsigned char>(json[i])) || json[i] == '-'))
    ++i;
  if (i == start) return 0;
  try {
    return std::stoll(json.substr(start, i - start));
  } catch (...) {
    return 0;
  }
}

std::string JsonEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    o += c;
  }
  return o;
}

std::string ReadFileText(const std::string& path) {
  std::ifstream in(path);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool WriteFileText(const std::string& path, const std::string& body) {
  std::ofstream out(path, std::ios::trunc);
  if (!out) return false;
  out << body;
  return static_cast<bool>(out);
}

std::string MapGet(const std::string& map_json, const std::string& key) {
  const std::string needle = "\"" + JsonEscape(key) + "\"";
  const size_t pos = map_json.find(needle);
  if (pos == std::string::npos) return {};
  size_t i = map_json.find(':', pos);
  if (i == std::string::npos) return {};
  ++i;
  while (i < map_json.size() && std::isspace(static_cast<unsigned char>(map_json[i]))) ++i;
  if (i >= map_json.size() || map_json[i] != '"') return {};
  ++i;
  std::string out;
  while (i < map_json.size()) {
    if (map_json[i] == '\\') {
      ++i;
      if (i < map_json.size()) out += map_json[i++];
      continue;
    }
    if (map_json[i] == '"') break;
    out += map_json[i++];
  }
  return out;
}

void MapSet(std::string* map_json, const std::string& key, const std::string& value) {
  if (!map_json) return;
  if (MapGet(*map_json, key) == value) return;
  if (map_json->empty() || *map_json == "{}") {
    *map_json = "{\"" + JsonEscape(key) + "\":\"" + JsonEscape(value) + "\"}";
    return;
  }
  const std::string needle = "\"" + JsonEscape(key) + "\"";
  const size_t pos = map_json->find(needle);
  if (pos != std::string::npos) {
    const size_t val_start = map_json->find('"', map_json->find(':', pos) + 1);
    if (val_start == std::string::npos) return;
    const size_t val_end = map_json->find('"', val_start + 1);
    if (val_end == std::string::npos) return;
    map_json->replace(val_start + 1, val_end - val_start - 1, JsonEscape(value));
    return;
  }
  if (map_json->back() == '}') map_json->pop_back();
  if (map_json->size() > 1) *map_json += ",";
  *map_json += "\"" + JsonEscape(key) + "\":\"" + JsonEscape(value) + "\"}";
}

std::vector<std::string> SplitKey(const std::string& key) {
  std::vector<std::string> parts;
  std::string cur;
  for (char c : key) {
    if (c == '/') {
      if (!cur.empty()) parts.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) parts.push_back(cur);
  return parts;
}

}  // namespace

std::string DefaultPdsApiEndpoint(const std::string& domain_id) {
  return "https://" + domain_id + ".api.aliyunfile.com";
}

std::string DefaultPdsAuthEndpoint(const std::string& domain_id) {
  return "https://" + domain_id + ".auth.aliyunfile.com";
}

std::string DefaultPdsRedirectUri(const std::string& domain_id) {
  return "https://" + domain_id + ".api.aliyunfile.com/v2/oauth/callback";
}

bool ImportPdsCredentialsCsv(const std::string& csv_path, PdsRemoteConfig* out) {
  if (!out) return false;
  const std::string text = ReadFileText(csv_path);
  if (text.empty()) return false;
  std::istringstream in(text);
  std::string header;
  if (!std::getline(in, header)) return false;
  std::string line;
  if (!std::getline(in, line)) return false;
  std::vector<std::string> cols;
  std::string cell;
  for (char c : line) {
    if (c == ',') {
      cols.push_back(cell);
      cell.clear();
    } else if (c != '\r') {
      cell += c;
    }
  }
  cols.push_back(cell);
  if (cols.size() < 3) return false;
  out->client_id = cols[0];
  out->client_secret = cols[2];
  return !out->client_id.empty() && !out->client_secret.empty();
}

PdsClient::PdsClient(std::string repo_path, PdsRemoteConfig* cfg)
    : repo_path_(std::move(repo_path)), cfg_(cfg) {}

std::string PdsClient::ApiEndpoint() const {
  if (cfg_ && !cfg_->api_endpoint.empty()) return cfg_->api_endpoint;
  if (cfg_ && !cfg_->domain_id.empty()) return DefaultPdsApiEndpoint(cfg_->domain_id);
  return {};
}

std::string PdsClient::AuthEndpoint() const {
  if (cfg_ && !cfg_->domain_id.empty()) return DefaultPdsAuthEndpoint(cfg_->domain_id);
  return ApiEndpoint();
}

std::string PdsClient::RedirectUri() const {
  if (cfg_ && !cfg_->redirect_uri.empty()) return cfg_->redirect_uri;
  if (cfg_ && !cfg_->domain_id.empty()) return DefaultPdsRedirectUri(cfg_->domain_id);
  return {};
}

std::string PdsClient::RemoteRoot() const {
  return cfg_ ? cfg_->root_prefix : "ebbackup";
}

std::string PdsClient::IndexPath() const {
  return repo_path_ + "/catalog/pds_file_index.json";
}

bool PdsClient::LoadIndex(std::string* error) {
  const std::string json = ReadFileText(IndexPath());
  if (json.empty()) {
    folder_index_ = "{}";
    file_index_ = "{}";
    return true;
  }
  folder_index_ = JsonGetString(json, "folders");
  file_index_ = JsonGetString(json, "files");
  if (folder_index_.empty()) folder_index_ = "{}";
  if (file_index_.empty()) file_index_ = "{}";
  return true;
}

bool PdsClient::SaveIndex(std::string* error) {
  std::ostringstream o;
  o << "{\n  \"folders\": \"" << JsonEscape(folder_index_) << "\",\n";
  o << "  \"files\": \"" << JsonEscape(file_index_) << "\"\n}\n";
  if (!WriteFileText(IndexPath(), o.str())) {
    if (error) *error = "cannot write pds index";
    return false;
  }
  return true;
}

std::string PdsClient::FolderIdForPath(const std::string& path) const {
  return MapGet(folder_index_, path);
}

void PdsClient::SetFolderId(const std::string& path, const std::string& id) {
  MapSet(&folder_index_, path, id);
}

std::string PdsClient::FileIdForKey(const std::string& key) const {
  return MapGet(file_index_, key);
}

void PdsClient::SetFileId(const std::string& key, const std::string& id) {
  MapSet(&file_index_, key, id);
}

bool PdsClient::ApiPost(const std::string& path, const std::string& json_body,
                        std::string* response, std::string* error) {
  if (!cfg_) {
    if (error) *error = "no config";
    return false;
  }
  if (!EnsureAccessToken(error)) return false;
  const std::string url = ApiEndpoint() + path;
  std::map<std::string, std::string> headers;
  headers["Content-Type"] = "application/json";
  headers["Authorization"] = "Bearer " + cfg_->access_token;
  const auto res = pdshttp::Request(
      "POST", url, headers,
      reinterpret_cast<const uint8_t*>(json_body.data()), json_body.size());
  if (response) {
    response->assign(reinterpret_cast<const char*>(res.body.data()), res.body.size());
  }
  if (!res.ok) {
    if (error) *error = res.error.empty() ? "pds api failed" : res.error;
    return false;
  }
  return true;
}

bool PdsClient::RefreshAccessToken(std::string* error) {
  if (!cfg_ || cfg_->client_id.empty() || cfg_->client_secret.empty()) {
    if (error) *error = "pds client credentials missing";
    return false;
  }
  std::ostringstream body;
  const std::string redirect = RedirectUri();
  if (!cfg_->refresh_token.empty()) {
    body << "refresh_token=" << pdshttp::UrlEncode(cfg_->refresh_token)
         << "&client_id=" << pdshttp::UrlEncode(cfg_->client_id)
         << "&client_secret=" << pdshttp::UrlEncode(cfg_->client_secret)
         << "&grant_type=refresh_token";
  } else {
    if (error) *error = "pds refresh_token missing; run eb-sync pds auth";
    return false;
  }
  const std::string body_str = body.str();
  const std::string url = ApiEndpoint() + "/v2/oauth/token";
  std::map<std::string, std::string> headers;
  headers["Content-Type"] = "application/x-www-form-urlencoded";
  const auto res = pdshttp::Request(
      "POST", url, headers,
      reinterpret_cast<const uint8_t*>(body_str.data()), body_str.size());
  if (!res.ok) {
    if (error) *error = res.error;
    return false;
  }
  const std::string text(reinterpret_cast<const char*>(res.body.data()), res.body.size());
  cfg_->access_token = JsonGetString(text, "access_token");
  const int64_t expires = JsonGetInt64(text, "expires_in");
  cfg_->access_token_expires_unix = NowUnix() + (expires > 0 ? expires : 3600);
  const std::string new_refresh = JsonGetString(text, "refresh_token");
  if (!new_refresh.empty()) cfg_->refresh_token = new_refresh;
  SyncState st;
  LoadSyncState(repo_path_, &st);
  LoadSyncConfig(repo_path_, &st);
  st.pds = *cfg_;
  SaveSyncConfig(repo_path_, st);
  return !cfg_->access_token.empty();
}

bool PdsClient::EnsureAccessToken(std::string* error) {
  if (!cfg_) {
    if (error) *error = "no config";
    return false;
  }
  if (!cfg_->access_token.empty() &&
      cfg_->access_token_expires_unix > NowUnix() + 60) {
    return true;
  }
  return RefreshAccessToken(error);
}

bool PdsClient::BuildAuthorizeUrl(std::string* url_out, std::string* error) {
  if (!cfg_ || cfg_->client_id.empty()) {
    if (error) *error = "pds client_id missing";
    return false;
  }
  const std::string redirect = pdshttp::UrlEncode(RedirectUri());
  std::ostringstream o;
  o << AuthEndpoint() << "/v2/oauth/authorize?client_id="
    << pdshttp::UrlEncode(cfg_->client_id) << "&response_type=code&redirect_uri="
    << redirect << "&state=ebbackup";
  if (!cfg_->oauth_scope.empty()) {
    o << "&scope=" << pdshttp::UrlEncode(cfg_->oauth_scope);
  } else {
    o << "&scope=all";
  }
  if (!cfg_->login_type.empty()) {
    o << "&login_type=" << pdshttp::UrlEncode(cfg_->login_type);
  }
  if (url_out) *url_out = o.str();
  return true;
}

bool PdsClient::ExchangeAuthCode(const std::string& code, std::string* error) {
  if (!cfg_) return false;
  std::ostringstream body;
  body << "code=" << pdshttp::UrlEncode(code)
       << "&client_id=" << pdshttp::UrlEncode(cfg_->client_id)
       << "&client_secret=" << pdshttp::UrlEncode(cfg_->client_secret)
       << "&redirect_uri=" << pdshttp::UrlEncode(RedirectUri())
       << "&grant_type=authorization_code";
  const std::string body_str = body.str();
  const std::string url = ApiEndpoint() + "/v2/oauth/token";
  std::map<std::string, std::string> headers;
  headers["Content-Type"] = "application/x-www-form-urlencoded";
  const auto res = pdshttp::Request(
      "POST", url, headers,
      reinterpret_cast<const uint8_t*>(body_str.data()), body_str.size());
  if (!res.ok) {
    if (error) *error = res.error;
    return false;
  }
  const std::string text(reinterpret_cast<const char*>(res.body.data()), res.body.size());
  cfg_->access_token = JsonGetString(text, "access_token");
  cfg_->refresh_token = JsonGetString(text, "refresh_token");
  const int64_t expires = JsonGetInt64(text, "expires_in");
  cfg_->access_token_expires_unix = NowUnix() + (expires > 0 ? expires : 3600);
  SyncState st;
  LoadSyncState(repo_path_, &st);
  LoadSyncConfig(repo_path_, &st);
  st.pds = *cfg_;
  st.remote_type = "pds";
  SaveSyncConfig(repo_path_, st);
  SaveSyncState(repo_path_, st);
  return !cfg_->refresh_token.empty();
}

bool PdsClient::ResolveDriveId(std::string* error) {
  if (cfg_ && !cfg_->drive_id.empty()) return true;
  std::ostringstream req;
  req << "{\"domain_id\":\"" << JsonEscape(cfg_->domain_id) << "\",\"limit\":50}";
  std::string resp;
  if (!ApiPost("/v2/drive/list", req.str(), &resp, error)) return false;
  size_t pos = 0;
  while ((pos = resp.find("\"drive_id\"", pos)) != std::string::npos) {
    const std::string id = JsonGetString(resp.substr(pos), "drive_id");
    if (!id.empty()) {
      cfg_->drive_id = id;
      SyncState st;
      LoadSyncState(repo_path_, &st);
      LoadSyncConfig(repo_path_, &st);
      st.pds = *cfg_;
      SaveSyncConfig(repo_path_, st);
      return true;
    }
    pos += 10;
  }
  if (error) *error = "no drive_id in list response";
  return false;
}

bool PdsClient::EnsureFolderPath(const std::string& rel_folder, std::string* folder_id,
                                 std::string* error) {
  if (!LoadIndex(error)) return false;
  if (const std::string cached = FolderIdForPath(rel_folder); !cached.empty()) {
    if (folder_id) *folder_id = cached;
    return true;
  }
  if (!ResolveDriveId(error)) return false;
  std::string parent = "root";
  std::string built;
  const auto parts = SplitKey(rel_folder);
  for (const auto& part : parts) {
    built = built.empty() ? part : built + "/" + part;
    if (const std::string cid = FolderIdForPath(built); !cid.empty()) {
      parent = cid;
      continue;
    }
    std::ostringstream req;
    req << "{\"drive_id\":\"" << JsonEscape(cfg_->drive_id)
        << "\",\"parent_file_id\":\"" << JsonEscape(parent)
        << "\",\"name\":\"" << JsonEscape(part) << "\",\"type\":\"folder\"}";
    std::string resp;
    if (!ApiPost("/v2/file/create", req.str(), &resp, error)) return false;
    const std::string fid = JsonGetString(resp, "file_id");
    if (fid.empty()) {
      if (error) *error = "create folder failed";
      return false;
    }
    SetFolderId(built, fid);
    parent = fid;
  }
  if (!SaveIndex(error)) return false;
  if (folder_id) *folder_id = parent;
  return true;
}

bool PdsClient::HeadObject(const std::string& key, bool* exists, std::string* error) {
  if (!exists) return false;
  if (!LoadIndex(error)) return false;
  *exists = !FileIdForKey(key).empty();
  return true;
}

bool PdsClient::PutObject(const std::string& key, const uint8_t* data, size_t len,
                          std::string* error) {
  if (!LoadIndex(error)) return false;
  if (!ResolveDriveId(error)) return false;
  const auto parts = SplitKey(key);
  if (parts.empty()) {
    if (error) *error = "empty key";
    return false;
  }
  std::string parent_folder = RemoteRoot();
  for (size_t i = 0; i + 1 < parts.size(); ++i) {
    parent_folder += "/" + parts[i];
  }
  std::string parent_id;
  if (!EnsureFolderPath(parent_folder, &parent_id, error)) return false;
  const std::string& file_name = parts.back();
  std::ostringstream req;
  req << "{\"drive_id\":\"" << JsonEscape(cfg_->drive_id)
      << "\",\"parent_file_id\":\"" << JsonEscape(parent_id)
      << "\",\"name\":\"" << JsonEscape(file_name)
      << "\",\"type\":\"file\",\"size\":" << len << "}";
  std::string resp;
  if (!ApiPost("/v2/file/create", req.str(), &resp, error)) return false;
  if (JsonGetString(resp, "rapid_upload") == "true") {
    const std::string fid = JsonGetString(resp, "file_id");
    if (!fid.empty()) {
      SetFileId(key, fid);
      return SaveIndex(error);
    }
  }
  const std::string file_id = JsonGetString(resp, "file_id");
  const std::string upload_id = JsonGetString(resp, "upload_id");
  std::string upload_url = JsonGetString(resp, "upload_url");
  if (upload_url.empty()) {
    const size_t list_pos = resp.find("\"part_info_list\"");
    if (list_pos != std::string::npos) {
      upload_url = JsonGetString(resp.substr(list_pos), "upload_url");
    }
  }
  if (upload_url.empty()) {
    const size_t up = resp.find("\"upload_url\"");
    if (up != std::string::npos) {
      upload_url = JsonGetString(resp.substr(up), "upload_url");
    }
  }
  if (upload_url.empty() || file_id.empty() || upload_id.empty()) {
    if (error) *error = "create file missing upload info";
    return false;
  }
  std::map<std::string, std::string> headers;
  headers["Content-Type"] = "application/octet-stream";
  const auto put_res =
      pdshttp::Request("PUT", upload_url, headers, data, len);
  if (!put_res.ok) {
    if (error) *error = put_res.error;
    return false;
  }
  std::ostringstream complete;
  complete << "{\"drive_id\":\"" << JsonEscape(cfg_->drive_id)
           << "\",\"file_id\":\"" << JsonEscape(file_id)
           << "\",\"upload_id\":\"" << JsonEscape(upload_id) << "\"}";
  if (!ApiPost("/v2/file/complete", complete.str(), &resp, error)) return false;
  SetFileId(key, file_id);
  return SaveIndex(error);
}

bool PdsClient::GetObject(const std::string& key, std::vector<uint8_t>* out,
                          std::string* error) {
  if (!out) return false;
  if (!LoadIndex(error)) return false;
  const std::string file_id = FileIdForKey(key);
  if (file_id.empty()) {
    if (error) *error = "pds object not found";
    return false;
  }
  if (!ResolveDriveId(error)) return false;
  std::ostringstream req;
  req << "{\"drive_id\":\"" << JsonEscape(cfg_->drive_id)
      << "\",\"file_id\":\"" << JsonEscape(file_id) << "\"}";
  std::string resp;
  if (!ApiPost("/v2/file/get_download_url", req.str(), &resp, error)) return false;
  const std::string url = JsonGetString(resp, "url");
  if (url.empty()) {
    if (error) *error = "no download url";
    return false;
  }
  const auto dl = pdshttp::Request("GET", url, {}, nullptr, 0);
  if (!dl.ok) {
    if (error) *error = dl.error;
    return false;
  }
  *out = dl.body;
  return true;
}

}  // namespace ebsync
