#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ebbackup/plugin/backup_plugin.h"
#include "ebbackup/report/backup_report.h"

namespace ebbackup {
namespace plugin {

std::vector<std::string> ListBuiltinPluginIds();
std::string PluginDisplayName(const std::string& id);
std::vector<std::unique_ptr<IBackupPlugin>> CreatePlugins(
    const std::vector<std::string>& ids);

class PluginSession {
 public:
  PluginSession() = default;
  PluginSession(const std::string& source_path,
                const std::vector<std::string>& plugin_ids);
  ~PluginSession();

  PluginSession(const PluginSession&) = delete;
  PluginSession& operator=(const PluginSession&) = delete;

  Status QuiesceAll(std::vector<report::BackupPathIssue>* issues);
  void CollectExtraScanRoots(std::vector<std::string>* out) const;
  void CollectScanHints(std::vector<ScanHint>* out) const;
  std::vector<std::string> PluginReportJsonFragments() const;
  void EndQuiesce() { ThawAll(); }

 private:
  std::string source_path_;
  std::vector<std::unique_ptr<IBackupPlugin>> plugins_;
  bool quiesced_{false};
  void ThawAll();
};

}  // namespace plugin
}  // namespace ebbackup
