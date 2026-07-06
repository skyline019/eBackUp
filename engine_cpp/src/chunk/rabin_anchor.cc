#include "ebbackup/chunk/rabin_anchor.h"

namespace ebbackup {

namespace {

constexpr uint64_t kRabinPoly = 0x3DA3358B4DC173ULL;

uint64_t RabinStep(uint64_t fp, uint8_t byte) {
  return ((fp << 8) | byte) ^ (kRabinPoly * ((fp >> 56) ^ byte));
}

}  // namespace

RabinAnchor::RabinAnchor(uint64_t mask) : mask_(mask) {}

void RabinAnchor::Reset() { fp_ = 0; }

void RabinAnchor::Feed(uint8_t byte) { fp_ = RabinStep(fp_, byte); }

bool RabinAnchor::IsAnchor() const { return (fp_ & mask_) == 0; }

uint64_t RabinAnchor::Table(uint8_t byte) {
  return RabinStep(0, byte);
}

bool RabinFindAnchor(const uint8_t* data, size_t len, size_t start, size_t end,
                     size_t window, uint64_t mask, size_t* cut_out) {
  if (!data || !cut_out || end <= start || window == 0 || start + window > len) {
    return false;
  }
  uint64_t fp = 0;
  for (size_t i = start; i < start + window; ++i) {
    fp = RabinStep(fp, data[i]);
  }
  for (size_t i = start + window; i <= end && i <= len; ++i) {
    if ((fp & mask) == 0) {
      *cut_out = i;
      return true;
    }
    if (i >= len) break;
    fp = RabinStep(fp, data[i]);
    fp ^= RabinStep(0, data[i - window]);
  }
  return false;
}

}  // namespace ebbackup
