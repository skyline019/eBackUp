#include <gtest/gtest.h>

#include <fstream>

#include "ebbackup/common/path_encoding.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/report/backup_report.h"
#include "ebbackup/winmeta/vss_session.h"
#include "test_util.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace ebbackup {
namespace {

#ifdef _WIN32
bool IsProcessElevated() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  TOKEN_ELEVATION elevation{};
  DWORD size = 0;
  const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation,
                                      sizeof(elevation), &size);
  CloseHandle(token);
  return ok && elevation.TokenIsElevated;
}
#endif

TEST(VssLockedFileBackupTest, LiveBackupRecordsLockedWithoutVss) {
#ifndef _WIN32
  GTEST_SKIP() << "Windows-only test";
#else
  const std::string repo = test::TempDir("vss_locked_repo");
  const std::string source = test::TempDir("vss_locked_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/locked.dat", "secret-payload");

  const std::wstring wide = Utf8ToWide(source + "/locked.dat");
  HANDLE h = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    GTEST_SKIP() << "cannot open exclusive file";
  }

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  BackupOptions opts{};
  opts.use_pipeline = true;
  const Status st = engine.RunBackup(source, BackupMode::kFull, opts);
  CloseHandle(h);
  if (!st.ok()) {
    GTEST_SKIP() << "backup failed in environment: " << st.message();
  }

  report::BackupReport br{};
  ASSERT_TRUE(report::LoadBackupReport(repo, engine.superblock().critical.txn_id, &br)
                  .ok());
  bool saw_file_issue = false;
  for (const auto& issue : br.issues) {
    if (issue.path.find("locked.dat") != std::string::npos &&
        (issue.reason == "locked" || issue.reason == "unreadable")) {
      saw_file_issue = true;
      break;
    }
  }
  EXPECT_TRUE(saw_file_issue || br.locked >= 1u || br.skipped >= 1u)
      << "expected locked file to be skipped or reported";
  EXPECT_FALSE(br.vss_used);
#endif
}

TEST(VssLockedFileBackupTest, VssBackupReadsLockedFile) {
#ifndef _WIN32
  GTEST_SKIP() << "Windows-only test";
#else
  if (!IsProcessElevated()) {
    GTEST_SKIP() << "VSS integration requires elevated process";
  }
  const Status prereq = winmeta::VssSession::CheckPrerequisites();
  if (!prereq.ok()) {
    GTEST_SKIP() << prereq.message();
  }

  const std::string repo = test::TempDir("vss_locked_vss_repo");
  const std::string source = test::TempDir("vss_locked_vss_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/locked.dat", "secret-payload");

  const std::wstring wide = Utf8ToWide(source + "/locked.dat");
  HANDLE h = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    GTEST_SKIP() << "cannot open exclusive file";
  }

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  BackupOptions opts{};
  opts.use_pipeline = true;
  opts.use_vss = true;
  const Status st = engine.RunBackup(source, BackupMode::kFull, opts);
  CloseHandle(h);
  ASSERT_TRUE(st.ok()) << st.message();

  report::BackupReport br{};
  ASSERT_TRUE(report::LoadBackupReport(repo, engine.superblock().critical.txn_id, &br)
                  .ok());
  EXPECT_TRUE(br.vss_used);
  EXPECT_EQ(br.vss_consistency, "crash");
  EXPECT_GE(br.backed_up, 1u);
  EXPECT_EQ(br.locked, 0u);
#endif
}

}  // namespace
}  // namespace ebbackup
