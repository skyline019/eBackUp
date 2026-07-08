#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "ebbackup/common/path_encoding.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/scan/scan_entry.h"
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
namespace {

std::string ToWinPath(std::string path) {
  for (char& c : path) {
    if (c == '/') c = '\\';
  }
  return path;
}

bool WriteAlternateStream(const std::string& host_path,
                          const std::string& stream_name,
                          const std::string& content) {
  const std::wstring wide =
      Utf8ToWide(ToWinPath(host_path) + ":" + stream_name);
  HANDLE h = CreateFileW(wide.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const BOOL ok =
      WriteFile(h, content.data(), static_cast<DWORD>(content.size()), &written,
                nullptr) &&
      written == content.size();
  CloseHandle(h);
  return ok != FALSE;
}

bool ReadAlternateStream(const std::string& host_path,
                         const std::string& stream_name, std::string* out) {
  if (!out) return false;
  out->clear();
  const std::wstring wide =
      Utf8ToWide(ToWinPath(host_path) + ":" + stream_name);
  HANDLE h = CreateFileW(wide.c_str(), GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 ||
      size.QuadPart > 16 * 1024 * 1024) {
    CloseHandle(h);
    return false;
  }
  out->resize(static_cast<size_t>(size.QuadPart));
  DWORD read = 0;
  const BOOL ok = size.QuadPart == 0 ||
                  ReadFile(h, out->data(), static_cast<DWORD>(out->size()), &read,
                           nullptr);
  CloseHandle(h);
  if (!ok) {
    out->clear();
    return false;
  }
  out->resize(read);
  return true;
}

}  // namespace

TEST(AdsBackupRestoreTest, ZoneIdentifierRoundTrip) {
  const std::string repo = test::TempDir("ads_repo");
  const std::string source = test::TempDir("ads_source");
  const std::string dest = test::TempDir("ads_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  const std::string host = source + "/file.txt";
  test::WriteFile(host, "main-body");
  ASSERT_TRUE(WriteAlternateStream(host, "Zone.Identifier", "zone-payload"))
      << "CreateFileW ADS write failed";

  ScanResult scan;
  ASSERT_TRUE(ScanDirectory(source, &scan).ok());
  bool saw_ads = false;
  for (const auto& entry : scan.entries) {
    if (entry.stream_name == "Zone.Identifier") saw_ads = true;
  }
  ASSERT_TRUE(saw_ads) << "scanner did not enumerate Zone.Identifier stream";

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  ASSERT_TRUE(engine.Verify().ok());
  ASSERT_TRUE(engine.Restore(dest).ok());

  std::ifstream main_in(dest + "/file.txt");
  const std::string main_body((std::istreambuf_iterator<char>(main_in)),
                              std::istreambuf_iterator<char>());
  EXPECT_EQ(main_body, "main-body");

  std::string ads_body;
  ASSERT_TRUE(ReadAlternateStream(dest + "/file.txt", "Zone.Identifier", &ads_body))
      << "Zone.Identifier stream missing after restore";
  EXPECT_EQ(ads_body, "zone-payload");
}
#endif

}  // namespace
}  // namespace ebbackup
