#pragma once

#include <cstdint>
#include <string>

namespace ebsync {

struct S3RemoteConfig {
  std::string endpoint;  // e.g. https://s3.amazonaws.com or http://127.0.0.1:9000
  std::string region{"us-east-1"};
  std::string bucket;
  std::string prefix;  // repo-id/ with trailing slash optional
  std::string access_key;
  std::string secret_key;
  bool path_style{false};  // MinIO often true
};

struct PdsRemoteConfig {
  std::string domain_id;
  std::string api_endpoint;
  std::string client_id;
  std::string client_secret;
  std::string redirect_uri;
  std::string drive_id;
  std::string root_prefix{"ebbackup"};
  std::string refresh_token;
  std::string access_token;
  int64_t access_token_expires_unix{0};
  std::string oauth_scope;
  std::string login_type{"default"};
};

struct SyncState {
  // local_mirror | ferry | s3 | pds | empty (not initialized)
  std::string remote_type;
  std::string local_mirror_root;
  S3RemoteConfig s3{};
  PdsRemoteConfig pds{};
  uint64_t synced_txn{0};
  uint64_t pending_txn{0};
  uint64_t last_export_base_txn{0};
  uint64_t last_ferry_target_txn{0};
  uint32_t generation{0};
  int64_t last_success_unix{0};
  int64_t backoff_until_unix{0};
  std::string last_error;
  uint64_t pending_chunk_count{0};
};

bool IsSyncInitialized(const SyncState& state);

std::string SyncStatePath(const std::string& repo_path);
std::string SyncConfigPath(const std::string& repo_path);

bool LoadSyncState(const std::string& repo_path, SyncState* out);
bool SaveSyncState(const std::string& repo_path, const SyncState& state);
std::string SyncStateToJson(const SyncState& state);

bool LoadSyncConfig(const std::string& repo_path, SyncState* out);
bool SaveSyncConfig(const std::string& repo_path, const SyncState& cfg);

}  // namespace ebsync
