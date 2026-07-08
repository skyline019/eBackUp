#include "ebsync/transport/transport.h"

#include <filesystem>
#include <fstream>

namespace ebsync {

namespace {

class LocalDirTransport : public IRemoteTransport {
 public:
  explicit LocalDirTransport(std::string root) : root_(std::move(root)) {
    std::error_code ec;
    std::filesystem::create_directories(root_, ec);
  }

  std::string FullKey(const std::string& rel) const override {
    return (std::filesystem::path(root_) / rel).generic_string();
  }

  TransportResult Head(const std::string& key, bool* exists) override {
    TransportResult r;
    if (!exists) {
      r.error = "exists is null";
      return r;
    }
    *exists = std::filesystem::exists(FullKey(key));
    r.ok = true;
    return r;
  }

  TransportResult Put(const std::string& key, const uint8_t* data, size_t len,
                    const PutOptions& opts) override {
    TransportResult r;
    const std::string path = FullKey(key);
    if (opts.skip_if_exists && std::filesystem::exists(path)) {
      r.ok = true;
      r.http_status = 200;
      return r;
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(),
                                        ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
      r.error = "write failed";
      return r;
    }
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(len));
    r.ok = static_cast<bool>(out);
    r.http_status = r.ok ? 200 : 0;
    if (!r.ok) r.error = "write failed";
    return r;
  }

  TransportResult Get(const std::string& key, std::vector<uint8_t>* out) override {
    TransportResult r;
    if (!out) {
      r.error = "out is null";
      return r;
    }
    std::ifstream in(FullKey(key), std::ios::binary);
    if (!in) {
      r.error = "not found";
      r.http_status = 404;
      return r;
    }
    in.seekg(0, std::ios::end);
    const auto len = in.tellg();
    if (len < 0) {
      r.error = "tell failed";
      return r;
    }
    in.seekg(0, std::ios::beg);
    out->resize(static_cast<size_t>(len));
    in.read(reinterpret_cast<char*>(out->data()), len);
    r.ok = static_cast<bool>(in);
    r.http_status = r.ok ? 200 : 0;
    return r;
  }

 private:
  std::string root_;
};

}  // namespace

std::unique_ptr<IRemoteTransport> CreateLocalDirTransport(const std::string& root) {
  return std::make_unique<LocalDirTransport>(root);
}

}  // namespace ebsync
