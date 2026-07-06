#include "ebbackup/daemon/backup_daemon.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "ebbackup/io/fs_watch.h"
#include "ebbackup/scan/backup_filter.h"

namespace ebbackup {

namespace {

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
  if (cfg.source_path.empty() || cfg.repo_base.empty()) return false;
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
  if (cfg.source_path.empty() || cfg.repo_base.empty()) {
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
    if (!ParseJsonSchedule(content, &cfg)) {
      return Status::InvalidArgument("invalid JSON schedule config");
    }
    *out = cfg;
    return Status::Ok();
  }
  return LoadScheduleConfig(config_path, out);
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

  int runs = 0;
  while (max_runs < 0 || runs < max_runs) {
    const std::string repo = MakeRotatedRepoPath(config.repo_base);
    const Status init_st = BackupEngine::InitRepo(repo);
    if (!init_st.ok()) return init_st;
    BackupOptions opts = config.backup_options;
    opts.encryption_password = config.encryption_password;
    const Status st =
        RunOneBackup(repo, config.source_path, BackupMode::kFull, opts);
    if (!st.ok()) return st;
    PruneRotatedRepos(config.repo_base, config.retain_count);
    ++runs;
    if (max_runs >= 0 && runs >= max_runs) break;
    std::this_thread::sleep_for(std::chrono::seconds(config.interval_seconds));
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
    const Status wait_st = watch.WaitForChange(debounce_ms);
    if (!wait_st.ok()) return wait_st;
    const Status st =
        RunOneBackup(repo_path, source_path, BackupMode::kIncremental, options);
    if (!st.ok()) return st;
    ++triggers;
  }
  return Status::Ok();
}

}  // namespace ebbackup
