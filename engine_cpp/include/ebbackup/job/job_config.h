#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"
#include "ebbackup/job/backup_window.h"

namespace ebbackup {
namespace job {

struct BackupJob {
  std::string id;
  std::string name;
  std::string source_path;
  uint32_t retention_tag{0};
  int immutability_days{0};
  bool worm{false};
  std::vector<std::string> exclude_globs;
  std::vector<std::string> exclude_paths;
  std::vector<std::string> plugins;
  BackupWindowPolicy window;
  bool use_vss{false};
  std::string vss_mode;
  bool vss_fallback_live{false};
  bool vss_include_junction_volumes{true};
  std::string quiesce_profile;
  std::string vss_app_failure_policy;
  std::string post_backup_webhook_url;
};

std::string JobsConfigPath(const std::string& repo_path);
Status LoadJobs(const std::string& repo_path, std::vector<BackupJob>* out);
Status SaveJobs(const std::string& repo_path, const std::vector<BackupJob>& jobs);
Status GetJob(const std::string& repo_path, const std::string& job_id,
              BackupJob* out);
Status UpsertJob(const std::string& repo_path, const BackupJob& job);
Status DeleteJob(const std::string& repo_path, const std::string& job_id);
std::string JobsToJson(const std::vector<BackupJob>& jobs);
Status ParseJobJson(const std::string& json, BackupJob* out);
Status ParseJobsJson(const std::string& json, std::vector<BackupJob>* out);

}  // namespace job
}  // namespace ebbackup
