#pragma once

#include <cstddef>
#include <cstdint>

namespace ebbackup {
namespace fastcdc_internal {

uint32_t BuildMask(uint32_t avg_size);
void InitGearTable(uint32_t gear[256]);
uint32_t InitWindowHash(const uint8_t* data, size_t end, uint32_t w,
                        const uint32_t gear[256]);

bool ScanGearCut(const uint8_t* data, size_t scan_start, size_t cut_limit,
                 uint32_t w, uint32_t mask, const uint32_t gear[256],
                 size_t* out_cut, bool* found);

}  // namespace fastcdc_internal
}  // namespace ebbackup
