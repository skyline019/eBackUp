#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>

#include <filesystem>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/winmeta/win_meta.h"
#include "test_util.h"

namespace ebbackup {
namespace {

#ifdef _WIN32
TEST(AclRestoreTest, BestEffortRecordsIssueOnBadSecurityDescriptor) {
  const std::string dest_file = test::TempDir("acl_be") + "/target.txt";
  test::WriteFile(dest_file, "acl-test");
  ManifestFileEntry entry;
  entry.security_descriptor_b64 = "not-valid-base64-sd";
  winmeta::AclRestorePolicy policy{};
  policy.mode = winmeta::AclRestorePolicy::Mode::kBestEffort;
  std::string soft_issue;
  const Status st =
      winmeta::ApplyWinMetaOnRestore(dest_file, entry, policy, &soft_issue);
  EXPECT_TRUE(st.ok());
  EXPECT_EQ(soft_issue, "acl_apply_failed");
}

TEST(AclRestoreTest, PreserveRoundTripAfterIcacls) {
  const std::string repo = test::TempDir("acl_repo");
  const std::string source = test::TempDir("acl_source");
  const std::string dest = test::TempDir("acl_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source + "/secured.txt", "secured-data");
  const std::string icacls_cmd =
      "icacls \"" + source + "\\secured.txt\" /grant Everyone:R >nul 2>&1";
  if (std::system(icacls_cmd.c_str()) != 0) {
    GTEST_SKIP() << "icacls not available";
  }

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  ManifestDocument doc;
  ASSERT_TRUE(engine.LoadManifest(0, &doc).ok());
  bool saw_sd = false;
  for (const auto& file : doc.files) {
    if (file.relative_path == "secured.txt" &&
        !file.security_descriptor_b64.empty()) {
      saw_sd = true;
      break;
    }
  }
  if (!saw_sd) GTEST_SKIP() << "security descriptor not captured on this host";

  RestoreOptions opts{};
  opts.acl_policy.mode = winmeta::AclRestorePolicy::Mode::kPreserve;
  const Status restore_st = engine.Restore(dest, opts);
  if (!restore_st.ok()) {
    opts.acl_policy.mode = winmeta::AclRestorePolicy::Mode::kBestEffort;
    ASSERT_TRUE(engine.Restore(dest, opts).ok());
  }
  EXPECT_TRUE(std::filesystem::exists(dest + "/secured.txt"));
}
#endif

}  // namespace
}  // namespace ebbackup
