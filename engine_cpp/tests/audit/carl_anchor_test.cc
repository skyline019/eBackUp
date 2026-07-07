#include <gtest/gtest.h>

#include <cstdlib>

#include "ebbackup/audit/carl_anchor.h"
#include "ebbackup/audit/rar_chain.h"
#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(CarlAnchorTest, PublishLoadVerifyRoundtrip) {
  const std::string repo = test::TempDir("carl_anchor_repo");
  const std::string source = test::TempDir("carl_anchor_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/file.txt", test::MakeSyntheticData(4096, 1));

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  const std::string chain_path = repo + "/audit/rar.chain";
  const std::string anchor_dir = repo + "/audit/anchors";
  audit::CarlSignedTreeHead published{};
  ASSERT_TRUE(audit::PublishCarlAnchor(chain_path, anchor_dir, &published).ok());
  EXPECT_FALSE(published.root_hash.empty());

  audit::CarlSignedTreeHead loaded{};
  bool found = false;
  ASSERT_TRUE(
      audit::LoadLatestCarlAnchor(anchor_dir, "rar.chain", &loaded, &found).ok());
  ASSERT_TRUE(found);
  EXPECT_EQ(loaded.root_hash, published.root_hash);
  ASSERT_TRUE(audit::VerifyCarlAnchorAgainstChain(chain_path, loaded, nullptr).ok());
}

TEST(CarlAnchorTest, SignedAnchorWithKey) {
  const std::string repo = test::TempDir("carl_anchor_sign_repo");
  const std::string source = test::TempDir("carl_anchor_sign_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/file.txt", "signed-anchor");

#ifdef _WIN32
  _putenv("EBBACKUP_AUDIT_KEY=test-secret-key");
#else
  setenv("EBBACKUP_AUDIT_KEY", "test-secret-key", 1);
#endif

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  BackupOptions verify_opts{};
  verify_opts.require_anchor = true;
  ASSERT_TRUE(engine.Verify(verify_opts).ok());

#ifdef _WIN32
  _putenv("EBBACKUP_AUDIT_KEY=");
#else
  unsetenv("EBBACKUP_AUDIT_KEY");
#endif
}

}  // namespace
}  // namespace ebbackup
