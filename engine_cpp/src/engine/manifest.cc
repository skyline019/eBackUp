#include "ebbackup/engine/manifest.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#include "ebbackup/common/crc32.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/fsync.h"
#include "ebbackup/common/path_encoding.h"

namespace ebbackup {

namespace {

constexpr uint32_t kManifestV4InnerMagic = 0xEB4F0001u;

enum ManifestV4MetaFlags : uint32_t {
  kMetaMode = 0x01u,
  kMetaUidGid = 0x02u,
  kMetaTimes = 0x04u,
  kMetaSymlink = 0x08u,
  kMetaDevice = 0x10u,
};

void WriteU32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v));
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v >> 16));
  out.push_back(static_cast<uint8_t>(v >> 24));
}

void WriteU64(std::vector<uint8_t>& out, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<uint8_t>(v >> (8 * i)));
  }
}

bool ReadU32(const uint8_t*& p, const uint8_t* end, uint32_t* out) {
  if (p + 4 > end) return false;
  *out = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
  p += 4;
  return true;
}

bool ReadU64(const uint8_t*& p, const uint8_t* end, uint64_t* out) {
  if (p + 8 > end) return false;
  *out = 0;
  for (int i = 0; i < 8; ++i) {
    *out |= static_cast<uint64_t>(p[i]) << (8 * i);
  }
  p += 8;
  return true;
}

uint32_t BuildV4MetaFlags(const ManifestFileEntry& f) {
  uint32_t flags = 0;
  if (f.mode != 0) flags |= kMetaMode;
  if (f.uid != 0xFFFFFFFFu || f.gid != 0xFFFFFFFFu) flags |= kMetaUidGid;
  if (f.mtime_unix != 0 || f.atime_unix != 0) flags |= kMetaTimes;
  if (!f.symlink_target.empty()) flags |= kMetaSymlink;
  if (f.device_major != 0 || f.device_minor != 0) flags |= kMetaDevice;
  return flags;
}

void AppendMetaLines(std::ostringstream& body, const ManifestFileEntry& f) {
  if (f.file_type != FileType::kRegular || f.has_extended_meta() ||
      !f.symlink_target.empty() || f.device_major != 0 || f.device_minor != 0) {
    body << "meta\tfile_type=" << FileTypeToString(f.file_type) << "\tmode="
         << f.mode << "\tuid=" << f.uid << "\tgid=" << f.gid
         << "\tmtime=" << f.mtime_unix << "\tatime=" << f.atime_unix << "\n";
  }
  if (!f.symlink_target.empty()) {
    body << "symlink_target\t" << f.symlink_target.size() << "\n"
         << f.symlink_target << "\n";
  }
  if (f.device_major != 0 || f.device_minor != 0) {
    body << "device\t" << f.device_major << "\t" << f.device_minor << "\n";
  }
}

std::string BuildBodyV2(const ManifestDocument& doc) {
  std::ostringstream body;
  body << "txn_id=" << doc.txn_id << "\n";
  for (const auto& f : doc.files) {
    body << "file\t" << f.relative_path.size() << "\t" << f.size << "\t"
         << f.chunk_hashes_hex.size() << "\n";
    body << f.relative_path << "\n";
    AppendMetaLines(body, f);
    for (const auto& h : f.chunk_hashes_hex) {
      body << "chunk\t" << h << "\n";
    }
    for (const auto& a : f.cfi.anchors) {
      body << "anchor\t" << a.offset << "\t" << a.length << "\t"
           << static_cast<int>(a.strength) << "\t" << BytesToHex(a.hash, 32)
           << "\n";
    }
  }
  return body.str();
}

bool DocumentUsesV3(const ManifestDocument& doc) {
  for (const auto& f : doc.files) {
    if (f.file_type != FileType::kRegular) return true;
    if (f.has_extended_meta()) return true;
    if (!f.symlink_target.empty()) return true;
    if (f.device_major != 0 || f.device_minor != 0) return true;
  }
  return false;
}

Status WriteManifestWithHeader(const std::string& path, const std::string& header,
                               const std::string& body) {
  uint32_t crc = 0;
  const Status crc_st = ComputeManifestBodyCrc32(body, &crc);
  if (!crc_st.ok()) return crc_st;
  std::ofstream out(PathFromUtf8(path), std::ios::binary);
  if (!out) return Status::IoError("cannot write manifest: " + path);
  out << header << "\n";
  out << body;
  char footer[32];
  snprintf(footer, sizeof(footer), "footer\t%08x\n", static_cast<unsigned>(crc));
  out << footer;
  out.flush();
  if (!out.good()) return Status::IoError("manifest write failed");
  out.close();
  return FsyncPath(path);
}

Status ParseMetaLine(const std::string& line, ManifestFileEntry* current) {
  if (line.rfind("meta\t", 0) != 0) return Status::Ok();
  std::istringstream iss(line.substr(5));
  std::string token;
  while (iss >> token) {
    const auto eq = token.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = token.substr(0, eq);
    const std::string val = token.substr(eq + 1);
    if (key == "file_type") {
      current->file_type = FileTypeFromString(val);
    } else if (key == "mode") {
      current->mode = static_cast<uint32_t>(std::stoul(val, nullptr, 0));
    } else if (key == "uid") {
      current->uid = static_cast<uint32_t>(std::stoul(val));
    } else if (key == "gid") {
      current->gid = static_cast<uint32_t>(std::stoul(val));
    } else if (key == "mtime") {
      current->mtime_unix = std::stoll(val);
    } else if (key == "atime") {
      current->atime_unix = std::stoll(val);
    }
  }
  return Status::Ok();
}

Status ParseBodyV2(std::istream& body_in, ManifestDocument* out) {
  std::string line;
  if (!std::getline(body_in, line) || line.rfind("txn_id=", 0) != 0) {
    return Status::Corrupt("manifest txn_id missing");
  }
  out->txn_id = std::stoull(line.substr(7));

  ManifestFileEntry current;
  bool have_file = false;
  while (std::getline(body_in, line)) {
    if (line.empty()) continue;
    if (line.rfind("file\t", 0) == 0) {
      if (have_file) out->files.push_back(current);
      current = ManifestFileEntry{};
      std::istringstream iss(line.substr(5));
      size_t path_len = 0;
      uint64_t size = 0;
      size_t chunk_count = 0;
      iss >> path_len >> size >> chunk_count;
      if (!std::getline(body_in, current.relative_path)) {
        return Status::Corrupt("manifest path missing");
      }
      if (current.relative_path.size() != path_len) {
        return Status::Corrupt("manifest path length mismatch");
      }
      current.size = size;
      current.chunk_hashes_hex.reserve(chunk_count);
      have_file = true;
    } else if (line.rfind("meta\t", 0) == 0 && have_file) {
      const Status st = ParseMetaLine(line, &current);
      if (!st.ok()) return st;
    } else if (line.rfind("symlink_target\t", 0) == 0 && have_file) {
      size_t len = static_cast<size_t>(std::stoull(line.substr(16)));
      if (!std::getline(body_in, current.symlink_target)) {
        return Status::Corrupt("symlink target missing");
      }
      if (current.symlink_target.size() != len) {
        return Status::Corrupt("symlink target length mismatch");
      }
    } else if (line.rfind("device\t", 0) == 0 && have_file) {
      std::istringstream iss(line.substr(7));
      iss >> current.device_major >> current.device_minor;
    } else if (line.rfind("chunk\t", 0) == 0 && have_file) {
      current.chunk_hashes_hex.push_back(line.substr(6));
    } else if (line.rfind("anchor\t", 0) == 0 && have_file) {
      ChunkAnchor anchor{};
      std::istringstream iss(line.substr(7));
      int strength = 0;
      std::string hex;
      iss >> anchor.offset >> anchor.length >> strength >> hex;
      anchor.strength = static_cast<AnchorStrength>(strength);
      if (!HexToBytes(hex, anchor.hash, 32)) {
        return Status::Corrupt("invalid anchor hash");
      }
      current.cfi.anchors.push_back(anchor);
    } else {
      return Status::Corrupt("invalid manifest body line");
    }
  }
  if (have_file) out->files.push_back(current);
  return Status::Ok();
}

std::vector<uint8_t> BuildBodyV4(const ManifestDocument& doc) {
  std::vector<uint8_t> body;
  WriteU32(body, kManifestV4InnerMagic);
  WriteU64(body, doc.txn_id);
  WriteU32(body, static_cast<uint32_t>(doc.files.size()));
  for (const auto& f : doc.files) {
    WriteU32(body, static_cast<uint32_t>(f.relative_path.size()));
    body.insert(body.end(), f.relative_path.begin(), f.relative_path.end());
    body.push_back(static_cast<uint8_t>(f.file_type));
    const uint32_t meta_flags = BuildV4MetaFlags(f);
    WriteU32(body, meta_flags);
    WriteU64(body, f.size);
    if (meta_flags & kMetaMode) WriteU32(body, f.mode);
    if (meta_flags & kMetaUidGid) {
      WriteU32(body, f.uid);
      WriteU32(body, f.gid);
    }
    if (meta_flags & kMetaTimes) {
      WriteU64(body, static_cast<uint64_t>(f.mtime_unix));
      WriteU64(body, static_cast<uint64_t>(f.atime_unix));
    }
    if (meta_flags & kMetaSymlink) {
      WriteU32(body, static_cast<uint32_t>(f.symlink_target.size()));
      body.insert(body.end(), f.symlink_target.begin(), f.symlink_target.end());
    }
    if (meta_flags & kMetaDevice) {
      WriteU32(body, f.device_major);
      WriteU32(body, f.device_minor);
    }
    WriteU32(body, static_cast<uint32_t>(f.chunk_hashes_hex.size()));
    for (const auto& hex : f.chunk_hashes_hex) {
      uint8_t hash[32];
      if (!HexToBytes(hex, hash, 32)) continue;
      body.insert(body.end(), hash, hash + 32);
    }
    WriteU32(body, static_cast<uint32_t>(f.cfi.anchors.size()));
    for (const auto& a : f.cfi.anchors) {
      WriteU64(body, a.offset);
      WriteU32(body, a.length);
      body.push_back(static_cast<uint8_t>(a.strength));
      body.push_back(0);
      body.push_back(0);
      body.push_back(0);
      body.insert(body.end(), a.hash, a.hash + 32);
      WriteU32(body, a.rolling_checksum);
    }
  }
  return body;
}

Status ParseBodyV4(const std::vector<uint8_t>& body, ManifestDocument* out) {
  const uint8_t* p = body.data();
  const uint8_t* end = body.data() + body.size();
  uint32_t magic = 0;
  if (!ReadU32(p, end, &magic) || magic != kManifestV4InnerMagic) {
    return Status::Corrupt("manifest v4 inner magic mismatch");
  }
  if (!ReadU64(p, end, &out->txn_id)) {
    return Status::Corrupt("manifest v4 txn_id missing");
  }
  uint32_t file_count = 0;
  if (!ReadU32(p, end, &file_count)) {
    return Status::Corrupt("manifest v4 file_count missing");
  }
  out->files.clear();
  out->files.reserve(file_count);
  for (uint32_t fi = 0; fi < file_count; ++fi) {
    ManifestFileEntry entry{};
    uint32_t path_len = 0;
    if (!ReadU32(p, end, &path_len) || p + path_len > end) {
      return Status::Corrupt("manifest v4 path missing");
    }
    entry.relative_path.assign(reinterpret_cast<const char*>(p), path_len);
    p += path_len;
    if (p >= end) return Status::Corrupt("manifest v4 file_type missing");
    entry.file_type = static_cast<FileType>(*p++);
    uint32_t meta_flags = 0;
    if (!ReadU32(p, end, &meta_flags) || !ReadU64(p, end, &entry.size)) {
      return Status::Corrupt("manifest v4 file header incomplete");
    }
    if (meta_flags & kMetaMode) {
      if (!ReadU32(p, end, &entry.mode)) {
        return Status::Corrupt("manifest v4 mode missing");
      }
    }
    if (meta_flags & kMetaUidGid) {
      if (!ReadU32(p, end, &entry.uid) || !ReadU32(p, end, &entry.gid)) {
        return Status::Corrupt("manifest v4 uid/gid missing");
      }
    }
    if (meta_flags & kMetaTimes) {
      uint64_t mtime = 0;
      uint64_t atime = 0;
      if (!ReadU64(p, end, &mtime) || !ReadU64(p, end, &atime)) {
        return Status::Corrupt("manifest v4 times missing");
      }
      entry.mtime_unix = static_cast<int64_t>(mtime);
      entry.atime_unix = static_cast<int64_t>(atime);
    }
    if (meta_flags & kMetaSymlink) {
      uint32_t sym_len = 0;
      if (!ReadU32(p, end, &sym_len) || p + sym_len > end) {
        return Status::Corrupt("manifest v4 symlink missing");
      }
      entry.symlink_target.assign(reinterpret_cast<const char*>(p), sym_len);
      p += sym_len;
    }
    if (meta_flags & kMetaDevice) {
      if (!ReadU32(p, end, &entry.device_major) ||
          !ReadU32(p, end, &entry.device_minor)) {
        return Status::Corrupt("manifest v4 device missing");
      }
    }
    uint32_t chunk_count = 0;
    if (!ReadU32(p, end, &chunk_count) || p + chunk_count * 32 > end) {
      return Status::Corrupt("manifest v4 chunks missing");
    }
    entry.chunk_hashes_hex.reserve(chunk_count);
    for (uint32_t ci = 0; ci < chunk_count; ++ci) {
      entry.chunk_hashes_hex.push_back(BytesToHex(p, 32));
      p += 32;
    }
    uint32_t anchor_count = 0;
    if (!ReadU32(p, end, &anchor_count)) {
      return Status::Corrupt("manifest v4 anchor_count missing");
    }
    entry.cfi.anchors.reserve(anchor_count);
    for (uint32_t ai = 0; ai < anchor_count; ++ai) {
      ChunkAnchor anchor{};
      if (!ReadU64(p, end, &anchor.offset) ||
          !ReadU32(p, end, &anchor.length) || p + 4 > end) {
        return Status::Corrupt("manifest v4 anchor header missing");
      }
      anchor.strength = static_cast<AnchorStrength>(p[0]);
      p += 4;
      if (p + 32 + 4 > end) {
        return Status::Corrupt("manifest v4 anchor payload missing");
      }
      std::memcpy(anchor.hash, p, 32);
      p += 32;
      if (!ReadU32(p, end, &anchor.rolling_checksum)) {
        return Status::Corrupt("manifest v4 anchor checksum missing");
      }
      entry.cfi.anchors.push_back(anchor);
    }
    out->files.push_back(std::move(entry));
  }
  return Status::Ok();
}

}  // namespace

Status ComputeManifestV4BodyCrc32(const std::vector<uint8_t>& body,
                                  uint32_t* out) {
  if (!out) return Status::InvalidArgument("out is null");
  *out = Crc32(body.data(), body.size());
  return Status::Ok();
}

const char* FileTypeToString(FileType type) {
  switch (type) {
    case FileType::kDirectory:
      return "directory";
    case FileType::kSymlink:
      return "symlink";
    case FileType::kFifo:
      return "fifo";
    case FileType::kBlock:
      return "block";
    case FileType::kChar:
      return "char";
    case FileType::kRegular:
    default:
      return "regular";
  }
}

FileType FileTypeFromString(const std::string& s) {
  if (s == "directory") return FileType::kDirectory;
  if (s == "symlink") return FileType::kSymlink;
  if (s == "fifo") return FileType::kFifo;
  if (s == "block") return FileType::kBlock;
  if (s == "char") return FileType::kChar;
  return FileType::kRegular;
}

ManifestFileEntry ManifestEntryFromScanMeta(
    const std::string& relative_path, FileType type, uint64_t size,
    uint32_t mode, uint32_t uid, uint32_t gid, int64_t mtime, int64_t atime,
    const std::string& symlink_target, uint32_t dev_major, uint32_t dev_minor) {
  ManifestFileEntry entry;
  entry.relative_path = relative_path;
  entry.file_type = type;
  entry.size = size;
  entry.mode = mode;
  entry.uid = uid;
  entry.gid = gid;
  entry.mtime_unix = mtime;
  entry.atime_unix = atime;
  entry.symlink_target = symlink_target;
  entry.device_major = dev_major;
  entry.device_minor = dev_minor;
  return entry;
}

Status ComputeManifestBodyCrc32(const std::string& body, uint32_t* out) {
  if (!out) return Status::InvalidArgument("out is null");
  *out = Crc32(body.data(), body.size());
  return Status::Ok();
}

Status WriteManifestV2(const std::string& path, const ManifestDocument& doc) {
  return WriteManifestWithHeader(path, "EBMANIFEST2", BuildBodyV2(doc));
}

Status WriteManifestV3(const std::string& path, const ManifestDocument& doc) {
  return WriteManifestWithHeader(path, "EBMANIFEST3", BuildBodyV2(doc));
}

Status WriteManifestV4(const std::string& path, const ManifestDocument& doc) {
  const std::vector<uint8_t> body = BuildBodyV4(doc);
  uint32_t crc = 0;
  const Status crc_st = ComputeManifestV4BodyCrc32(body, &crc);
  if (!crc_st.ok()) return crc_st;
  std::ofstream out(PathFromUtf8(path), std::ios::binary | std::ios::trunc);
  if (!out) return Status::IoError("cannot write manifest: " + path);
  out << "EBMANIFEST4\n";
  out.write(reinterpret_cast<const char*>(body.data()),
            static_cast<std::streamsize>(body.size()));
  const uint8_t crc_bytes[4] = {
      static_cast<uint8_t>(crc), static_cast<uint8_t>(crc >> 8),
      static_cast<uint8_t>(crc >> 16), static_cast<uint8_t>(crc >> 24)};
  out.write(reinterpret_cast<const char*>(crc_bytes), 4);
  out.flush();
  if (!out.good()) return Status::IoError("manifest write failed");
  out.close();
  return FsyncPath(path);
}

Status WriteManifestAuto(const std::string& path, const ManifestDocument& doc,
                         bool prefer_binary) {
  if (prefer_binary) {
    return WriteManifestV4(path, doc);
  }
  if (DocumentUsesV3(doc)) {
    return WriteManifestV3(path, doc);
  }
  return WriteManifestV2(path, doc);
}

Status ReadManifestAuto(const std::string& path, ManifestDocument* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->files.clear();
  out->txn_id = 0;

  std::ifstream in(PathFromUtf8(path), std::ios::binary);
  if (!in) return Status::IoError("cannot open manifest: " + path);
  std::string header;
  if (!std::getline(in, header)) return Status::Corrupt("empty manifest");

  if (header == "EBMANIFEST1") {
    ManifestFileEntry current;
    bool have_file = false;
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty()) continue;
      if (line[0] == 'F') {
        if (have_file) out->files.push_back(current);
        current = ManifestFileEntry{};
        std::istringstream iss(line.substr(2));
        uint64_t size = 0;
        size_t chunk_count = 0;
        iss >> current.relative_path >> size >> chunk_count;
        current.size = size;
        current.chunk_hashes_hex.reserve(chunk_count);
        have_file = true;
      } else if (line[0] == 'C' && have_file) {
        current.chunk_hashes_hex.push_back(line.substr(2));
      } else {
        return Status::Corrupt("invalid manifest v1 line");
      }
    }
    if (have_file) out->files.push_back(current);
    return Status::Ok();
  }

  if (header == "EBMANIFEST4") {
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    if (raw.size() < 4) return Status::Corrupt("manifest v4 too short");
    const uint32_t expected =
        static_cast<uint32_t>(raw[raw.size() - 4]) |
        (static_cast<uint32_t>(raw[raw.size() - 3]) << 8) |
        (static_cast<uint32_t>(raw[raw.size() - 2]) << 16) |
        (static_cast<uint32_t>(raw[raw.size() - 1]) << 24);
    raw.resize(raw.size() - 4);
    uint32_t actual = 0;
    const Status crc_st = ComputeManifestV4BodyCrc32(raw, &actual);
    if (!crc_st.ok()) return crc_st;
    if (actual != expected) return Status::Corrupt("manifest v4 crc mismatch");
    return ParseBodyV4(raw, out);
  }

  if (header != "EBMANIFEST2" && header != "EBMANIFEST3") {
    return Status::Corrupt("unknown manifest header");
  }

  std::ostringstream body_stream;
  std::string line;
  std::string footer_line;
  while (std::getline(in, line)) {
    if (line.rfind("footer\t", 0) == 0) {
      footer_line = line;
      break;
    }
    body_stream << line << "\n";
  }
  const std::string body = body_stream.str();
  if (footer_line.empty()) return Status::Corrupt("manifest footer missing");
  const uint32_t expected =
      static_cast<uint32_t>(std::stoul(footer_line.substr(7), nullptr, 16));
  uint32_t actual = 0;
  const Status crc_st = ComputeManifestBodyCrc32(body, &actual);
  if (!crc_st.ok()) return crc_st;
  if (actual != expected) return Status::Corrupt("manifest crc mismatch");

  std::istringstream body_in(body);
  return ParseBodyV2(body_in, out);
}

}  // namespace ebbackup
