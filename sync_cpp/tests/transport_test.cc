#include <gtest/gtest.h>

#include <filesystem>

#include "ebsync/sync_agent.h"
#include "ebsync/transport/transport.h"

TEST(TransportTest, LocalDirPutHeadGet) {
  const std::string root = "transport_test_root";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  auto transport = ebsync::CreateLocalDirTransport(root);
  ASSERT_NE(transport, nullptr);
  const char payload[] = "chunk-bytes";
  auto put = transport->Put("chunks/abc", reinterpret_cast<const uint8_t*>(payload),
                            sizeof(payload) - 1, {});
  ASSERT_TRUE(put.ok);
  bool exists = false;
  auto head = transport->Head("chunks/abc", &exists);
  ASSERT_TRUE(head.ok);
  EXPECT_TRUE(exists);
  std::vector<uint8_t> got;
  auto get = transport->Get("chunks/abc", &got);
  ASSERT_TRUE(get.ok);
  EXPECT_EQ(std::string(got.begin(), got.end()), "chunk-bytes");

  std::filesystem::remove_all(root, ec);
}
