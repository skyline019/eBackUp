#pragma once

#include <string>
#include <vector>

#include "ebbackup/common/status.h"

namespace ebbackup {
namespace plugin {

struct ScanHint {
  std::string path_prefix;
  bool skip_subtree{false};
};

struct PluginContext {
  std::string source_path;
};

class IBackupPlugin {
 public:
  virtual ~IBackupPlugin() = default;

  virtual const char* id() const = 0;

  virtual void SetContext(const PluginContext& ctx) { ctx_ = ctx; }

  virtual Status Quiesce() { return Status::Ok(); }
  virtual Status Thaw() { return Status::Ok(); }

  virtual Status ScanHints(std::vector<ScanHint>* out) {
    (void)out;
    return Status::Ok();
  }

  virtual Status ExtraScanRoots(std::vector<std::string>* out) {
    (void)out;
    return Status::Ok();
  }

  virtual std::string PluginReportJson() const { return {}; }

 protected:
  PluginContext ctx_;
};

}  // namespace plugin
}  // namespace ebbackup
