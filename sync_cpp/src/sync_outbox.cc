#include "ebsync/sync_outbox.h"

#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ebsync {
namespace {

int64_t NowUnix() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

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
  *out = std::stoull(s.substr(start, *i - start));
  return true;
}

bool ParseJsonInt64(const std::string& s, size_t* i, int64_t* out) {
  SkipWs(s, i);
  if (*i >= s.size()) return false;
  size_t start = *i;
  if (s[*i] == '-') ++(*i);
  while (*i < s.size() && std::isdigit(static_cast<unsigned char>(s[*i]))) ++(*i);
  if (*i == start) return false;
  *out = std::stoll(s.substr(start, *i - start));
  return true;
}

void JsonEscape(const std::string& s, std::string* out) {
  *out += '"';
  for (char c : s) {
    if (c == '"' || c == '\\') *out += '\\';
    *out += c;
  }
  *out += '"';
}

OutboxState ParseState(const std::string& s) {
  if (s == "uploading") return OutboxState::kUploading;
  if (s == "done") return OutboxState::kDone;
  if (s == "failed") return OutboxState::kFailed;
  return OutboxState::kPending;
}

const char* StateName(OutboxState st) {
  switch (st) {
    case OutboxState::kUploading:
      return "uploading";
    case OutboxState::kDone:
      return "done";
    case OutboxState::kFailed:
      return "failed";
    default:
      return "pending";
  }
}

bool ParseOutboxLine(const std::string& line, SyncOutboxEntry* out) {
  if (!out || line.empty()) return false;
  size_t i = 0;
  SkipWs(line, &i);
  if (i >= line.size() || line[i] != '{') return false;
  ++i;
  SyncOutboxEntry e;
  while (i < line.size()) {
    SkipWs(line, &i);
    if (i < line.size() && line[i] == '}') break;
    std::string key;
    if (!ParseJsonString(line, &i, &key)) return false;
    SkipWs(line, &i);
    if (i >= line.size() || line[i] != ':') return false;
    ++i;
    if (key == "base_txn") {
      if (!ParseJsonUint64(line, &i, &e.base_txn)) return false;
    } else if (key == "target_txn") {
      if (!ParseJsonUint64(line, &i, &e.target_txn)) return false;
    } else if (key == "enqueued_at") {
      if (!ParseJsonInt64(line, &i, &e.enqueued_at_unix)) return false;
    } else if (key == "state") {
      std::string st;
      if (!ParseJsonString(line, &i, &st)) return false;
      e.state = ParseState(st);
    } else if (key == "last_error") {
      if (!ParseJsonString(line, &i, &e.last_error)) return false;
    } else {
      return false;
    }
    SkipWs(line, &i);
    if (i < line.size() && line[i] == ',') ++i;
  }
  if (e.target_txn == 0) return false;
  *out = std::move(e);
  return true;
}

std::string EntryToLine(const SyncOutboxEntry& e) {
  std::string o = "{";
  o += "\"base_txn\":" + std::to_string(e.base_txn);
  o += ",\"target_txn\":" + std::to_string(e.target_txn);
  o += ",\"enqueued_at\":" + std::to_string(e.enqueued_at_unix);
  o += ",\"state\":\"";
  o += StateName(e.state);
  o += "\",\"last_error\":";
  JsonEscape(e.last_error, &o);
  o += "}";
  return o;
}

}  // namespace

std::string SyncOutboxPath(const std::string& repo_path) {
  return repo_path + "/catalog/sync_outbox.jsonl";
}

bool LoadSyncOutbox(const std::string& repo_path, std::vector<SyncOutboxEntry>* out) {
  if (!out) return false;
  out->clear();
  std::ifstream in(SyncOutboxPath(repo_path));
  if (!in) return true;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    SyncOutboxEntry e;
    if (ParseOutboxLine(line, &e)) out->push_back(std::move(e));
  }
  return true;
}

bool SaveSyncOutbox(const std::string& repo_path,
                    const std::vector<SyncOutboxEntry>& entries) {
  const std::string path = SyncOutboxPath(repo_path);
  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path(path).parent_path(), ec);
  std::ofstream out(path, std::ios::trunc);
  if (!out) return false;
  for (const auto& e : entries) {
    out << EntryToLine(e) << '\n';
  }
  return static_cast<bool>(out);
}

bool EnqueueSyncOutbox(const std::string& repo_path, uint64_t base_txn,
                       uint64_t target_txn) {
  std::vector<SyncOutboxEntry> entries;
  if (!LoadSyncOutbox(repo_path, &entries)) return false;
  for (const auto& e : entries) {
    if (e.target_txn == target_txn && e.state != OutboxState::kDone) {
      return true;
    }
  }
  SyncOutboxEntry e;
  e.base_txn = base_txn;
  e.target_txn = target_txn;
  e.enqueued_at_unix = NowUnix();
  e.state = OutboxState::kPending;
  entries.push_back(e);
  return SaveSyncOutbox(repo_path, entries);
}

}  // namespace ebsync
