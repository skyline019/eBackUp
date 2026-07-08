#include "ebbackup/plugin/backup_plugin.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "ebbackup/common/path_util.h"

#ifdef EBBACKUP_HAVE_SQLITE3
#include "sqlite3.h"
#endif

namespace ebbackup {
namespace plugin {
namespace {

bool EndsWithIgnoreCase(const std::string& path, const std::string& suffix) {
  if (path.size() < suffix.size()) return false;
  const size_t off = path.size() - suffix.size();
  for (size_t i = 0; i < suffix.size(); ++i) {
    char a = path[off + i];
    char b = suffix[i];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

bool IsSqliteDbPath(const std::string& path) {
  return EndsWithIgnoreCase(path, ".db") || EndsWithIgnoreCase(path, ".sqlite");
}

void CollectSqliteDbPaths(const std::filesystem::path& root,
                          std::vector<std::string>* out) {
  if (!out) return;
  std::error_code ec;
  if (!std::filesystem::exists(root, ec)) return;
  for (auto it = std::filesystem::recursive_directory_iterator(
           root, std::filesystem::directory_options::skip_permission_denied, ec);
       it != std::filesystem::recursive_directory_iterator(); ++it) {
    if (ec) break;
    std::error_code fec;
    if (!it->is_regular_file(fec)) continue;
    const std::string abs = PathToUtf8(it->path());
    if (IsSqliteDbPath(abs)) out->push_back(abs);
  }
}

class SqliteCheckpointPlugin : public IBackupPlugin {
 public:
  const char* id() const override { return "sqlite_checkpoint"; }

  Status Quiesce() override {
    checkpointed_.clear();
    hints_.clear();
    failures_.clear();
    if (ctx_.source_path.empty()) return Status::Ok();

    std::vector<std::string> db_paths;
    CollectSqliteDbPaths(PathFromUtf8(ctx_.source_path), &db_paths);

#ifdef EBBACKUP_HAVE_SQLITE3
    for (const std::string& db : db_paths) {
      sqlite3* handle = nullptr;
      const int open_rc = sqlite3_open_v2(
          db.c_str(), &handle, SQLITE_OPEN_READONLY, nullptr);
      if (open_rc != SQLITE_OK || !handle) {
        failures_.push_back(db);
        if (handle) sqlite3_close(handle);
        continue;
      }
      char* err = nullptr;
      const int cp_rc =
          sqlite3_exec(handle, "PRAGMA wal_checkpoint(FULL);", nullptr, nullptr, &err);
      if (cp_rc != SQLITE_OK) {
        failures_.push_back(db);
        if (err) sqlite3_free(err);
      } else {
        checkpointed_.push_back(db);
        ScanHint wal{};
        wal.path_prefix = db + "-wal";
        wal.skip_subtree = true;
        hints_.push_back(wal);
        ScanHint shm{};
        shm.path_prefix = db + "-shm";
        shm.skip_subtree = true;
        hints_.push_back(shm);
      }
      sqlite3_close(handle);
    }
#else
    (void)db_paths;
#endif
    return Status::Ok();
  }

  Status ScanHints(std::vector<ScanHint>* out) override {
    if (!out) return Status::InvalidArgument("out is null");
    out->insert(out->end(), hints_.begin(), hints_.end());
    return Status::Ok();
  }

  std::string PluginReportJson() const override {
    std::ostringstream os;
    os << "{\"id\":\"sqlite_checkpoint\",\"checkpointed\":" << checkpointed_.size()
       << ",\"failed\":" << failures_.size() << "}";
    return os.str();
  }

 private:
  std::vector<std::string> checkpointed_;
  std::vector<std::string> failures_;
  std::vector<ScanHint> hints_;
};

}  // namespace

std::unique_ptr<IBackupPlugin> MakeSqliteCheckpointPlugin() {
  return std::make_unique<SqliteCheckpointPlugin>();
}

}  // namespace plugin
}  // namespace ebbackup
