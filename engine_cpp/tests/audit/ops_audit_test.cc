#include <gtest/gtest.h>

#include "ebbackup/audit/rar_chain.h"
#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

namespace ebbackup {
namespace audit {
namespace {

TEST(OpsAuditTest, AppendAndListOpsEntry) {
  const std::string repo = test::TempDir("ops_audit_repo");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  const Status st = AppendOpsAuditEntry(
      repo, "gc_orphans", R"({"dry_run":false,"orphan_count":3})", DigestAlgo::kLegacy,
      "");
  ASSERT_TRUE(st.ok()) << st.message();

  std::vector<RarChainEntry> entries;
  ASSERT_TRUE(ListOpsAuditEntries(repo, &entries).ok());
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_NE(entries[0].body_json.find("\"kind\":\"ops\""), std::string::npos);
  EXPECT_NE(entries[0].body_json.find("\"op\":\"gc_orphans\""), std::string::npos);

  const std::string json = OpsAuditEntriesToJson(entries);
  EXPECT_NE(json.find("\"ok\":true"), std::string::npos);
}

TEST(OpsAuditTest, OpsEntryKeepsRarChainConsistent) {
  const std::string repo = test::TempDir("ops_audit_chain_repo");
  const std::string source = test::TempDir("ops_audit_chain_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/data.txt", "ops-audit-chain");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  const std::string append = engine.AppendOpsAuditJson(
      R"({"op":"compact","dry_run":false,"physical_before":100,"physical_after":80})");
  EXPECT_NE(append.find("\"ok\":true"), std::string::npos);

  const std::string chain_path = repo + "/audit/rar.chain";
  RarChainVerifyReport report{};
  ASSERT_TRUE(VerifyRarChain(chain_path, &report).ok());
  EXPECT_TRUE(report.consistent);
  EXPECT_GE(report.entry_count, 2u);
}

}  // namespace
}  // namespace audit
}  // namespace ebbackup
