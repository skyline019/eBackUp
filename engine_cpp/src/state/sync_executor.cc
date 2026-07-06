#include "ebbackup/state/sync_executor.h"

namespace ebbackup {

void BackupSyncExecutor::Register(BackupEvent type, BackupHandler handler) {
  rules_.emplace_back(type, std::move(handler));
}

Status BackupSyncExecutor::Dispatch(BackupEvent type, BackupContext* ctx) {
  for (const auto& rule : rules_) {
    if (rule.first != type) continue;
    const Status st = rule.second(ctx);
    if (!st.ok()) return st;
  }
  return Status::Ok();
}

}  // namespace ebbackup
