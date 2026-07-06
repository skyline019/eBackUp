#include <gtest/gtest.h>

#include <cstring>
#include <fstream>

#include "ebbackup/engine/manifest.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(ManifestTest, V2RoundtripWithSpacePath) {
  const std::string path = test::TempDir("manifest_v2") + "/manifest";
  ManifestDocument doc;
  doc.txn_id = 99;
  ManifestFileEntry file;
  file.relative_path = "folder/my file.txt";
  file.size = 123;
  file.chunk_hashes_hex = {"aa", "bb"};
  ChunkAnchor anchor{};
  anchor.offset = 0;
  anchor.length = 64;
  anchor.strength = AnchorStrength::kWeak;
  std::memset(anchor.hash, 7, 32);
  file.cfi.anchors.push_back(anchor);
  doc.files.push_back(file);

  ASSERT_TRUE(WriteManifestV2(path, doc).ok());
  ManifestDocument loaded;
  ASSERT_TRUE(ReadManifestAuto(path, &loaded).ok());
  ASSERT_EQ(loaded.txn_id, 99u);
  ASSERT_EQ(loaded.files.size(), 1u);
  EXPECT_EQ(loaded.files[0].relative_path, "folder/my file.txt");
  EXPECT_EQ(loaded.files[0].chunk_hashes_hex.size(), 2u);
  ASSERT_EQ(loaded.files[0].cfi.anchors.size(), 1u);
}

TEST(ManifestTest, V1Roundtrip) {
  const std::string path = test::TempDir("manifest_v1") + "/manifest";
  std::ofstream out(path, std::ios::binary);
  out << "EBMANIFEST1\n";
  out << "F data.bin 100 2\n";
  out << "C aa\n";
  out << "C bb\n";
  out.close();

  ManifestDocument loaded;
  ASSERT_TRUE(ReadManifestAuto(path, &loaded).ok());
  ASSERT_EQ(loaded.files.size(), 1u);
  EXPECT_EQ(loaded.files[0].relative_path, "data.bin");
  EXPECT_EQ(loaded.files[0].size, 100u);
  EXPECT_EQ(loaded.files[0].chunk_hashes_hex.size(), 2u);
}

TEST(ManifestTest, V3MetaRoundtrip) {
  const std::string path = test::TempDir("manifest_v3") + "/manifest";
  ManifestDocument doc;
  doc.txn_id = 42;
  ManifestFileEntry dir;
  dir.relative_path = "empty_dir";
  dir.file_type = FileType::kDirectory;
  dir.mode = 0755;
  dir.mtime_unix = 1700000000;
  doc.files.push_back(dir);

  ManifestFileEntry file;
  file.relative_path = "notes.txt";
  file.size = 4;
  file.file_type = FileType::kRegular;
  file.mode = 0644;
  file.chunk_hashes_hex = {"aa"};
  doc.files.push_back(file);

  ASSERT_TRUE(WriteManifestV3(path, doc).ok());
  ManifestDocument loaded;
  ASSERT_TRUE(ReadManifestAuto(path, &loaded).ok());
  ASSERT_EQ(loaded.txn_id, 42u);
  ASSERT_EQ(loaded.files.size(), 2u);
  EXPECT_EQ(loaded.files[0].file_type, FileType::kDirectory);
  EXPECT_EQ(loaded.files[0].mode, 0755u);
  EXPECT_EQ(loaded.files[1].relative_path, "notes.txt");
}

}  // namespace
}  // namespace ebbackup
