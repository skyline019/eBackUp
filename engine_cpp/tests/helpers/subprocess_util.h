#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"
#include "ebbackup/engine/backup_engine.h"

namespace ebbackup {
namespace test {

Status RunBackupSubprocessAndKill(const std::string& repo,
                                  const std::string& source, BackupMode mode,
                                  const BackupOptions& options, int delay_ms);

Status RunCliSubprocessAndKill(const std::string& args, int delay_ms);

inline Status RunBackupSubprocessAndKill(const std::string& repo,
                                         const std::string& source,
                                         uint32_t flags = 0,
                                         int delay_ms = 50) {
  BackupOptions opts{};
  opts.use_pipeline = (flags & 0x1) != 0;
  opts.disable_pipeline = (flags & 0x2) != 0;
  return RunBackupSubprocessAndKill(repo, source, BackupMode::kFull, opts,
                                    delay_ms);
}

}  // namespace test
}  // namespace ebbackup
