#include "ebbackup/common/digest_pool.h"

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace ebbackup {

namespace {

unsigned ResolveThreadCount(unsigned requested) {
  if (requested > 0) return requested;
  const char* env = std::getenv("EBBACKUP_DIGEST_THREADS");
  if (env && env[0] != '\0') {
    const long parsed = std::strtol(env, nullptr, 10);
    if (parsed > 0) return static_cast<unsigned>(parsed);
    if (parsed == 0) return 1;
  }
  const unsigned hw = std::thread::hardware_concurrency();
  const unsigned base = hw > 0 ? hw : 8;
  return std::max<unsigned>(4, std::min(16u, base / 2));
}

}  // namespace

struct DigestJob {
  DigestAlgo algo{DigestAlgo::kStandard};
  const uint8_t* base{nullptr};
  const DigestSpan* spans{nullptr};
  size_t span_count{0};
  uint8_t* hashes_out{nullptr};
  std::mutex mu;
  std::condition_variable cv;
  bool done{false};
};

class DigestPoolImpl {
 public:
  explicit DigestPoolImpl(unsigned threads) : threads_(ResolveThreadCount(threads)) {
    workers_.reserve(threads_);
    for (unsigned i = 0; i < threads_; ++i) {
      workers_.emplace_back([this] { WorkerLoop(); });
    }
  }

  ~DigestPoolImpl() {
    {
      std::lock_guard<std::mutex> lock(queue_mu_);
      stop_ = true;
    }
    queue_cv_.notify_all();
    for (auto& worker : workers_) {
      if (worker.joinable()) worker.join();
    }
  }

  void SetThreads(unsigned threads) {
    (void)threads;
  }

  unsigned threads() const { return threads_; }

  void HashRegions(DigestAlgo algo, const uint8_t* base, const DigestSpan* spans,
                   size_t span_count, uint8_t* hashes_out) {
    if (!base || !spans || !hashes_out || span_count == 0) return;

    if (span_count == 1 || threads_ <= 1) {
      for (size_t i = 0; i < span_count; ++i) {
        ContentHash(algo, base + spans[i].offset, spans[i].length,
                    hashes_out + i * 32);
      }
      return;
    }

    if (span_count >= threads_) {
      std::vector<std::thread> helpers;
      helpers.reserve(threads_);
      for (unsigned w = 0; w < threads_; ++w) {
        const size_t begin = w * span_count / threads_;
        const size_t end = (w + 1) * span_count / threads_;
        if (begin >= end) continue;
        helpers.emplace_back([algo, base, spans, hashes_out, begin, end] {
          for (size_t i = begin; i < end; ++i) {
            ContentHash(algo, base + spans[i].offset, spans[i].length,
                        hashes_out + i * 32);
          }
        });
      }
      for (auto& helper : helpers) {
        if (helper.joinable()) helper.join();
      }
      return;
    }

    auto job = std::make_shared<DigestJob>();
    job->algo = algo;
    job->base = base;
    job->spans = spans;
    job->span_count = span_count;
    job->hashes_out = hashes_out;

    {
      std::lock_guard<std::mutex> lock(queue_mu_);
      queue_.push_back(job);
    }
    queue_cv_.notify_one();

    std::unique_lock<std::mutex> lock(job->mu);
    job->cv.wait(lock, [&] { return job->done; });
  }

 private:
  void WorkerLoop() {
    for (;;) {
      std::shared_ptr<DigestJob> job;
      {
        std::unique_lock<std::mutex> lock(queue_mu_);
        queue_cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
        if (stop_ && queue_.empty()) return;
        job = std::move(queue_.front());
        queue_.pop_front();
      }
      RunJob(job.get());
      {
        std::lock_guard<std::mutex> lock(job->mu);
        job->done = true;
      }
      job->cv.notify_one();
    }
  }

  void RunJob(DigestJob* job) {
    for (size_t i = 0; i < job->span_count; ++i) {
      ContentHash(job->algo, job->base + job->spans[i].offset,
                  job->spans[i].length, job->hashes_out + i * 32);
    }
  }

  const unsigned threads_;
  std::mutex queue_mu_;
  std::condition_variable queue_cv_;
  std::deque<std::shared_ptr<DigestJob>> queue_;
  std::vector<std::thread> workers_;
  bool stop_{false};
};

DigestPool::DigestPool(unsigned threads)
    : impl_(new DigestPoolImpl(threads)) {}

DigestPool::~DigestPool() { delete impl_; }

void DigestPool::SetThreads(unsigned threads) { impl_->SetThreads(threads); }

unsigned DigestPool::threads() const { return impl_->threads(); }

void DigestPool::HashRegions(DigestAlgo algo, const uint8_t* base,
                             const DigestSpan* spans, size_t span_count,
                             uint8_t* hashes_out) {
  impl_->HashRegions(algo, base, spans, span_count, hashes_out);
}

DigestPool& DigestPool::Shared() {
  static DigestPool pool{};
  return pool;
}

}  // namespace ebbackup
