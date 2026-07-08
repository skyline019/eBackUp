#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ebsync/sync_state.h"

namespace ebsync {

struct TransportResult {
  bool ok{false};
  int http_status{0};
  std::string error;
  bool retryable{false};
};

struct PutOptions {
  bool skip_if_exists{true};
};

class IRemoteTransport {
 public:
  virtual ~IRemoteTransport() = default;
  virtual TransportResult Head(const std::string& key, bool* exists) = 0;
  virtual TransportResult Put(const std::string& key, const uint8_t* data,
                              size_t len, const PutOptions& opts) = 0;
  virtual TransportResult Get(const std::string& key,
                              std::vector<uint8_t>* out) = 0;
  virtual std::string FullKey(const std::string& rel) const = 0;
};

std::unique_ptr<IRemoteTransport> CreateLocalDirTransport(const std::string& root);
std::unique_ptr<IRemoteTransport> CreateS3Transport(const S3RemoteConfig& cfg);
std::unique_ptr<IRemoteTransport> CreatePdsTransport(const std::string& repo_path,
                                                       PdsRemoteConfig* cfg);

}  // namespace ebsync
