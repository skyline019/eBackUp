#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ebbackup/chunk/cfi_index.h"

namespace ebbackup {

class CfiBloomFilter {
 public:
  explicit CfiBloomFilter(size_t expected_entries = 0);

  void Insert(const uint8_t hash[32]);
  bool MightContain(const uint8_t hash[32]) const;

  static CfiBloomFilter BuildFromCfi(const CfiIndex& history);

 private:
  void InsertSlice(const uint8_t slice[32]);

  std::vector<uint64_t> bits_;
  size_t bit_count_{0};
};

}  // namespace ebbackup
