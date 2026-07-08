#include "ebbackup/daemon/backup_daemon.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "ebbackup/codec/content_class.h"
#include "ebbackup/io/fs_watch.h"
#include "ebbackup/scan/backup_filter.h"

namespace ebbackup {

namespace {

std::atomic<bool> g_daemon_stop_requested{false};

bool SleepSecondsInterruptible(int total_seconds) {
  const int secs = std::max(1, total_seconds);
  for (int i = 0; i < secs; ++i) {
    if (g_daemon_stop_requested.load()) return false;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  return !g_daemon_stop_requested.load();
}

std::string Trim(const std::string& s) {
  size_t start = 0;
  while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' ||
                              s[start] == '\n')) {
    ++start;
  }
  size_t end = s.size();
  while (end > start &&
         (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' ||
          s[end - 1] == '\n')) {
    --end;
  }
  return s.substr(start, end - start);
}

bool ParseBool(const std::string& value) {
  return value == "1" || value == "true" || value == "yes" || value == "on";
}

void ApplyScheduleField(const std::string& key, const std::string& value,
                        ScheduleConfig* cfg) {
  if (key == "interval_seconds") {
    cfg->interval_seconds = std::max(1, std::atoi(value.c_str()));
  } else if (key == "source") {
    cfg->source_path = value;
  } else if (key == "repo_base") {
    cfg->repo_base = value;
  } else if (key == "retain") {
    cfg->retain_count = std::max(1, std::atoi(value.c_str()));
    cfg->retention_policy.retain_min = cfg->retain_count;
  } else if (key == "retain_min") {
    cfg->retention_policy.retain_min = std::max(1, std::atoi(value.c_str()));
  } else if (key == "retention_tiers") {
    (void)ParseRetentionTiers(value, &cfg->retention_policy);
  } else if (key == "auto_prune") {
    cfg->auto_prune = ParseBool(value);
  } else if (key == "auto_gc_after_prune") {
    cfg->auto_gc_after_prune = ParseBool(value);
  } else if (key == "lz4") {
    cfg->backup_options.use_lz4 = ParseBool(value);
  } else if (key == "pipeline") {
    cfg->backup_options.use_pipeline = ParseBool(value);
  } else if (key == "encrypt") {
    cfg->backup_options.use_encryption = ParseBool(value);
  } else if (key == "password") {
    cfg->encryption_password = value;
    cfg->backup_options.encryption_password = value;
  } else if (key == "filter_file") {
    BackupFilterOptions loaded{};
    if (LoadBackupFilterFromFile(value, &loaded).ok()) {
      cfg->backup_options.filter = std::move(loaded);
    }
  } else if (key == "include_glob") {
    cfg->backup_options.filter.include_globs.push_back(value);
  } else if (key == "exclude_glob") {
    cfg->backup_options.filter.exclude_globs.push_back(value);
  } else if (key == "compress") {
    if (value == "auto") {
      cfg->backup_options.compress_mode = CompressMode::kAuto;
    } else if (value == "lz4") {
      cfg->backup_options.compress_mode = CompressMode::kLz4;
      cfg->backup_options.use_lz4 = true;
    } else if (value == "zstd") {
      cfg->backup_options.compress_mode = CompressMode::kZstd;
    } else if (value == "off") {
      cfg->backup_options.compress_mode = CompressMode::kOff;
    }
  } else if (key == "cpu_budget") {
    const int pct = std::max(1, std::min(100, std::atoi(value.c_str())));
    cfg->backup_options.cpu_budget_permille =
        static_cast<uint32_t>(pct * 10);
  } else if (key == "durability") {
    if (value == "balanced") {
      cfg->backup_options.durability = DurabilityMode::kBalanced;
    } else {
      cfg->backup_options.durability = DurabilityMode::kStrict;
    }
  } else if (key == "pre_backup_cmd") {
    cfg->backup_options.pre_backup_cmd = value;
  } else if (key == "post_backup_cmd") {
    cfg->backup_options.post_backup_cmd = value;
  } else if (key == "mode") {
    cfg->mode = value;
  } else if (key == "repo_path") {
    cfg->repo_path = value;
  } else if (key == "drain_queue") {
    cfg->drain_queue = ParseBool(value);
  } else if (key == "job_ids") {
    cfg->job_ids.clear();
    size_t start = 0;
    while (start < value.size()) {
      const size_t comma = value.find(',', start);
      const std::string part = Trim(value.substr(
          start, comma == std::string::npos ? std::string::npos : comma - start));
      if (!part.empty()) cfg->job_ids.push_back(part);
      if (comma == std::string::npos) break;
      start = comma + 1;
    }
  } else if (key == "plugins") {
    cfg->backup_options.plugins.clear();
    size_t start = 0;
    while (start < value.size()) {
      const size_t comma = value.find(',', start);
      const std::string part = Trim(value.substr(
          start, comma == std::string::npos ? std::string::npos : comma - start));
      if (!part.empty()) cfg->backup_options.plugins.push_back(part);
      if (comma == std::string::npos) break;
      start = comma + 1;
    }
  }
}

std::string UnescapeJsonString(const std::string& raw) {
  std::string out;
  out.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == '\\' && i + 1 < raw.size()) {
      const char next = raw[i + 1];
      if (next == '\\' || next == '"') {
        out += next;
        ++i;
        continue;
      }
    }
    out += raw[i];
  }
  return out;
}

std::string UnquoteJsonString(const std::string& raw) {
  if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
    return raw.substr(1, raw.size() - 2);
  }
  return raw;
}

bool ParseJsonSchedule(const std::string& text, ScheduleConfig* out) {
  ScheduleConfig cfg{};
  cfg.retention_policy = DefaultRetentionPolicy();
  size_t i = 0;
  while (i < text.size()) {
    const size_t key_start = text.find('"', i);
    if (key_start == std::string::npos) break;
    const size_t key_end = text.find('"', key_start + 1);
    if (key_end == std::string::npos) break;
    const std::string key = text.substr(key_start + 1, key_end - key_start - 1);
    const size_t colon = text.find(':', key_end);
    if (colon == std::string::npos) break;
    size_t val_start = colon + 1;
    while (val_start < text.size() &&
           std::isspace(static_cast<unsigned char>(text[val_start]))) {
      ++val_start;
    }
    if (val_start >= text.size()) break;
    std::string value;
    if (text[val_start] == '"') {
      const size_t val_end = text.find('"', val_start + 1);
      if (val_end == std::string::npos) break;
      value = UnescapeJsonString(text.substr(val_start + 1, val_end - val_start - 1));
      i = val_end + 1;
    } else {
      size_t val_end = val_start;
      while (val_end < text.size() && text[val_end] != ',' && text[val_end] != '}') {
        ++val_end;
      }
      value = Trim(text.substr(val_start, val_end - val_start));
      i = val_end;
    }
    ApplyScheduleField(key, UnquoteJsonString(value), &cfg);
  }
  const bool queue_drain = cfg.mode == "queue_drain" || cfg.drain_queue;
  if (queue_drain) {
    if (cfg.repo_path.empty() && cfg.repo_base.empty()) return false;
  } else if (cfg.source_path.empty() || cfg.repo_base.empty()) {
    return false;
  }
  *out = cfg;
  return true;
}

Status RunOneBackup(const std::string& repo_path, const std::string& source_path,
                    BackupMode mode, const BackupOptions& options) {
  BackupEngine engine(repo_path);
  const Status open_st = engine.Open();
  if (!open_st.ok()) return open_st;
  return engine.RunBackup(source_path, mode, options);
}

}  // namespace

Status LoadScheduleConfig(const std::string& config_path, ScheduleConfig* out) {
  if (!out) return Status::InvalidArgument("out is null");
  std::ifstream in(config_path);
  if (!in) return Status::IoError("cannot open config: " + config_path);
  ScheduleConfig cfg{};
  cfg.retention_policy = DefaultRetentionPolicy();
  std::string line;
  while (std::getline(in, line)) {
    line = Trim(line);
    if (line.empty() || line[0] == '#') continue;
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = Trim(line.substr(0, eq));
    const std::string value = Trim(line.substr(eq + 1));
    ApplyScheduleField(key, value, &cfg);
  }
  const bool queue_drain = cfg.mode == "queue_drain" || cfg.drain_queue;
  if (queue_drain) {
    if (cfg.repo_path.empty() && cfg.repo_base.empty()) {
      return Status::InvalidArgument("queue_drain config requires repo_path or repo_base");
    }
  } else if (cfg.source_path.empty() || cfg.repo_base.empty()) {
    return Status::InvalidArgument("config requires source and repo_base");
  }
  *out = cfg;
  return Status::Ok();
}

Status LoadScheduleConfigAuto(const std::string& config_path, ScheduleConfig* out) {
  if (!out) return Status::InvalidArgument("out is null");
  std::ifstream in(config_path);
  if (!in) return Status::IoError("cannot open config: " + config_path);
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  const auto trim_start = content.find_first_not_of(" \t\r\n");
  if (trim_start == std::string::npos) {
    return Status::InvalidArgument("empty config");
  }
  if (content[trim_start] == '{') {
    ScheduleConfig cfg{};
    cfg.retention_policy = DefaultRetentionPolicy();
    if (!ParseJsonSchedule(content, &cfg)) {
      return Status::InvalidArgument("invalid JSON schedule config");
    }
    *out = cfg;
    return Status::Ok();
  }
  return LoadScheduleConfig(config_path, out);
}

std::string ScheduleRepoPath(const std::string& repo_base) {
  return (std::filesystem::path(repo_base) / "current").string();
}

bool HasLegacyRotatedRepoDirs(const std::string& repo_base) {
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(repo_base, ec)) {
    if (ec) return false;
    if (!entry.is_directory()) continue;
    const auto name = entry.path().filename().string();
    if (name.rfind("repo-", 0) == 0) return true;
  }
  return false;
}

std::string MakeRotatedRepoPath(const std::string& repo_base) {
  const std::time_t now = std::time(nullptr);
  std::tm tm_buf{};
#ifdef _WIN32
  localtime_s(&tm_buf, &now);
#else
  localtime_r(&now, &tm_buf);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm_buf);
  return (std::filesystem::path(repo_base) / ("repo-" + std::string(buf))).string();
}

void PruneRotatedRepos(const std::string& repo_base, int retain_count) {
  std::vector<std::filesystem::path> repos;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(repo_base, ec)) {
    if (!entry.is_directory()) continue;
    const auto name = entry.path().filename().string();
    if (name.rfind("repo-", 0) == 0) repos.push_back(entry.path());
  }
  std::sort(repos.begin(), repos.end());
  while (static_cast<int>(repos.size()) > retain_count) {
    std::filesystem::remove_all(repos.front(), ec);
    repos.erase(repos.begin());
  }
}

Status RunScheduledBackup(const ScheduleConfig& config, int max_runs) {
  if (config.source_path.empty() || config.repo_base.empty()) {
    return Status::InvalidArgument("schedule config incomplete");
  }
  std::error_code ec;
  std::filesystem::create_directories(config.repo_base, ec);

  const std::string repo = ScheduleRepoPath(config.repo_base);
  const std::string superblock = repo + "/superblock.bin";
  const bool new_repo = !std::filesystem::exists(superblock);
  if (new_repo) {
    RepoInitOptions init{};
    init.standard_digest = true;
    init.persistent_index = true;
    init.manifest_binary = true;
    init.snapshots = true;
    init.ebpack = true;
    init.coalesced_meta = true;
    const Status init_st = BackupEngine::InitRepoEx(repo, init);
    if (!init_st.ok()) return init_st;
  }

  int runs = 0;
  while (max_runs < 0 || runs < max_runs) {
    if (g_daemon_stop_requested.load()) break;
    BackupOptions opts = config.backup_options;
    opts.encryption_password = config.encryption_password;
    const BackupMode mode =
        (new_repo && runs == 0) ? BackupMode::kFull : BackupMode::kIncremental;
    const Status st = RunOneBackup(repo, config.source_path, mode, opts);
    if (!st.ok()) return st;

    if (config.auto_prune) {
      RetentionPolicy policy = config.retention_policy;
      if (policy.tiers.empty()) policy = DefaultRetentionPolicy();
      PruneReport prune_report{};
      const Status prune_st =
          PruneSnapshots(repo, policy, false, &prune_report);
      if (!prune_st.ok()) return prune_st;
      if (config.auto_gc_after_prune) {
        BackupEngine gc_engine(repo);
        const Status gc_open = gc_engine.Open();
        if (!gc_open.ok()) return gc_open;
        const Status gc_st = gc_engine.GcOrphans(false, nullptr, false);
        if (!gc_st.ok()) return gc_st;
      }
    }

    if (HasLegacyRotatedRepoDirs(config.repo_base)) {
      PruneRotatedRepos(config.repo_base, config.retain_count);
    }
    ++runs;
    if (max_runs >= 0 && runs >= max_runs) break;
    if (!SleepSecondsInterruptible(config.interval_seconds)) break;
  }
  return Status::Ok();
}

Status RunWatchBackup(const std::string& source_path, const std::string& repo_path,
                      const BackupOptions& options, int debounce_ms,
                      int max_triggers) {
  if (!std::filesystem::exists(source_path)) {
    return Status::NotFound("watch source not found");
  }

  FsWatch watch(source_path);
  const Status open_st = watch.Open();
  if (!open_st.ok()) return open_st;

  int triggers = 0;
  while (max_triggers < 0 || triggers < max_triggers) {
    if (g_daemon_stop_requested.load()) break;
    const Status wait_st = watch.WaitForChange(debounce_ms);
    if (!wait_st.ok()) return wait_st;
    const Status st =
        RunOneBackup(repo_path, source_path, BackupMode::kIncremental, options);
    if (!st.ok()) return st;
    ++triggers;
  }
  return Status::Ok();
}

Status RunJobQueueDrain(const std::string& repo_path, const BackupOptions& options,
                        const std::vector<std::string>& pre_enqueue_job_ids,
                        int max_cycles, int interval_seconds) {
  if (repo_path.empty()) {
    return Status::InvalidArgument("repo_path required");
  }
  BackupEngine engine(repo_path);
  const Status open_st = engine.Open();
  if (!open_st.ok()) return open_st;

  const BackupOptions opts = options;
  for (const auto& job_id : pre_enqueue_job_ids) {
    if (job_id.empty()) continue;
    const Status enq_st = engine.EnqueueJob(job_id);
    if (!enq_st.ok()) return enq_st;
  }

  int cycles = 0;
  for (;;) {
    if (g_daemon_stop_requested.load()) break;
    const Status st = engine.RunJobQueue(true, opts);
    if (!st.ok()) return st;
    ++cycles;
    if (max_cycles >= 0 && cycles >= max_cycles) break;
    if (max_cycles < 0 || cycles < max_cycles) {
      if (!SleepSecondsInterruptible(std::max(1, interval_seconds))) break;
    }
  }
  return Status::Ok();
}

void RequestDaemonStop() { g_daemon_stop_requested.store(true); }

void ResetDaemonStop() { g_daemon_stop_requested.store(false); }

bool IsDaemonStopRequested() { return g_daemon_stop_requested.load(); }

Status RunScheduleConfig(const ScheduleConfig& config, int max_runs) {
  ResetDaemonStop();
  if (config.mode == "queue_drain" || config.drain_queue) {
    const std::string repo = config.repo_path.empty()
                                 ? ScheduleRepoPath(config.repo_base)
                                 : config.repo_path;
    BackupOptions opts = config.backup_options;
    opts.encryption_password = config.encryption_password;
    return RunJobQueueDrain(repo, opts, config.job_ids, max_runs,
                            config.interval_seconds);
  }
  return RunScheduledBackup(config, max_runs);
}

}  // namespace ebbackup
