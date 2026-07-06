#pragma once

#include <functional>
#include <vector>

#include "ebbackup/common/status.h"
#include "ebbackup/state/backup_phase.h"

namespace ebbackup {

class BackupEngine;

struct BackupContext {
  BackupEngine* engine{nullptr};
  BackupPhase phase{BackupPhase::kIdle};
};

using BackupHandler = std::function<Status(BackupContext*)>;

class BackupSyncExecutor {
 public:
  void Register(BackupEvent type, BackupHandler handler);
  Status Dispatch(BackupEvent type, BackupContext* ctx);

 private:
  std::vector<std::pair<BackupEvent, BackupHandler>> rules_;
};

}  // namespace ebbackup
