#include "ebbackup/queue/job_queue.h"

#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ebbackup/engine/backup_engine.h"

namespace ebbackup {
namespace queue {

namespace {

void SkipWs(const std::string& s, size_t* i) {
  while (*i < s.size() && std::isspace(static_cast<unsigned char>(s[*i]))) ++(*i);
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

Status ParseJsonBool(const std::string& s, size_t* i, bool* out) {
  SkipWs(s, i);
  if (s.compare(*i, 4, "true") == 0) {
    *i += 4;
    *out = true;
    return Status::Ok();
  }
  if (s.compare(*i, 5, "false") == 0) {
    *i += 5;
    *out = false;
    return Status::Ok();
  }
  return Status::InvalidArgument("expected bool");
}

Status ParseJsonUint64(const std::string& s, size_t* i, uint64_t* out) {
  SkipWs(s, i);
  if (*i >= s.size()) return Status::InvalidArgument("expected number");
  size_t start = *i;
  while (*i < s.size() && std::isdigit(static_cast<unsigned char>(s[*i]))) ++(*i);
  if (*i == start) return Status::InvalidArgument("expected number");
  try {
    *out = std::stoull(s.substr(start, *i - start));
    return Status::Ok();
  } catch (...) {
    return Status::InvalidArgument("bad number");
  }
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

std::string QueuedJobToJsonLine(const QueuedJob& job) {
  std::string line = "{";
  line += "\"job_id\":";
  JsonEscape(job.job_id, &line);
  line += ",\"enqueued_at\":" + std::to_string(job.enqueued_at_unix);
  line += job.incremental ? ",\"incremental\":true" : ",\"incremental\":false";
  line += ",\"flags\":" + std::to_string(job.flags);
  line += "}";
  return line;
}

Status ParseQueuedJobLine(const std::string& line, QueuedJob* out) {
  if (!out) return Status::InvalidArgument("out is null");
  size_t i = 0;
  SkipWs(line, &i);
  if (i >= line.size() || line[i] != '{') {
    return Status::InvalidArgument("job queue line must be object");
  }
  ++i;
  QueuedJob job;
  while (i < line.size()) {
    SkipWs(line, &i);
    if (i < line.size() && line[i] == '}') break;
    std::string key;
    const Status key_st = ParseJsonString(line, &i, &key);
    if (!key_st.ok()) return key_st;
    SkipWs(line, &i);
    if (i >= line.size() || line[i] != ':') {
      return Status::InvalidArgument("expected colon");
    }
    ++i;
    if (key == "job_id") {
      const Status st = ParseJsonString(line, &i, &job.job_id);
      if (!st.ok()) return st;
    } else if (key == "enqueued_at") {
      const Status st = ParseJsonUint64(line, &i, &job.enqueued_at_unix);
      if (!st.ok()) return st;
    } else if (key == "incremental") {
      const Status st = ParseJsonBool(line, &i, &job.incremental);
      if (!st.ok()) return st;
    } else if (key == "flags") {
      uint64_t flags = 0;
      const Status st = ParseJsonUint64(line, &i, &flags);
      if (!st.ok()) return st;
      job.flags = static_cast<uint32_t>(flags);
    } else {
      return Status::InvalidArgument("unknown job queue field");
    }
    SkipWs(line, &i);
    if (i < line.size() && line[i] == ',') ++i;
  }
  if (job.job_id.empty()) return Status::InvalidArgument("job_id missing");
  *out = std::move(job);
  return Status::Ok();
}

}  // namespace

std::string JobQueueFilePath(const std::string& repo_path) {
  return repo_path + "/catalog/job_queue.jsonl";
}

Status LoadJobQueue(const std::string& repo_path, std::vector<QueuedJob>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  const std::string path = JobQueueFilePath(repo_path);
  std::ifstream in(path);
  if (!in) return Status::Ok();
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    QueuedJob job;
    const Status st = ParseQueuedJobLine(line, &job);
    if (!st.ok()) return st;
    out->push_back(std::move(job));
  }
  return Status::Ok();
}

Status SaveJobQueue(const std::string& repo_path,
                    const std::vector<QueuedJob>& jobs) {
  std::error_code ec;
  std::filesystem::create_directories(repo_path + "/catalog", ec);
  const std::string path = JobQueueFilePath(repo_path);
  const std::string temp = path + ".new";
  std::ofstream out(temp, std::ios::trunc);
  if (!out) return Status::IoError("cannot write job queue");
  for (const auto& job : jobs) {
    out << QueuedJobToJsonLine(job) << '\n';
  }
  if (!out) return Status::IoError("job queue write failed");
  out.close();
  std::filesystem::rename(temp, path, ec);
  if (ec) return Status::IoError("job queue rename failed: " + ec.message());
  return Status::Ok();
}

JobQueue::JobQueue(std::string repo_path) : repo_path_(std::move(repo_path)) {}

Status JobQueue::Load() {
  if (repo_path_.empty()) return Status::InvalidArgument("repo_path empty");
  return LoadJobQueue(repo_path_, &pending_);
}

Status JobQueue::Save() const {
  if (repo_path_.empty()) return Status::InvalidArgument("repo_path empty");
  return SaveJobQueue(repo_path_, pending_);
}

Status JobQueue::Enqueue(const std::string& job_id,
                         const JobQueueEnqueueOptions& opts) {
  if (job_id.empty()) return Status::InvalidArgument("job_id empty");
  QueuedJob job;
  job.job_id = job_id;
  job.enqueued_at_unix = static_cast<uint64_t>(std::time(nullptr));
  job.incremental = opts.incremental;
  job.flags = opts.flags;
  pending_.push_back(std::move(job));
  state_ = JobQueueState::kIdle;
  return Status::Ok();
}

Status JobQueue::RunNext(BackupEngine* engine, const BackupOptions& base_options,
                         JobQueueRunReport* report) {
  if (!engine) return Status::InvalidArgument("engine is null");
  if (pending_.empty()) return Status::NotFound("queue empty");
  state_ = JobQueueState::kRunning;
  QueuedJob job = pending_.front();
  pending_.erase(pending_.begin());
  if (!repo_path_.empty()) {
    const Status save_st = Save();
    if (!save_st.ok()) return save_st;
  }

  BackupOptions opts = base_options;
  opts.use_lz4 = opts.use_lz4 || ((job.flags & 1u) != 0);
  const auto mode =
      job.incremental ? BackupMode::kIncremental : BackupMode::kFull;
  const Status st = engine->RunJob(job.job_id, mode, opts);
  state_ = pending_.empty() ? JobQueueState::kIdle : JobQueueState::kRunning;
  if (pending_.empty() && st.ok()) state_ = JobQueueState::kDone;
  if (!st.ok() && st.code() == StatusCode::kConflict &&
      st.message() == "outside backup window") {
    state_ = pending_.empty() ? JobQueueState::kIdle : JobQueueState::kRunning;
    if (report) {
      report->job_id = job.job_id;
      report->run_status = Status::Ok();
    }
    return Status::Ok();
  }
  if (report) {
    report->job_id = job.job_id;
    report->run_status = st;
  }
  return st;
}

std::string JobQueue::StatusJson() const {
  std::string j = "{\"ok\":true,\"pending_count\":" +
                  std::to_string(pending_.size()) + ",\"state\":\"";
  switch (state_) {
    case JobQueueState::kRunning:
      j += "running";
      break;
    case JobQueueState::kDone:
      j += "done";
      break;
    default:
      j += "idle";
      break;
  }
  j += "\",\"jobs\":[";
  for (size_t i = 0; i < pending_.size(); ++i) {
    if (i) j += ',';
    const auto& job = pending_[i];
    j += '{';
    j += "\"job_id\":";
    JsonEscape(job.job_id, &j);
    j += ",\"enqueued_at\":" + std::to_string(job.enqueued_at_unix);
    j += job.incremental ? ",\"incremental\":true" : ",\"incremental\":false";
    j += ",\"flags\":" + std::to_string(job.flags);
    j += '}';
  }
  j += "]}";
  return j;
}

}  // namespace queue
}  // namespace ebbackup
