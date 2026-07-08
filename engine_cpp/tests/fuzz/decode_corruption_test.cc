#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "ebbackup/codec/codec_types.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/path_encoding.h"
#include "ebbackup/store/chunk_store.h"
#include "ebbackup/store/eb_pack.h"
#include "test_util.h"

namespace ebbackup {
namespace {

void XorMutateFileByte(const std::string& path, uint64_t offset, uint8_t mask) {
  std::fstream io(path, std::ios::binary | std::ios::in | std::ios::out);
  ASSERT_TRUE(static_cast<bool>(io)) << "cannot open " << path;
  io.seekg(static_cast<std::streamoff>(offset));
  char byte = 0;
  io.read(&byte, 1);
  ASSERT_TRUE(static_cast<bool>(io)) << "read failed at " << offset;
  byte = static_cast<char>(static_cast<uint8_t>(byte) ^ mask);
  io.seekp(static_cast<std::streamoff>(offset));
  io.write(&byte, 1);
  ASSERT_TRUE(static_cast<bool>(io)) << "write failed at " << offset;
}

std::string PacksDirForStore(const ChunkStore& store) {
  return (std::filesystem::path(store.path()).parent_path() / "packs").string();
}

std::string FindFirstEbPackInPacks(const std::string& packs_dir) {
  std::error_code ec;
  if (!std::filesystem::exists(PathFromUtf8(packs_dir), ec)) return {};
  for (const auto& ent :
       std::filesystem::directory_iterator(PathFromUtf8(packs_dir), ec)) {
    if (ent.is_regular_file() && ent.path().extension() == ".ebpack") {
      return ent.path().string();
    }
  }
  return {};
}

void FuzzDecodeGetNoCrash(ChunkStore* store, const uint8_t hash[32],
                          const std::string& file_path, uint64_t region_start,
                          uint64_t region_len,
                          const std::vector<uint8_t>& expected, int iterations,
                          uint32_t seed) {
  ASSERT_GT(region_len, 0u);
  std::mt19937 rng(seed);
  std::uniform_int_distribution<uint64_t> off_dist(0, region_len - 1);
  std::uniform_int_distribution<int> mask_dist(1, 255);

  for (int i = 0; i < iterations; ++i) {
    const uint64_t offset = region_start + off_dist(rng);
    const uint8_t mask = static_cast<uint8_t>(mask_dist(rng));
    XorMutateFileByte(file_path, offset, mask);

    std::vector<uint8_t> out;
    const Status st = store->Get(hash, &out);
    if (st.ok()) {
      EXPECT_EQ(out, expected) << "iteration=" << i << " offset=" << offset;
    }
  }
}

TEST(DecodeCorruptionTest, ChunkStoreMutatedPayloadNeverCrashes) {
  const std::string dir = test::TempDir("decode_fuzz_flat");
  ChunkStore store(dir + "/chunks");
  ChunkStorePutOptions opts{};
  opts.compress_mode = CompressMode::kZstd;
  opts.compress_tier = CompressTier::kBalanced;
  ASSERT_TRUE(store.Open().ok());

  const std::string payload = test::MakeSyntheticData(256 * 1024, 17);
  std::vector<uint8_t> expected(payload.begin(), payload.end());
  uint8_t hash[32]{};
  ASSERT_TRUE(store.Put(reinterpret_cast<const uint8_t*>(payload.data()),
                        payload.size(), hash, nullptr, &opts)
                  .ok());
  ASSERT_TRUE(store.Flush().ok());

  const std::string chunk_file = store.path();
  const uint64_t file_size = store.file_size();
  ASSERT_GT(file_size, 64u);

  FuzzDecodeGetNoCrash(&store, hash, chunk_file, 0, file_size, expected, 32,
                       0xA11CEu);
}

TEST(DecodeCorruptionTest, EbPackMutatedPayloadNeverCrashes) {
  const std::string repo = test::TempDir("decode_fuzz_ebpack");
  ASSERT_TRUE(test::InitV05Repo(repo).ok());
  ChunkStore store(repo + "/data/chunks");
  store.SetUseEbPack(true);
  store.SetTxnId(1);
  ChunkStorePutOptions opts{};
  opts.compress_mode = CompressMode::kZstd;
  opts.compress_tier = CompressTier::kMax;
  ASSERT_TRUE(store.Open().ok());
  ASSERT_TRUE(store.BeginAppendSession().ok());

  const std::string payload = test::MakeSyntheticData(512 * 1024, 23);
  std::vector<uint8_t> expected(payload.begin(), payload.end());
  uint8_t hash[32]{};
  ASSERT_TRUE(store.Put(reinterpret_cast<const uint8_t*>(payload.data()),
                        payload.size(), hash, nullptr, &opts)
                  .ok());
  ASSERT_TRUE(store.Flush().ok());
  ASSERT_TRUE(store.EndAppendSession().ok());

  const std::string pack_path = FindFirstEbPackInPacks(PacksDirForStore(store));
  ASSERT_FALSE(pack_path.empty()) << "expected ebpack file";

  uint64_t record_offset = 0;
  uint32_t stored_len = 0;
  ASSERT_TRUE(store
                  .ForEachRecord([&](const uint8_t* rec_hash, uint64_t offset,
                                     uint32_t len) {
                    if (std::memcmp(rec_hash, hash, 32) == 0) {
                      record_offset = offset;
                      stored_len = len;
                    }
                    return Status::Ok();
                  })
                  .ok());
  ASSERT_GT(stored_len, 0u);

  const uint64_t region_len =
      static_cast<uint64_t>(kChunkHeaderV2Size) + stored_len;
  const uint64_t file_size =
      std::filesystem::file_size(PathFromUtf8(pack_path));
  ASSERT_GE(file_size, record_offset + region_len);

  FuzzDecodeGetNoCrash(&store, hash, pack_path, record_offset, region_len,
                       expected, 32, 0xEB00u);
}

TEST(DecodeCorruptionTest, ReadEbPackRecordAtRejectsRecordRegionCorruption) {
  const std::string dir = test::TempDir("decode_fuzz_ebpack_hdr");
  EbPackWriter writer(dir, 99);
  const std::string payload = test::MakeSyntheticData(64 * 1024, 31);
  uint8_t hash[32]{};
  ContentHash(DigestAlgo::kStandard,
              reinterpret_cast<const uint8_t*>(payload.data()), payload.size(),
              hash);

  EbPackRecordRef ref{};
  ASSERT_TRUE(writer
                  .AppendRecord(hash, reinterpret_cast<const uint8_t*>(payload.data()),
                                payload.size(), static_cast<uint32_t>(payload.size()),
                                ChunkCodec::kRaw, &ref)
                  .ok());
  ASSERT_TRUE(writer.FlushOpenPack(true).ok());

  const uint64_t region_len =
      static_cast<uint64_t>(kChunkHeaderV2Size) + ref.stored_len;
  std::mt19937 rng(0xDEC0DEu);
  std::uniform_int_distribution<uint64_t> off_dist(0, region_len > 0 ? region_len - 1 : 0);
  std::uniform_int_distribution<int> mask_dist(1, 255);

  int rejected = 0;
  for (int i = 0; i < 24; ++i) {
    const uint64_t rel = off_dist(rng);
    XorMutateFileByte(ref.pack_path, ref.offset + rel,
                      static_cast<uint8_t>(mask_dist(rng)));

    ChunkStore::ParsedHeader parsed{};
    std::vector<uint8_t> out;
    const Status st =
        ChunkStore::ReadEbPackRecordAt(ref.pack_path, ref.offset, &parsed, &out);
    if (!st.ok()) {
      ++rejected;
      continue;
    }
    if (out.size() != payload.size() ||
        std::memcmp(out.data(), payload.data(), payload.size()) != 0) {
      ++rejected;
    }
  }
  EXPECT_GE(rejected, 12) << "expected most record-region mutations to fail or alter payload";
}

}  // namespace
}  // namespace ebbackup
