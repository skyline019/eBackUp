#include <gtest/gtest.h>

#include <fstream>

#include "ebbackup/common/digest.h"
#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(RestoreTest, RoundTripHashMatch) {
  const std::string repo = test::TempDir("restore_repo");
  const std::string source = test::TempDir("restore_source");
  const std::string dest = test::TempDir("restore_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  const std::string payload = test::MakeSyntheticData(10 * 1024 * 1024, 4);
  test::WriteFile(source + "/nested/data.bin", payload);

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());
  ASSERT_TRUE(engine.Verify().ok());
  ASSERT_TRUE(engine.Restore(dest).ok());

  std::ifstream restored(dest + "/nested/data.bin", std::ios::binary);
  const std::string got((std::istreambuf_iterator<char>(restored)),
                        std::istreambuf_iterator<char>());
  EXPECT_EQ(Sha256Hex(reinterpret_cast<const uint8_t*>(got.data()), got.size()),
            Sha256Hex(reinterpret_cast<const uint8_t*>(payload.data()),
                      payload.size()));
}

TEST(RestoreTest, Lz4BackupRoundTrip) {
  const std::string repo = test::TempDir("restore_lz4_repo");
  const std::string source = test::TempDir("restore_lz4_source");
  const std::string dest = test::TempDir("restore_lz4_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  const std::string payload = test::MakeSyntheticData(8 * 1024 * 1024, 6);
  test::WriteFile(source + "/compressed.bin", payload);

  BackupOptions opts{};
  opts.use_lz4 = true;
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());
  ASSERT_TRUE(engine.Verify().ok());
  ASSERT_TRUE(engine.Restore(dest).ok());

  std::ifstream restored(dest + "/compressed.bin", std::ios::binary);
  const std::string got((std::istreambuf_iterator<char>(restored)),
                        std::istreambuf_iterator<char>());
  EXPECT_EQ(got, payload);
}

}  // namespace
}  // namespace ebbackup
