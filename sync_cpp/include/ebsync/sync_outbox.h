#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ebsync {

enum class OutboxState : uint8_t {
  kPending = 0,
  kUploading = 1,
  kDone = 2,
  kFailed = 3,
};

struct SyncOutboxEntry {
  uint64_t base_txn{0};
  uint64_t target_txn{0};
  int64_t enqueued_at_unix{0};
  OutboxState state{OutboxState::kPending};
  std::string last_error;
};

std::string SyncOutboxPath(const std::string& repo_path);

bool LoadSyncOutbox(const std::string& repo_path, std::vector<SyncOutboxEntry>* out);
bool SaveSyncOutbox(const std::string& repo_path,
                    const std::vector<SyncOutboxEntry>& entries);

bool EnqueueSyncOutbox(const std::string& repo_path, uint64_t base_txn,
                       uint64_t target_txn);

}  // namespace ebsync
