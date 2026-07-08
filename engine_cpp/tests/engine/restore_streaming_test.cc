#include <gtest/gtest.h>

#include <fstream>

#include "ebbackup/audit/merkle.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "test_util.h"

namespace ebbackup {
namespace {

void RoundTripLargeFile(size_t file_size, uint8_t seed, const char* tag) {
  const std::string repo = test::TempDir(std::string("restore_stream_") + tag + "_repo");
  const std::string source = test::TempDir(std::string("restore_stream_") + tag + "_source");
  const std::string dest = test::TempDir(std::string("restore_stream_") + tag + "_dest");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  const std::string payload = test::MakeSyntheticData(file_size, seed);
  test::WriteFile(source + "/large.bin", payload);

  BackupOptions opts{};
  opts.use_pipeline = true;
  opts.use_lz4 = true;

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());

  BackupOptions verify_opts{};
  verify_opts.verify_deep_content = true;
  ASSERT_TRUE(engine.Verify(verify_opts).ok());
  ASSERT_TRUE(engine.Restore(dest).ok());

  ManifestDocument doc;
  ASSERT_TRUE(ReadManifestAuto(repo + "/manifest", &doc).ok());
  const ManifestFileEntry* file = test::FindManifestFile(doc, "large.bin");
  ASSERT_NE(file, nullptr);

  const std::string restored_path = dest + "/large.bin";
  ASSERT_TRUE(audit::VerifyRestoredFileChunks(restored_path, *file,
                                              engine.chunk_store()).ok());

  std::ifstream restored(restored_path, std::ios::binary);
  ASSERT_TRUE(restored.good());
  const std::string got((std::istreambuf_iterator<char>(restored)),
                        std::istreambuf_iterator<char>());
  EXPECT_EQ(file->size, got.size());
  EXPECT_EQ(Sha256Hex(reinterpret_cast<const uint8_t*>(got.data()), got.size()),
            Sha256Hex(reinterpret_cast<const uint8_t*>(payload.data()),
                      payload.size()));
}

TEST(RestoreStreamingTest, Pipeline256MBRoundTrip) {
  RoundTripLargeFile(256u * 1024u * 1024u, 71, "256m");
}

TEST(RestoreStreamingTest, Pipeline512MBRoundTrip) {
  RoundTripLargeFile(512u * 1024u * 1024u, 72, "512m");
}

}  // namespace
}  // namespace ebbackup
