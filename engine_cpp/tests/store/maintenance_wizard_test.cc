#include <gtest/gtest.h>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/store/maintenance_wizard.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(MaintenanceWizardTest, DryRunDoesNotChangePhysicalBytes) {
  const std::string repo = test::TempDir("wiz_repo");
  const std::string source = test::TempDir("wiz_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/data.txt", test::MakeSyntheticData(4096, 9));

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  MaintenanceWizardOptions opts{};
  opts.dry_run_only = true;
  MaintenanceWizardReport report{};
  ASSERT_TRUE(RunMaintenanceWizard(&engine, opts, &report).ok());
  EXPECT_EQ(report.stats_before.physical_bytes, report.stats_after.physical_bytes);
  EXPECT_GE(report.stats_before.physical_bytes, report.stats_before.live_bytes);
}

}  // namespace
}  // namespace ebbackup
