#pragma once

#include <vector>

#include "ebbackup/plugin/backup_plugin.h"

namespace ebbackup {

struct ScanHintOptions {
  std::vector<plugin::ScanHint> hints;
};

bool ShouldSkipScanPath(const std::string& absolute_path,
                        const ScanHintOptions& opts);

}  // namespace ebbackup
