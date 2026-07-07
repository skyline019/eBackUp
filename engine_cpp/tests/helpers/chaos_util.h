#pragma once

#include <cstdint>
#include <string>

#include "ebbackup/common/status.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/state/backup_phase.h"

namespace ebbackup {
namespace test {

Status FlipByteInFile(const std::string& path, uint64_t offset);

Status TruncateFile(const std::string& path, uint64_t new_size);

Status CorruptRandomChunkRecord(const std::string& repo_path, uint32_t seed);

Status CorruptChunkPayloadByte(const std::string& repo_path, uint32_t seed);

Status InjectPhase(const std::string& repo_path, BackupPhase phase,
                   uint64_t chunks_written);

class ScopedAbortAfterPhase {
 public:
  explicit ScopedAbortAfterPhase(BackupPhase phase);
  ~ScopedAbortAfterPhase();

  ScopedAbortAfterPhase(const ScopedAbortAfterPhase&) = delete;
  ScopedAbortAfterPhase& operator=(const ScopedAbortAfterPhase&) = delete;

 private:
  bool active_{false};
};

}  // namespace test
}  // namespace ebbackup
