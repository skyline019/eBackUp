#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "test_util.h"

namespace ebbackup {
namespace {

#if defined(_WIN32)
void SetEnvVar(const char* key, const char* value) {
  _putenv_s(key, value);
}
void ClearEnvVar(const char* key) {
  _putenv_s(key, "");
}
#else
void SetEnvVar(const char* key, const char* value) {
  setenv(key, value, 1);
}
void ClearEnvVar(const char* key) {
  unsetenv(key);
}
#endif

class GtCdcEnvGuard {
 public:
  explicit GtCdcEnvGuard(bool enable) : enabled_(enable) {
    if (enabled_) SetEnvVar("EBBACKUP_CDC_ALGO", "gtcdc");
  }
  ~GtCdcEnvGuard() {
    if (enabled_) ClearEnvVar("EBBACKUP_CDC_ALGO");
  }

 private:
  bool enabled_;
};

TEST(GtCdcPipelineTest, RouterEnabledFlag) {
  GtCdcEnvGuard guard(true);
  EXPECT_TRUE(CdcGtCdcEnabled());
}

TEST(GtCdcPipelineTest, IncrementalChain) {
  GtCdcEnvGuard guard(true);
  const auto base = test::TestOutputRoot() / "gtcdc_incr_chain";
  const std::string source = (base / "source").string();
  const std::string repo = (base / "repo").string();
  std::error_code ec;
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(source);
  std::string payload = test::MakeSyntheticData(4 * 1024 * 1024, 1);
  test::WriteFile(source + "/a.bin", payload);

  ASSERT_TRUE(test::InitGtCdcV4Repo(repo).ok());
  EXPECT_TRUE(test::RepoHasGtCdcNative(repo));
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  BackupOptions opts{};
  opts.use_pipeline = true;
  const Status full_st = engine.RunBackup(source, BackupMode::kFull, opts);
  ASSERT_TRUE(full_st.ok()) << full_st.message();
  ASSERT_TRUE(engine.Verify().ok());

  payload[512 * 1024] ^= 0x42;
  test::WriteFile(source + "/a.bin", payload);
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental, opts).ok());
  ASSERT_TRUE(engine.Verify().ok());
}

TEST(GtCdcPipelineTest, V5IncrementalChain) {
  GtCdcEnvGuard guard(true);
  const auto base = test::TestOutputRoot() / "gtcdc_v5_incr_chain";
  const std::string source = (base / "source").string();
  const std::string repo = (base / "repo").string();
  std::error_code ec;
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(source);
  std::string payload = test::MakeRandomData(4 * 1024 * 1024, 17);
  test::WriteFile(source + "/a.bin", payload);

  ASSERT_TRUE(test::InitGtCdcV5Repo(repo).ok());
  EXPECT_TRUE(test::RepoHasGtCdcAnGear(repo));
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  BackupOptions opts{};
  opts.use_pipeline = true;
  const Status full_st = engine.RunBackup(source, BackupMode::kFull, opts);
  ASSERT_TRUE(full_st.ok()) << full_st.message();
  ASSERT_TRUE(engine.Verify().ok());

  payload[512 * 1024] ^= 0x42;
  test::WriteFile(source + "/a.bin", payload);
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental, opts).ok());
  ASSERT_TRUE(engine.Verify().ok());
}

TEST(GtCdcPipelineTest, V6IncrementalChain) {
  GtCdcEnvGuard guard(true);
  const auto base = test::TestOutputRoot() / "gtcdc_v6_incr_chain";
  const std::string source = (base / "source").string();
  const std::string repo = (base / "repo").string();
  std::error_code ec;
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(source);
  std::string payload = test::MakeRandomData(4 * 1024 * 1024, 23);
  test::WriteFile(source + "/a.bin", payload);

  ASSERT_TRUE(test::InitGtCdcV6Repo(repo).ok());
  EXPECT_TRUE(test::RepoHasGtCdcTwoFGear(repo));
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  BackupOptions opts{};
  opts.use_pipeline = true;
  const Status full_st = engine.RunBackup(source, BackupMode::kFull, opts);
  ASSERT_TRUE(full_st.ok()) << full_st.message();
  ASSERT_TRUE(engine.Verify().ok());

  payload[512 * 1024] ^= 0x42;
  test::WriteFile(source + "/a.bin", payload);
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental, opts).ok());
  ASSERT_TRUE(engine.Verify().ok());
}

}  // namespace
}  // namespace ebbackup
