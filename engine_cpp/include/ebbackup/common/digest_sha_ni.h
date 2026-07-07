#pragma once

#include <cstddef>
#include <cstdint>

namespace ebbackup {

bool DigestShaNiAvailable();
void Sha256ShaNi(const uint8_t* data, size_t len, uint8_t hash_out[32]);

}  // namespace ebbackup
