#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebsync/sync_state.h"

namespace ebsync {

struct PdsFileIndex {
  std::string folders_json;  // serialized map path->file_id
  std::string files_json;
};

class PdsClient {
 public:
  explicit PdsClient(std::string repo_path, PdsRemoteConfig* cfg);

  bool EnsureAccessToken(std::string* error);
  bool ResolveDriveId(std::string* error);
  bool BuildAuthorizeUrl(std::string* url_out, std::string* error);
  bool ExchangeAuthCode(const std::string& code, std::string* error);
  bool HeadObject(const std::string& key, bool* exists, std::string* error);
  bool PutObject(const std::string& key, const uint8_t* data, size_t len,
                 std::string* error);
  bool GetObject(const std::string& key, std::vector<uint8_t>* out,
                 std::string* error);

 private:
  bool ApiPost(const std::string& path, const std::string& json_body,
               std::string* response, std::string* error);
  bool RefreshAccessToken(std::string* error);
  bool EnsureFolderPath(const std::string& rel_folder, std::string* folder_id,
                        std::string* error);
  std::string ApiEndpoint() const;
  std::string AuthEndpoint() const;
  std::string RedirectUri() const;
  std::string RemoteRoot() const;
  std::string IndexPath() const;
  bool LoadIndex(std::string* error);
  bool SaveIndex(std::string* error);
  std::string FolderIdForPath(const std::string& path) const;
  void SetFolderId(const std::string& path, const std::string& id);
  std::string FileIdForKey(const std::string& key) const;
  void SetFileId(const std::string& key, const std::string& id);

  std::string repo_path_;
  PdsRemoteConfig* cfg_;
  std::string folder_index_;
  std::string file_index_;
};

bool ImportPdsCredentialsCsv(const std::string& csv_path, PdsRemoteConfig* out);
std::string DefaultPdsApiEndpoint(const std::string& domain_id);
std::string DefaultPdsAuthEndpoint(const std::string& domain_id);
std::string DefaultPdsRedirectUri(const std::string& domain_id);

}  // namespace ebsync
