#pragma once

#include <cstddef>
#include <cstdint>

#include "ebbackup/common/digest.h"

namespace ebbackup {

struct DigestSpan {
  size_t offset{0};
  size_t length{0};
};

class DigestPoolImpl;

class DigestPool {
 public:
  explicit DigestPool(unsigned threads = 0);
  ~DigestPool();

  DigestPool(const DigestPool&) = delete;
  DigestPool& operator=(const DigestPool&) = delete;

  void SetThreads(unsigned threads);
  unsigned threads() const;

  void HashRegions(DigestAlgo algo, const uint8_t* base,
                   const DigestSpan* spans, size_t span_count,
                   uint8_t* hashes_out);

  static DigestPool& Shared();

 private:
  DigestPoolImpl* impl_;
};

}  // namespace ebbackup
