#include <gtest/gtest.h>

#include "ebbackup/state/backup_phase.h"
#include "ebbackup/state/sync_executor.h"

namespace ebbackup {
namespace {

TEST(SyncExecutorTest, NoFallbackOnError) {
  BackupSyncExecutor exec;
  int calls = 0;
  exec.Register(BackupEvent::kScanFile, [&calls](BackupContext* ctx) -> Status {
    ++calls;
    ctx->phase = NextPhase(ctx->phase, BackupEvent::kScanFile);
    return Status::Corrupt("forced");
  });
  exec.Register(BackupEvent::kChunkFile, [&calls](BackupContext* ctx) -> Status {
    ++calls;
    ctx->phase = NextPhase(ctx->phase, BackupEvent::kChunkFile);
    return Status::Ok();
  });

  BackupContext ctx{};
  ctx.phase = BackupPhase::kIdle;
  const Status st = exec.Dispatch(BackupEvent::kScanFile, &ctx);
  EXPECT_FALSE(st.ok());
  EXPECT_EQ(calls, 1);
}

}  // namespace
}  // namespace ebbackup
