#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ebbackup {

struct ScheduledFileInput {
  size_t index{0};
  std::string path;
  std::string relative_path;
  uint64_t file_size{0};
};

// Graph-coloring scheduler: spreads large adjacent files and hash prefixes.
std::vector<std::vector<ScheduledFileInput>> ScheduleFilesByColor(
    const std::vector<ScheduledFileInput>& files, size_t worker_count);

size_t ResolvePipelineWorkerCount(size_t requested, size_t file_count = 0,
                                  uint64_t total_bytes = 0);

}  // namespace ebbackup
