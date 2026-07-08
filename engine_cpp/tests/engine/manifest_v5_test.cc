#include <gtest/gtest.h>

#include "ebbackup/engine/manifest.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(ManifestV5Test, RoundTripWinMetaFields) {
  ManifestDocument doc;
  doc.txn_id = 42;
  ManifestFileEntry file;
  file.relative_path = "docs/readme.txt";
  file.size = 128;
  file.chunk_hashes_hex.push_back(std::string(64, 'a'));
  file.security_descriptor_b64 = "U0Q=";
  file.inode_id = 0x1234;
  file.reparse_tag = 0xA0000003u;
  file.reparse_target = "C:\\target\\real_dir";
  file.stream_name = "Zone.Identifier";
  doc.files.push_back(file);

  const std::string path = test::TempDir("manifest_v5") + "/manifest";
  ASSERT_TRUE(WriteManifestV5(path, doc).ok());

  ManifestDocument loaded;
  ASSERT_TRUE(ReadManifestAuto(path, &loaded).ok());
  ASSERT_EQ(loaded.txn_id, 42u);
  ASSERT_EQ(loaded.files.size(), 1u);
  EXPECT_EQ(loaded.files[0].security_descriptor_b64, "U0Q=");
  EXPECT_EQ(loaded.files[0].inode_id, 0x1234u);
  EXPECT_EQ(loaded.files[0].reparse_tag, 0xA0000003u);
  EXPECT_EQ(loaded.files[0].reparse_target, "C:\\target\\real_dir");
  EXPECT_EQ(loaded.files[0].stream_name, "Zone.Identifier");
}

}  // namespace
}  // namespace ebbackup
