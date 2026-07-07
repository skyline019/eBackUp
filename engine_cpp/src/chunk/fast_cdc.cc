#include "ebbackup/chunk/fast_cdc.h"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <vector>

#include "ebbackup/chunk/fast_cdc_internal.h"
#include "ebbackup/common/digest_pool.h"

namespace ebbackup {

namespace {

constexpr size_t kParallelHashMinBytes = 1024 * 1024;
constexpr size_t kParallelHashMinChunks = 2;

void HashChunkRegions(DigestAlgo algo, const uint8_t* data,
                      const std::vector<size_t>& offsets,
                      const std::vector<uint32_t>& lengths,
                      std::vector<ChunkDescriptor>* out) {
  out->clear();
  out->resize(offsets.size());
  const size_t count = offsets.size();
  if (count == 0) return;

  const bool use_pool =
      data && count >= kParallelHashMinChunks &&
      std::accumulate(lengths.begin(), lengths.end(), size_t{0}) >=
          kParallelHashMinBytes;

  if (use_pool) {
    std::vector<DigestSpan> spans(count);
    for (size_t i = 0; i < count; ++i) {
      spans[i].offset = offsets[i];
      spans[i].length = lengths[i];
    }
    std::vector<uint8_t> hashes(count * 32);
    DigestPool::Shared().HashRegions(algo, data, spans.data(), count,
                                     hashes.data());
    for (size_t i = 0; i < count; ++i) {
      (*out)[i].offset = offsets[i];
      (*out)[i].length = lengths[i];
      std::memcpy((*out)[i].hash, hashes.data() + i * 32, 32);
    }
    return;
  }

  for (size_t i = 0; i < count; ++i) {
    (*out)[i].offset = offsets[i];
    (*out)[i].length = lengths[i];
    ContentHash(algo, data + offsets[i], lengths[i], (*out)[i].hash);
  }
}

}  // namespace

FastCdcSlice::FastCdcSlice(FastCdcConfig config) : config_(config) {
  fastcdc_internal::InitGearTable(gear_);
}

uint32_t FastCdcSlice::Mask() const {
  return fastcdc_internal::BuildMask(config_.avg_size);
}

Status FastCdcSlice::Chunk(const uint8_t* data, size_t len,
                           std::vector<ChunkDescriptor>* out) const {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  if (len == 0) return Status::Ok();

  std::vector<size_t> offsets;
  std::vector<uint32_t> lengths;
  const Status cuts_st = ChunkCuts(data, len, &offsets, &lengths);
  if (!cuts_st.ok()) return cuts_st;

  if (len <= config_.min_size) {
    ChunkDescriptor desc{};
    desc.offset = 0;
    desc.length = static_cast<uint32_t>(len);
    ContentHash(config_.digest_algo, data, len, desc.hash);
    out->push_back(desc);
    return Status::Ok();
  }

  HashChunkRegions(config_.digest_algo, data, offsets, lengths, out);
  return Status::Ok();
}

Status FastCdcSlice::ChunkCuts(const uint8_t* data, size_t len,
                               std::vector<size_t>* offsets,
                               std::vector<uint32_t>* lengths) const {
  if (!offsets || !lengths) return Status::InvalidArgument("out is null");
  offsets->clear();
  lengths->clear();
  if (len == 0) return Status::Ok();

  if (len <= config_.min_size) {
    offsets->push_back(0);
    lengths->push_back(static_cast<uint32_t>(len));
    return Status::Ok();
  }

  const uint32_t mask = Mask();
  const uint32_t w = config_.window_size;
  offsets->reserve(len / config_.min_size + 1);
  lengths->reserve(offsets->capacity());

  size_t pos = 0;
  while (pos < len) {
    const size_t remaining = len - pos;
    if (remaining <= config_.min_size) {
      offsets->push_back(pos);
      lengths->push_back(static_cast<uint32_t>(remaining));
      break;
    }

    const size_t scan_start = pos + config_.min_size;
    size_t cut = std::min(pos + config_.max_size, len);
    bool found = false;

    if (scan_start >= w && scan_start < cut) {
      fastcdc_internal::ScanGearCut(data, scan_start, cut, w, mask, gear_,
                                    &cut, &found);
    }

    if (!found && remaining > config_.max_size) {
      cut = pos + config_.max_size;
    } else if (!found) {
      cut = len;
    }

    offsets->push_back(pos);
    lengths->push_back(static_cast<uint32_t>(cut - pos));
    pos = cut;
  }
  return Status::Ok();
}

}  // namespace ebbackup
