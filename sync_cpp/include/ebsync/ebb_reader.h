#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ebsync {

struct EbbTocEntry {
  std::string relative_path;
  uint64_t offset{0};
  uint64_t size{0};
  uint32_t crc32{0};
};

struct EbbHeader {
  char magic[4]{};
  uint32_t version{0};
  uint64_t base_txn_id{0};
  uint64_t target_txn_id{0};
  uint32_t count{0};
};

struct EbbBundle {
  EbbHeader header{};
  std::vector<EbbTocEntry> toc;
  std::vector<std::vector<uint8_t>> payloads;
};

bool ReadEbbBundle(const std::string& path, EbbBundle* out, std::string* error);

bool WriteEbbBundle(const std::string& path, const EbbBundle& bundle, std::string* error);

bool IsChunkTocPath(const std::string& rel, std::string* hex_out);

}  // namespace ebsync
