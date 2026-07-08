#include "ebsync/transport/transport.h"

#include "ebsync/pds/pds_client.h"

namespace ebsync {
namespace {

class PdsTransport : public IRemoteTransport {
 public:
  PdsTransport(std::string repo_path, PdsRemoteConfig* cfg)
      : cfg_(cfg), client_(std::move(repo_path), cfg) {}

  std::string FullKey(const std::string& rel) const override {
    if (!cfg_ || cfg_->root_prefix.empty()) return rel;
    std::string p = cfg_->root_prefix;
    while (!p.empty() && p.back() == '/') p.pop_back();
    if (rel.empty()) return p;
    if (rel.front() == '/') return p + rel;
    return p + "/" + rel;
  }

  TransportResult Head(const std::string& key, bool* exists) override {
    TransportResult r;
    if (!exists) {
      r.error = "exists null";
      return r;
    }
    std::string err;
    if (!client_.HeadObject(key, exists, &err)) {
      r.error = err;
      r.retryable = err.find("HTTP 5") != std::string::npos;
      return r;
    }
    r.ok = true;
    r.http_status = 200;
    return r;
  }

  TransportResult Put(const std::string& key, const uint8_t* data, size_t len,
                      const PutOptions& opts) override {
    TransportResult r;
    if (opts.skip_if_exists) {
      bool exists = false;
      const TransportResult hr = Head(key, &exists);
      if (hr.ok && exists) {
        r.ok = true;
        r.http_status = 200;
        return r;
      }
    }
    std::string err;
    if (!client_.PutObject(key, data, len, &err)) {
      r.error = err;
      r.retryable = err.find("HTTP 5") != std::string::npos;
      return r;
    }
    r.ok = true;
    r.http_status = 200;
    return r;
  }

  TransportResult Get(const std::string& key, std::vector<uint8_t>* out) override {
    TransportResult r;
    if (!out) {
      r.error = "out null";
      return r;
    }
    std::string err;
    if (!client_.GetObject(key, out, &err)) {
      r.error = err;
      r.http_status = 404;
      return r;
    }
    r.ok = true;
    r.http_status = 200;
    return r;
  }

 private:
  PdsRemoteConfig* cfg_;
  PdsClient client_;
};

}  // namespace

std::unique_ptr<IRemoteTransport> CreatePdsTransport(const std::string& repo_path,
                                                       PdsRemoteConfig* cfg) {
  if (!cfg || cfg->domain_id.empty() || cfg->client_id.empty() ||
      cfg->client_secret.empty()) {
    return nullptr;
  }
  return std::make_unique<PdsTransport>(repo_path, cfg);
}

}  // namespace ebsync
