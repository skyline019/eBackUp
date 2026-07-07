#include "ebbackup/chunk/cfi_bloom.h"

#include <algorithm>
#include <cstring>

namespace ebbackup {

namespace {

uint64_t Mix64(uint64_t x) {
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33;
  return x;
}

uint64_t HashSlice(const uint8_t slice[32], uint32_t seed) {
  uint64_t h = seed;
  for (size_t i = 0; i < 32; i += 8) {
    uint64_t chunk = 0;
    std::memcpy(&chunk, slice + i, sizeof(chunk));
    h ^= Mix64(chunk + seed + i);
    h = Mix64(h);
  }
  return h;
}

}  // namespace

CfiBloomFilter::CfiBloomFilter(size_t expected_entries) {
  size_t bits = expected_entries > 0 ? expected_entries * 10 : 4096;
  bits = std::max(bits, static_cast<size_t>(4096));
  bit_count_ = 1;
  while (bit_count_ < bits) bit_count_ <<= 1;
  bits_.assign(bit_count_ / 64, 0);
}

void CfiBloomFilter::InsertSlice(const uint8_t slice[32]) {
  if (bit_count_ == 0) return;
  const uint64_t mask = bit_count_ - 1;
  constexpr uint32_t kProbes = 4;
  for (uint32_t p = 0; p < kProbes; ++p) {
    const uint64_t h = HashSlice(slice, p * 0x9E3779B9u + 1u);
    const size_t idx = static_cast<size_t>(h & mask);
    bits_[idx / 64] |= (1ULL << (idx % 64));
  }
}

void CfiBloomFilter::Insert(const uint8_t hash[32]) {
  if (!hash) return;
  InsertSlice(hash);
}

bool CfiBloomFilter::MightContain(const uint8_t hash[32]) const {
  if (!hash || bit_count_ == 0) return false;
  const uint64_t mask = bit_count_ - 1;
  constexpr uint32_t kProbes = 4;
  for (uint32_t p = 0; p < kProbes; ++p) {
    const uint64_t h = HashSlice(hash, p * 0x9E3779B9u + 1u);
    const size_t idx = static_cast<size_t>(h & mask);
    if ((bits_[idx / 64] & (1ULL << (idx % 64))) == 0) return false;
  }
  return true;
}

CfiBloomFilter CfiBloomFilter::BuildFromCfi(const CfiIndex& history) {
  CfiBloomFilter bloom(history.anchors.size());
  for (const auto& anchor : history.anchors) {
    bloom.Insert(anchor.hash);
  }
  return bloom;
}

}  // namespace ebbackup
