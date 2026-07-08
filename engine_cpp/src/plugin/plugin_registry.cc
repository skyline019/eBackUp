#include "ebbackup/plugin/plugin_registry.h"

#include <memory>
#include <string>
#include <vector>

namespace ebbackup {
namespace plugin {

std::unique_ptr<IBackupPlugin> MakeSqliteCheckpointPlugin();
std::unique_ptr<IBackupPlugin> MakeRegistryHivePlugin();
std::unique_ptr<IBackupPlugin> MakeVhdxScanPlugin();

namespace {

class NoOpPlugin : public IBackupPlugin {
 public:
  NoOpPlugin(const char* plugin_id, const char* reason)
      : plugin_id_(plugin_id), reason_(reason) {}

  const char* id() const override { return plugin_id_; }

  std::string PluginReportJson() const override {
    if (!reason_ || !reason_[0]) return {};
    return std::string("{\"id\":\"") + plugin_id_ + "\",\"note\":\"" + reason_ +
           "\"}";
  }

 private:
  const char* plugin_id_;
  const char* reason_;
};

struct BuiltinEntry {
  const char* id;
  const char* display;
  bool windows_only;
  std::unique_ptr<IBackupPlugin> (*factory)();
};

BuiltinEntry kBuiltins[] = {
    {"sqlite_checkpoint", "SQLite WAL checkpoint", false,
     &MakeSqliteCheckpointPlugin},
    {"registry_hive", "Registry hive export", true, &MakeRegistryHivePlugin},
    {"vhdx_scan", "VHDX read-only scan", true, &MakeVhdxScanPlugin},
};

}  // namespace

std::vector<std::string> ListBuiltinPluginIds() {
  std::vector<std::string> out;
  for (const auto& e : kBuiltins) out.push_back(e.id);
  return out;
}

std::string PluginDisplayName(const std::string& id) {
  for (const auto& e : kBuiltins) {
    if (id == e.id) return e.display;
  }
  return id;
}

std::vector<std::unique_ptr<IBackupPlugin>> CreatePlugins(
    const std::vector<std::string>& ids) {
  std::vector<std::unique_ptr<IBackupPlugin>> out;
  for (const std::string& id : ids) {
    bool found = false;
    for (const auto& e : kBuiltins) {
      if (id != e.id) continue;
      found = true;
#if !defined(_WIN32)
      if (e.windows_only) {
        out.push_back(
            std::make_unique<NoOpPlugin>(e.id, "plugin_skipped:platform"));
        break;
      }
#endif
      out.push_back(e.factory());
      break;
    }
    if (!found) {
      out.push_back(std::make_unique<NoOpPlugin>(id.c_str(), "plugin_unknown"));
    }
  }
  return out;
}

PluginSession::PluginSession(const std::string& source_path,
                             const std::vector<std::string>& plugin_ids)
    : source_path_(source_path), plugins_(CreatePlugins(plugin_ids)) {
  PluginContext ctx{};
  ctx.source_path = source_path_;
  for (auto& p : plugins_) {
    if (p) p->SetContext(ctx);
  }
}

PluginSession::~PluginSession() { ThawAll(); }

Status PluginSession::QuiesceAll(std::vector<report::BackupPathIssue>* issues) {
  for (auto& p : plugins_) {
    if (!p) continue;
    const Status st = p->Quiesce();
    if (!st.ok() && issues) {
      issues->push_back({"", std::string("plugin_quiesce_failed:") + p->id() +
                                    ":" + st.message()});
    }
    const std::string report = p->PluginReportJson();
    if (issues && report.find("plugin_skipped:platform") != std::string::npos) {
      issues->push_back({"", std::string("plugin_skipped:platform:") + p->id()});
    }
    if (issues && report.find("plugin_unknown") != std::string::npos) {
      issues->push_back({"", std::string("plugin_unknown:") + p->id()});
    }
  }
  quiesced_ = true;
  return Status::Ok();
}

void PluginSession::ThawAll() {
  if (!quiesced_) return;
  for (auto it = plugins_.rbegin(); it != plugins_.rend(); ++it) {
    if (*it) (void)(*it)->Thaw();
  }
  quiesced_ = false;
}

void PluginSession::CollectExtraScanRoots(std::vector<std::string>* out) const {
  if (!out) return;
  for (const auto& p : plugins_) {
    if (!p) continue;
    (void)p->ExtraScanRoots(out);
  }
}

void PluginSession::CollectScanHints(std::vector<ScanHint>* out) const {
  if (!out) return;
  for (const auto& p : plugins_) {
    if (!p) continue;
    (void)p->ScanHints(out);
  }
}

std::vector<std::string> PluginSession::PluginReportJsonFragments() const {
  std::vector<std::string> out;
  for (const auto& p : plugins_) {
    if (!p) continue;
    const std::string frag = p->PluginReportJson();
    if (!frag.empty()) out.push_back(frag);
  }
  return out;
}

}  // namespace plugin
}  // namespace ebbackup
