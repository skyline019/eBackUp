#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"

namespace ebbackup {

class BackupEngine;
struct BackupOptions;

namespace queue {

enum class JobQueueState : uint8_t { kIdle = 0, kRunning = 1, kDone = 2 };

struct JobQueueEnqueueOptions {
  bool incremental{false};
  uint32_t flags{0};
};

struct QueuedJob {
  std::string job_id;
  uint64_t enqueued_at_unix{0};
  bool incremental{false};
  uint32_t flags{0};
};

struct JobQueueRunReport {
  std::string job_id;
  Status run_status;
};

std::string JobQueueFilePath(const std::string& repo_path);

Status LoadJobQueue(const std::string& repo_path, std::vector<QueuedJob>* out);
Status SaveJobQueue(const std::string& repo_path,
                    const std::vector<QueuedJob>& jobs);

class JobQueue {
 public:
  explicit JobQueue(std::string repo_path = {});

  const std::string& repo_path() const { return repo_path_; }
  JobQueueState state() const { return state_; }
  size_t pending_count() const { return pending_.size(); }
  const std::vector<QueuedJob>& pending() const { return pending_; }

  Status Load();
  Status Save() const;
  Status Enqueue(const std::string& job_id,
                 const JobQueueEnqueueOptions& opts = {});
  Status RunNext(BackupEngine* engine, const BackupOptions& base_options,
                 JobQueueRunReport* report = nullptr);

  std::string StatusJson() const;

 private:
  std::string repo_path_;
  std::vector<QueuedJob> pending_;
  JobQueueState state_{JobQueueState::kIdle};
};

}  // namespace queue
}  // namespace ebbackup
