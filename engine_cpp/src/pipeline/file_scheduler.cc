#include "ebbackup/pipeline/file_scheduler.h"

#include <algorithm>
#include <cstdlib>
#include <thread>
#include <vector>

namespace ebbackup {

namespace {

constexpr uint64_t kLargeFileThreshold = 256u * 1024u * 4u;
constexpr uint64_t kSingleFileParallelThreshold = 64u * 1024u * 1024u;

size_t HashSpreadBucket(const std::string& path, size_t index) {
  uint32_t h = static_cast<uint32_t>(index * 2654435761u);
  for (unsigned char c : path) {
    h = h * 31u + c;
  }
  return static_cast<size_t>(h % 16);
}

}  // namespace

size_t ResolvePipelineWorkerCount(size_t requested, size_t file_count,
                                  uint64_t total_bytes) {
  if (requested > 0) return requested;
  const char* env = std::getenv("EBBACKUP_PIPELINE_WORKERS");
  if (env && env[0] != '\0') {
    const long parsed = std::strtol(env, nullptr, 10);
    if (parsed > 0) return static_cast<size_t>(parsed);
  }
  const unsigned hw = std::thread::hardware_concurrency();
  const unsigned base = hw > 0 ? hw : 8;
  if (file_count <= 1 && total_bytes >= kSingleFileParallelThreshold) {
    return std::max<size_t>(4, std::min<size_t>(16, base / 2));
  }
  if (file_count <= 1 || total_bytes < kSingleFileParallelThreshold) {
    return std::max<size_t>(2, base / 4);
  }
  return std::max<size_t>(4, std::min<size_t>(16, base / 2));
}

std::vector<std::vector<ScheduledFileInput>> ScheduleFilesByColor(
    const std::vector<ScheduledFileInput>& files, size_t worker_count) {
  if (worker_count == 0) worker_count = 1;
  std::vector<std::vector<ScheduledFileInput>> queues(worker_count);

  struct Scored {
    ScheduledFileInput file;
    size_t preferred{0};
  };
  std::vector<Scored> scored;
  scored.reserve(files.size());
  for (size_t i = 0; i < files.size(); ++i) {
    Scored s{};
    s.file = files[i];
    s.preferred = (HashSpreadBucket(files[i].path, files[i].index) + i) %
                  worker_count;
    scored.push_back(std::move(s));
  }

  std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
    if (a.file.file_size != b.file.file_size) {
      return a.file.file_size > b.file.file_size;
    }
    return a.file.index < b.file.index;
  });

  std::vector<uint64_t> queue_bytes(worker_count, 0);
  std::vector<bool> last_was_large(worker_count, false);

  for (const Scored& item : scored) {
    size_t best = item.preferred;
    uint64_t best_load = queue_bytes[best];
    const bool large = item.file.file_size >= kLargeFileThreshold;

    for (size_t w = 0; w < worker_count; ++w) {
      if (large && last_was_large[w]) continue;
      if (queue_bytes[w] < best_load) {
        best_load = queue_bytes[w];
        best = w;
      }
    }

    queues[best].push_back(item.file);
    queue_bytes[best] += item.file.file_size;
    last_was_large[best] = large;
  }

  for (auto& q : queues) {
    std::sort(q.begin(), q.end(),
              [](const ScheduledFileInput& a, const ScheduledFileInput& b) {
                return a.index < b.index;
              });
  }
  return queues;
}

}  // namespace ebbackup
