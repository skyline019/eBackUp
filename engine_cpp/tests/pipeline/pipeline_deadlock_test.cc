#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

namespace ebbackup {
namespace {

#if defined(_WIN32)
void SetTestEnv(const char* key, const char* value) {
  _putenv_s(key, value);
}
void ClearTestEnv(const char* key) {
  _putenv_s(key, "");
}
#else
void SetTestEnv(const char* key, const char* value) {
  setenv(key, value, 1);
}
void ClearTestEnv(const char* key) {
  unsetenv(key);
}
#endif

template <typename T>
class TestBoundedQueue {
 public:
  explicit TestBoundedQueue(size_t capacity) : capacity_(capacity) {}

  bool Push(T value) {
    std::unique_lock<std::mutex> lock(mu_);
    not_full_.wait(lock, [this] { return closed_ || queue_.size() < capacity_; });
    if (closed_) return false;
    queue_.push_back(std::move(value));
    not_empty_.notify_one();
    return true;
  }

  void Close() {
    std::lock_guard<std::mutex> lock(mu_);
    closed_ = true;
    not_full_.notify_all();
    not_empty_.notify_all();
  }

 private:
  std::mutex mu_;
  std::condition_variable not_full_;
  std::condition_variable not_empty_;
  std::deque<T> queue_;
  size_t capacity_{1};
  bool closed_{false};
};

TEST(PipelineDeadlockTest, BoundedQueueCloseUnblocksPush) {
  TestBoundedQueue<int> queue(1);
  ASSERT_TRUE(queue.Push(1));

  std::promise<bool> push_done;
  std::thread blocked([&] {
    const bool ok = queue.Push(2);
    push_done.set_value(ok);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  queue.Close();

  auto future = push_done.get_future();
  ASSERT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
  EXPECT_FALSE(future.get());
  blocked.join();
}

TEST(PipelineDeadlockTest, PipelineManyFilesCompletesUnderTimeout) {
  const std::string source = test::TempDir("pipeline_deadlock_many_source");
  for (int i = 0; i < 60; ++i) {
    const std::string name = "f" + std::to_string(i) + ".bin";
    test::WriteFile(source + "/" + name,
                    test::MakeSyntheticData(4096, static_cast<uint8_t>(60 + i)));
  }

  const std::string repo = test::TempDir("pipeline_deadlock_many_repo");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  BackupOptions opts{};
  opts.use_pipeline = true;

  auto backup_future = std::async(std::launch::async, [&] {
    BackupEngine engine(repo);
    if (!engine.Open().ok()) return false;
    return engine.RunBackup(source, BackupMode::kFull, opts).ok();
  });

  ASSERT_EQ(backup_future.wait_for(std::chrono::seconds(30)),
            std::future_status::ready);
  EXPECT_TRUE(backup_future.get());
}

TEST(PipelineDeadlockTest, PipelineSetErrorDoesNotHangJoin) {
  const std::string source = test::TempDir("pipeline_deadlock_fail_source");
  for (int i = 0; i < 40; ++i) {
    const std::string name = "f" + std::to_string(i) + ".bin";
    test::WriteFile(source + "/" + name,
                    test::MakeSyntheticData(8192, static_cast<uint8_t>(80 + i)));
  }

  const std::string repo = test::TempDir("pipeline_deadlock_fail_repo");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  SetTestEnv("EBTEST_PIPELINE_STORE_FAIL", "1");

  BackupOptions opts{};
  opts.use_pipeline = true;

  auto backup_future = std::async(std::launch::async, [&] {
    BackupEngine engine(repo);
    if (!engine.Open().ok()) return Status::IoError("open failed");
    return engine.RunBackup(source, BackupMode::kFull, opts);
  });

  ASSERT_EQ(backup_future.wait_for(std::chrono::seconds(10)),
            std::future_status::ready);
  const Status st = backup_future.get();
  EXPECT_FALSE(st.ok());

  ClearTestEnv("EBTEST_PIPELINE_STORE_FAIL");
}

}  // namespace
}  // namespace ebbackup
