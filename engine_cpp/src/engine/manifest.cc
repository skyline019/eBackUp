#include "ebbackup/engine/manifest.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#include "ebbackup/common/crc32.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/fsync.h"

namespace ebbackup {

namespace {

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
  std::ofstream out(path, std::ios::binary);
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

}  // namespace

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

Status WriteManifestAuto(const std::string& path, const ManifestDocument& doc) {
  if (DocumentUsesV3(doc)) {
    return WriteManifestV3(path, doc);
  }
  return WriteManifestV2(path, doc);
}

Status ReadManifestAuto(const std::string& path, ManifestDocument* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->files.clear();
  out->txn_id = 0;

  std::ifstream in(path, std::ios::binary);
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
