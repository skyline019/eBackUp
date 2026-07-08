#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "ebsync/sync_state.h"
#include "ebsync/transport/transport.h"

namespace ebsync {

uint64_t ReadLatestTxnId(const std::string& repo_path);
uint64_t ReadSyncedTxnHint(const std::string& repo_path);

struct PushOptions {
  bool once{false};
  bool drain{false};
  int max_attempts{8};
};

struct PushReport {
  bool ok{false};
  std::string error;
  uint64_t synced_txn{0};
  uint64_t chunks_uploaded{0};
};

bool InitSyncRepo(const std::string& repo_path, const SyncState& cfg);
bool InitSyncRepoLocal(const std::string& repo_path, const std::string& mirror_root);
bool InitSyncRepoFerry(const std::string& repo_path);
bool InitSyncRepoPds(const std::string& repo_path, const PdsRemoteConfig& pds_cfg,
                     const std::string& repo_label);
bool ImportPdsCredentialsCsv(const std::string& csv_path, PdsRemoteConfig* out);
bool PdsBuildAuthUrl(const std::string& repo_path, std::string* url_out);
bool PdsAuthExchangeCode(const std::string& repo_path, const std::string& code,
                         std::string* summary_out);
bool PdsSetupDrive(const std::string& repo_path, std::string* summary_out);
std::string BuildStatusJson(const std::string& repo_path);
bool BuildPlanJson(const std::string& repo_path, uint64_t base_txn, std::string* json_out);

PushReport PushSync(const std::string& repo_path, const PushOptions& opts);
struct FerryExportOptions {
  bool auto_base{false};
  uint64_t base_txn{0};
  uint64_t target_txn{0};
  bool also_mirror{false};
};

bool FerryExport(const std::string& repo_path, const std::string& out_dir,
                 const FerryExportOptions& opts, std::string* summary_out);
bool FerryImport(const std::string& base_path, const std::string& delta_path,
                 const std::string& dest_repo, std::string* summary_out);
bool VerifyRemote(const std::string& repo_path, uint64_t at_txn, std::string* json_out);
bool PullRemote(const std::string& repo_path, const std::string& dest_repo,
                uint64_t at_txn, std::string* summary_out);

bool ShouldBlockMaintenance(const std::string& repo_path, std::string* reason_out);

std::unique_ptr<IRemoteTransport> CreateTransportForRepo(const std::string& repo_path,
                                                         SyncState* state);

}  // namespace ebsync
