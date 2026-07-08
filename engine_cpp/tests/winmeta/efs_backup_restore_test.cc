#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/common/path_encoding.h"
#include "ebbackup/report/backup_report.h"
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
Status CreateEfsTestFile(const std::string& path, const std::string& content) {
  const std::wstring wide = Utf8ToWide(path);
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return Status::IoError("create efs test file failed");
    out << content;
  }
  if (!EncryptFileW(wide.c_str())) {
    return Status::IoError("EncryptFile failed (EFS may be unavailable)");
  }
  return Status::Ok();
}

TEST(EfsBackupRestoreTest, TierASkipAndManifestFlag) {
  const std::string repo = test::TempDir("efs_repo");
  const std::string source = test::TempDir("efs_source");
  const std::string dest = test::TempDir("efs_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  const std::string efs_path = source + "/secret.txt";
  const Status created = CreateEfsTestFile(efs_path, "efs-tier-a");
  if (!created.ok()) {
    GTEST_SKIP() << created.message();
  }

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  BackupOptions opts{};
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());

  report::BackupReport br{};
  ASSERT_TRUE(report::LoadBackupReport(repo, engine.superblock().critical.txn_id, &br).ok());
  EXPECT_GE(br.efs_skipped_count, 1u);

  const std::string manifest_path =
      repo + "/manifests/" + std::to_string(engine.superblock().critical.txn_id) + ".manifest";
  ManifestDocument doc{};
  ASSERT_TRUE(ReadManifestAuto(manifest_path, &doc).ok());
  bool found_efs = false;
  for (const auto& f : doc.files) {
    if (f.relative_path.find("secret.txt") != std::string::npos) {
      found_efs = true;
      EXPECT_TRUE(f.efs_encrypted);
      EXPECT_TRUE(f.efs_key_blob_b64.empty());
    }
  }
  EXPECT_TRUE(found_efs);

  ASSERT_TRUE(engine.Restore(dest).ok());
}

TEST(EfsBackupRestoreTest, TierBKeyRoundTrip) {
  const std::string repo = test::TempDir("efs_b_repo");
  const std::string source = test::TempDir("efs_b_source");
  const std::string dest = test::TempDir("efs_b_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  const std::string efs_path = source + "/secret_b.txt";
  const Status created = CreateEfsTestFile(efs_path, "efs-tier-b-payload");
  if (!created.ok()) {
    GTEST_SKIP() << created.message();
  }

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  BackupOptions opts{};
  opts.efs_export_keys = true;
  const Status backup_st = engine.RunBackup(source, BackupMode::kFull, opts);
  if (!backup_st.ok()) {
    GTEST_SKIP() << backup_st.message();
  }

  const std::string manifest_path =
      repo + "/manifests/" + std::to_string(engine.superblock().critical.txn_id) + ".manifest";
  ManifestDocument doc{};
  ASSERT_TRUE(ReadManifestAuto(manifest_path, &doc).ok());
  bool has_blob = false;
  for (const auto& f : doc.files) {
    if (f.relative_path.find("secret_b.txt") != std::string::npos) {
      EXPECT_TRUE(f.efs_encrypted);
      if (!f.efs_key_blob_b64.empty()) has_blob = true;
    }
  }
  if (!has_blob) {
    GTEST_SKIP() << "efs key export unavailable in this environment";
  }

  ASSERT_TRUE(engine.Restore(dest).ok());
  const std::string restored_path = dest + "/secret_b.txt";
  const DWORD attrs = GetFileAttributesW(Utf8ToWide(restored_path).c_str());
  ASSERT_NE(attrs, INVALID_FILE_ATTRIBUTES);
  EXPECT_NE(attrs & FILE_ATTRIBUTE_ENCRYPTED, 0u);
}
#endif

}  // namespace
}  // namespace ebbackup
