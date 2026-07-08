#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ebsync {
namespace s3internal {

std::string Sha256Hex(const uint8_t* data, size_t len);
std::string Sha256Hex(const std::string& data);
std::string HmacSha256Hex(const std::string& key, const std::string& data);
std::string HmacSha256Raw(const std::string& key, const std::string& data);

std::string UrlEncode(const std::string& in, bool encode_slash = false);
std::string AmzDate(int64_t unix_time);
std::string DateStamp(int64_t unix_time);

}  // namespace s3internal
}  // namespace ebsync
