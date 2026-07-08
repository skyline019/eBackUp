#include "ebbackup/job/job_config.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ebbackup/common/path_encoding.h"
#include "ebbackup/common/path_util.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/engine/restore_options_json.h"
#include "ebbackup/state/superblock.h"

namespace ebbackup {
namespace job {

namespace {

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

void SkipWs(const std::string& s, size_t* i) {
  while (*i < s.size() && std::isspace(static_cast<unsigned char>(s[*i]))) ++(*i);
}

Status ParseJsonObject(const std::string& json, BackupJob* out) {
  if (!out) return Status::InvalidArgument("out is null");
  BackupJob job{};
  const Status st1 = ReadJsonStringField(json, "id", &job.id);
  if (!st1.ok()) return st1;
  const Status st2 = ReadJsonStringField(json, "name", &job.name);
  if (!st2.ok()) return st2;
  const Status st3 = ReadJsonStringField(json, "source_path", &job.source_path);
  if (!st3.ok()) return st3;
  uint64_t tag = 0;
  (void)ReadJsonU64Field(json, "retention_tag", &tag);
  job.retention_tag = static_cast<uint32_t>(tag);
  (void)ReadJsonIntField(json, "immutability_days", &job.immutability_days);
  (void)ReadJsonBoolField(json, "worm", &job.worm);
  const Status st4 = ReadJsonStringArrayField(json, "exclude_globs", &job.exclude_globs);
  if (!st4.ok()) return st4;
  (void)ReadJsonStringArrayField(json, "exclude_paths", &job.exclude_paths);
  (void)ReadJsonStringArrayField(json, "plugins", &job.plugins);
  (void)ReadJsonStringField(json, "window_start", &job.window.window_start);
  (void)ReadJsonStringField(json, "window_end", &job.window.window_end);
  (void)ReadJsonIntField(json, "deadline_grace_seconds", &job.window.deadline_grace_seconds);
  (void)ReadJsonBoolField(json, "durability_adaptive", &job.window.durability_adaptive);
  (void)ReadJsonBoolField(json, "use_vss", &job.use_vss);
  (void)ReadJsonStringField(json, "vss_mode", &job.vss_mode);
  (void)ReadJsonBoolField(json, "vss_fallback_live", &job.vss_fallback_live);
  (void)ReadJsonBoolField(json, "vss_include_junction_volumes",
                          &job.vss_include_junction_volumes);
  (void)ReadJsonStringField(json, "quiesce_profile", &job.quiesce_profile);
  (void)ReadJsonStringField(json, "vss_app_failure_policy", &job.vss_app_failure_policy);
  (void)ReadJsonStringField(json, "post_backup_webhook_url", &job.post_backup_webhook_url);
  if (job.window.deadline_grace_seconds <= 0) job.window.deadline_grace_seconds = 300;
  *out = std::move(job);
  return Status::Ok();
}

Status MaybeEnableRepoImmutable(const std::string& repo_path, const BackupJob& job) {
  if (!job.worm && job.immutability_days <= 0) return Status::Ok();
  BackupSuperBlockStore store(RepoJoinUtf8(repo_path, "superblock.bin"));
  BackupSuperBlock sb{};
  const Status load_st = store.Load(&sb);
  if (!load_st.ok()) return load_st;
  if ((sb.ext.backup_features & kBackupFeatureImmutable) != 0) return Status::Ok();
  sb.ext.backup_features |= kBackupFeatureImmutable;
  return store.Commit(sb);
}

}  // namespace

std::string JobsConfigPath(const std::string& repo_path) {
  return RepoJoinUtf8(repo_path, "jobs.json");
}

std::string JobsToJson(const std::vector<BackupJob>& jobs) {
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < jobs.size(); ++i) {
    if (i) out << ',';
    const BackupJob& j = jobs[i];
    out << "{";
    std::string esc;
    JsonEscape(j.id, &esc);
    out << "\"id\":\"" << esc << "\"";
    JsonEscape(j.name, &esc);
    out << ",\"name\":\"" << esc << "\"";
    JsonEscape(j.source_path, &esc);
    out << ",\"source_path\":\"" << esc << "\"";
    out << ",\"retention_tag\":" << j.retention_tag;
    out << ",\"immutability_days\":" << j.immutability_days;
    out << ",\"worm\":" << (j.worm ? "true" : "false");
    out << ",\"exclude_globs\":[";
    for (size_t k = 0; k < j.exclude_globs.size(); ++k) {
      if (k) out << ',';
      JsonEscape(j.exclude_globs[k], &esc);
      out << "\"" << esc << "\"";
    }
    out << "],\"exclude_paths\":[";
    for (size_t k = 0; k < j.exclude_paths.size(); ++k) {
      if (k) out << ',';
      JsonEscape(j.exclude_paths[k], &esc);
      out << "\"" << esc << "\"";
    }
    out << "],\"plugins\":[";
    for (size_t k = 0; k < j.plugins.size(); ++k) {
      if (k) out << ',';
      JsonEscape(j.plugins[k], &esc);
      out << "\"" << esc << "\"";
    }
    out << "]";
    if (!j.window.window_start.empty()) {
      JsonEscape(j.window.window_start, &esc);
      out << ",\"window_start\":\"" << esc << "\"";
    }
    if (!j.window.window_end.empty()) {
      JsonEscape(j.window.window_end, &esc);
      out << ",\"window_end\":\"" << esc << "\"";
    }
    if (j.window.deadline_grace_seconds != 300) {
      out << ",\"deadline_grace_seconds\":" << j.window.deadline_grace_seconds;
    }
    if (j.window.durability_adaptive) {
      out << ",\"durability_adaptive\":true";
    }
    if (j.use_vss) {
      out << ",\"use_vss\":true";
    }
    if (!j.vss_mode.empty()) {
      JsonEscape(j.vss_mode, &esc);
      out << ",\"vss_mode\":\"" << esc << "\"";
    }
    if (j.vss_fallback_live) {
      out << ",\"vss_fallback_live\":true";
    }
    if (!j.vss_include_junction_volumes) {
      out << ",\"vss_include_junction_volumes\":false";
    }
    if (!j.quiesce_profile.empty()) {
      JsonEscape(j.quiesce_profile, &esc);
      out << ",\"quiesce_profile\":\"" << esc << "\"";
    }
    if (!j.vss_app_failure_policy.empty()) {
      JsonEscape(j.vss_app_failure_policy, &esc);
      out << ",\"vss_app_failure_policy\":\"" << esc << "\"";
    }
    if (!j.post_backup_webhook_url.empty()) {
      JsonEscape(j.post_backup_webhook_url, &esc);
      out << ",\"post_backup_webhook_url\":\"" << esc << "\"";
    }
    out << "}";
  }
  out << "]";
  return out.str();
}

Status ParseJobJson(const std::string& json, BackupJob* out) {
  return ParseJsonObject(json, out);
}

Status ParseJobsJson(const std::string& json, std::vector<BackupJob>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  size_t i = 0;
  SkipWs(json, &i);
  if (i >= json.size()) return Status::InvalidArgument("empty jobs json");
  if (json[i] == '[') {
    ++i;
    SkipWs(json, &i);
    if (i < json.size() && json[i] == ']') return Status::Ok();
    while (i < json.size()) {
      SkipWs(json, &i);
      if (i >= json.size() || json[i] != '{') {
        return Status::InvalidArgument("expected job object in array");
      }
      const size_t start = i;
      int depth = 0;
      for (; i < json.size(); ++i) {
        if (json[i] == '{') ++depth;
        if (json[i] == '}') {
          --depth;
          if (depth == 0) {
            ++i;
            break;
          }
        }
      }
      BackupJob job{};
      const Status st = ParseJsonObject(json.substr(start, i - start), &job);
      if (!st.ok()) return st;
      out->push_back(std::move(job));
      SkipWs(json, &i);
      if (i < json.size() && json[i] == ',') {
        ++i;
        continue;
      }
      if (i < json.size() && json[i] == ']') return Status::Ok();
      return Status::InvalidArgument("bad jobs array");
    }
    return Status::InvalidArgument("unterminated jobs array");
  }
  BackupJob job{};
  const Status st = ParseJsonObject(json, &job);
  if (!st.ok()) return st;
  out->push_back(std::move(job));
  return Status::Ok();
}

Status LoadJobs(const std::string& repo_path, std::vector<BackupJob>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  const std::string path = JobsConfigPath(repo_path);
  if (!std::filesystem::exists(PathFromUtf8(path))) return Status::Ok();
  std::ifstream in(PathFromUtf8(path), std::ios::binary);
  if (!in) return Status::IoError("cannot read jobs.json");
  std::ostringstream ss;
  ss << in.rdbuf();
  return ParseJobsJson(ss.str(), out);
}

Status SaveJobs(const std::string& repo_path, const std::vector<BackupJob>& jobs) {
  std::error_code ec;
  std::filesystem::create_directories(PathFromUtf8(repo_path), ec);
  std::ofstream out(PathFromUtf8(JobsConfigPath(repo_path)), std::ios::trunc);
  if (!out) return Status::IoError("cannot write jobs.json");
  out << JobsToJson(jobs);
  if (!out) return Status::IoError("jobs.json write failed");
  return Status::Ok();
}

Status GetJob(const std::string& repo_path, const std::string& job_id,
              BackupJob* out) {
  if (!out) return Status::InvalidArgument("out is null");
  if (job_id.empty()) return Status::InvalidArgument("job_id empty");
  std::vector<BackupJob> jobs;
  const Status st = LoadJobs(repo_path, &jobs);
  if (!st.ok()) return st;
  for (const auto& j : jobs) {
    if (j.id == job_id) {
      *out = j;
      return Status::Ok();
    }
  }
  return Status::NotFound("job not found: " + job_id);
}

Status UpsertJob(const std::string& repo_path, const BackupJob& job) {
  if (job.id.empty()) return Status::InvalidArgument("job id required");
  if (job.source_path.empty()) return Status::InvalidArgument("source_path required");
  if (!std::filesystem::exists(PathFromUtf8(job.source_path))) {
    return Status::NotFound("source_path not found: " + job.source_path);
  }
  std::vector<BackupJob> jobs;
  const Status load_st = LoadJobs(repo_path, &jobs);
  if (!load_st.ok()) return load_st;
  bool found = false;
  for (auto& j : jobs) {
    if (j.id == job.id) {
      j = job;
      found = true;
      break;
    }
  }
  if (!found) jobs.push_back(job);
  const Status save_st = SaveJobs(repo_path, jobs);
  if (!save_st.ok()) return save_st;
  return MaybeEnableRepoImmutable(repo_path, job);
}

Status DeleteJob(const std::string& repo_path, const std::string& job_id) {
  if (job_id.empty()) return Status::InvalidArgument("job_id empty");
  std::vector<BackupJob> jobs;
  const Status load_st = LoadJobs(repo_path, &jobs);
  if (!load_st.ok()) return load_st;
  const size_t before = jobs.size();
  jobs.erase(std::remove_if(jobs.begin(), jobs.end(),
                            [&](const BackupJob& j) { return j.id == job_id; }),
             jobs.end());
  if (jobs.size() == before) return Status::NotFound("job not found: " + job_id);
  return SaveJobs(repo_path, jobs);
}

}  // namespace job
}  // namespace ebbackup
