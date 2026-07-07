#pragma once

#include <cstdint>

namespace ebbackup {

enum class BackupPhase : uint8_t {
  kIdle = 0,
  kScanning = 1,
  kChunking = 2,
  kStoring = 3,
  kCommittingMeta = 4,
  kAuditing = 5,
  kComplete = 6,
  kAborted = 0xFF,
};

enum class BackupEvent {
  kOpen,
  kScanFile,
  kChunkFile,
  kStoreChunk,
  kCommitManifest,
  kAppendAudit,
  kVerify,
  kRecover,
  kGcOrphans,
  kComplete,
};

inline BackupPhase NextPhase(BackupPhase current, BackupEvent event) {
  switch (current) {
    case BackupPhase::kIdle:
    case BackupPhase::kAborted:
      if (event == BackupEvent::kScanFile) return BackupPhase::kScanning;
      break;
    case BackupPhase::kScanning:
      if (event == BackupEvent::kChunkFile) return BackupPhase::kChunking;
      break;
    case BackupPhase::kChunking:
      if (event == BackupEvent::kStoreChunk) return BackupPhase::kStoring;
      break;
    case BackupPhase::kStoring:
      if (event == BackupEvent::kCommitManifest) return BackupPhase::kCommittingMeta;
      break;
    case BackupPhase::kCommittingMeta:
      if (event == BackupEvent::kComplete) return BackupPhase::kIdle;
      if (event == BackupEvent::kAppendAudit) return BackupPhase::kAuditing;
      break;
    case BackupPhase::kAuditing:
      if (event == BackupEvent::kComplete) return BackupPhase::kIdle;
      break;
    default:
      break;
  }
  return BackupPhase::kAborted;
}

}  // namespace ebbackup
