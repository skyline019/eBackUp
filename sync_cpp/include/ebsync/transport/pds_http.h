#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ebsync {
namespace pdshttp {

struct HttpResponse {
  int status{0};
  std::vector<uint8_t> body;
  std::string error;
  bool ok{false};
  bool retryable{false};
};

HttpResponse Request(const std::string& method, const std::string& url,
                     const std::map<std::string, std::string>& headers,
                     const uint8_t* body, size_t body_len);

std::string UrlEncode(const std::string& s);

}  // namespace pdshttp
}  // namespace ebsync
