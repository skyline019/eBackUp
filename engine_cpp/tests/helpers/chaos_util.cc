#include "chaos_util.h"

#include <cstdlib>
#include <fstream>
#include <random>

#include "ebbackup/state/superblock.h"
#include "ebbackup/store/chunk_store.h"

namespace ebbackup {
namespace test {

Status FlipByteInFile(const std::string& path, uint64_t offset) {
  std::fstream io(path, std::ios::binary | std::ios::in | std::ios::out);
  if (!io) return Status::IoError("cannot open for flip: " + path);
  io.seekg(static_cast<std::streamoff>(offset));
  char byte = 0;
  io.read(&byte, 1);
  if (!io) return Status::IoError("flip read failed");
  byte ^= 0x01;
  io.seekp(static_cast<std::streamoff>(offset));
  io.write(&byte, 1);
  if (!io) return Status::IoError("flip write failed");
  return Status::Ok();
}

Status TruncateFile(const std::string& path, uint64_t new_size) {
  std::error_code ec;
  std::filesystem::resize_file(path, static_cast<std::uintmax_t>(new_size), ec);
  if (ec) return Status::IoError("truncate failed: " + ec.message());
  return Status::Ok();
}

Status CorruptRandomChunkRecord(const std::string& repo_path, uint32_t seed) {
  ChunkStore store(repo_path + "/data/chunks");
  const Status open_st = store.Open();
  if (!open_st.ok()) return open_st;

  std::vector<uint64_t> offsets;
  const Status scan = store.ForEachRecord(
      [&](const uint8_t*, uint64_t offset, uint32_t) {
        offsets.push_back(offset);
        return Status::Ok();
      });
  if (!scan.ok()) return scan;
  if (offsets.empty()) return Status::NotFound("no chunk records");

  std::mt19937 gen(seed);
  std::uniform_int_distribution<size_t> dist(0, offsets.size() - 1);
  const uint64_t off = offsets[dist(gen)];
  return store.CorruptRecordAtOffsetForTest(off);
}

Status CorruptChunkPayloadByte(const std::string& repo_path, uint32_t seed) {
  ChunkStore store(repo_path + "/data/chunks");
  const Status open_st = store.Open();
  if (!open_st.ok()) return open_st;

  struct Rec {
    uint64_t offset;
    uint32_t stored_len;
  };
  std::vector<Rec> records;
  const Status scan = store.ForEachRecord(
      [&](const uint8_t*, uint64_t offset, uint32_t stored_len) {
        records.push_back({offset, stored_len});
        return Status::Ok();
      });
  if (!scan.ok()) return scan;
  if (records.empty()) return Status::NotFound("no chunk records");

  std::mt19937 gen(seed);
  std::uniform_int_distribution<size_t> dist(0, records.size() - 1);
  const Rec rec = records[dist(gen)];
  if (rec.stored_len == 0) {
    return Status::InvalidArgument("empty stored_len");
  }
  const uint64_t payload_off =
      rec.offset + kChunkHeaderV2Size + (rec.stored_len / 2);
  return FlipByteInFile(repo_path + "/data/chunks", payload_off);
}

Status InjectPhase(const std::string& repo_path, BackupPhase phase,
                   uint64_t chunks_written) {
  BackupSuperBlockStore sb_store(repo_path + "/superblock.bin");
  BackupSuperBlock sb{};
  const Status load = sb_store.Load(&sb);
  if (!load.ok()) return load;
  SetPhase(&sb, phase);
  sb.critical.chunks_written = chunks_written;
  return sb_store.Commit(sb);
}

ScopedAbortAfterPhase::ScopedAbortAfterPhase(BackupPhase phase) {
  const std::string val = std::to_string(static_cast<int>(phase));
#ifdef _WIN32
  _putenv(("EBTEST_ABORT_AFTER=" + val).c_str());
#else
  setenv("EBTEST_ABORT_AFTER", val.c_str(), 1);
#endif
  active_ = true;
}

ScopedAbortAfterPhase::~ScopedAbortAfterPhase() {
  if (!active_) return;
#ifdef _WIN32
  _putenv("EBTEST_ABORT_AFTER=");
#else
  unsetenv("EBTEST_ABORT_AFTER");
#endif
}

}  // namespace test
}  // namespace ebbackup
