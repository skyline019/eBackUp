#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/common/path_encoding.h"
#include "ebbackup/report/backup_report.h"
#include "test_util.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#endif

namespace ebbackup {
namespace {

#ifdef _WIN32
Status CreateTestSparseFile(const std::string& path, uint64_t logical_size,
                            uint64_t data_offset, const std::vector<uint8_t>& payload) {
  const std::wstring wide = Utf8ToWide(path);
  HANDLE h = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return Status::IoError("CreateFile sparse test file failed");
  }
  DWORD bytes = 0;
  if (!DeviceIoControl(h, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &bytes, nullptr)) {
    CloseHandle(h);
    return Status::IoError("FSCTL_SET_SPARSE failed");
  }
  if (!payload.empty()) {
    LARGE_INTEGER pos{};
    pos.QuadPart = static_cast<LONGLONG>(data_offset);
    if (!SetFilePointerEx(h, pos, nullptr, FILE_BEGIN)) {
      CloseHandle(h);
      return Status::IoError("sparse test seek failed");
    }
    DWORD written = 0;
    if (!WriteFile(h, payload.data(), static_cast<DWORD>(payload.size()), &written,
                   nullptr) ||
        written != payload.size()) {
      CloseHandle(h);
      return Status::IoError("sparse test write failed");
    }
  }
  if (logical_size > 0) {
    LARGE_INTEGER end_pos{};
    end_pos.QuadPart = static_cast<LONGLONG>(logical_size - 1);
    if (!SetFilePointerEx(h, end_pos, nullptr, FILE_BEGIN)) {
      CloseHandle(h);
      return Status::IoError("sparse test SetEnd seek failed");
    }
    uint8_t zero = 0;
    DWORD written = 0;
    if (!WriteFile(h, &zero, 1, &written, nullptr) || written != 1) {
      CloseHandle(h);
      return Status::IoError("sparse test SetEnd write failed");
    }
  }
  CloseHandle(h);
  return Status::Ok();
}

TEST(SparseBackupRestoreTest, RoundTripSparsePayload) {
  const std::string repo = test::TempDir("sparse_repo");
  const std::string source = test::TempDir("sparse_source");
  const std::string dest = test::TempDir("sparse_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  const std::vector<uint8_t> payload = {'s', 'p', 'a', 'r', 's', 'e', '-', 'd', 'a', 't', 'a'};
  const std::string sparse_path = source + "/large.bin";
  const uint64_t logical_size = 8u * 1024u * 1024u;
  const uint64_t data_offset = 512u * 1024u;
  ASSERT_TRUE(CreateTestSparseFile(sparse_path, logical_size, data_offset, payload).ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  BackupOptions opts{};
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());
  ASSERT_TRUE(engine.Verify().ok());

  report::BackupReport br{};
  ASSERT_TRUE(report::LoadBackupReport(repo, engine.superblock().critical.txn_id, &br).ok());
  EXPECT_GE(br.sparse_file_count, 1u);

  ASSERT_TRUE(engine.Restore(dest).ok());

  std::ifstream restored(dest + "/large.bin", std::ios::binary);
  ASSERT_TRUE(restored.good());
  restored.seekg(0, std::ios::end);
  const auto restored_size = static_cast<uint64_t>(restored.tellg());
  EXPECT_EQ(restored_size, logical_size);

  std::vector<char> at_offset(payload.size());
  restored.seekg(static_cast<std::streamoff>(data_offset));
  restored.read(at_offset.data(), static_cast<std::streamsize>(payload.size()));
  ASSERT_TRUE(restored.good());
  EXPECT_EQ(std::memcmp(at_offset.data(), payload.data(), payload.size()), 0);

  std::vector<char> hole(4096);
  restored.seekg(0);
  restored.read(hole.data(), static_cast<std::streamsize>(hole.size()));
  for (char c : hole) {
    EXPECT_EQ(static_cast<unsigned char>(c), 0u);
  }
}
#endif

}  // namespace
}  // namespace ebbackup
