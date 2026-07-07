#include "fixture_util.h"

#include <fstream>
#include <unordered_map>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "ebbackup/common/digest.h"

namespace ebbackup {
namespace test {

namespace {

std::filesystem::path EngineCppRoot() {
#ifdef EBTEST_ENGINE_ROOT
  return std::filesystem::path(EBTEST_ENGINE_ROOT);
#else
  return std::filesystem::path("engine_cpp");
#endif
}

void CopyRecursive(const std::filesystem::path& from,
                   const std::filesystem::path& to, std::error_code& ec) {
  std::filesystem::create_directories(to, ec);
  for (const auto& entry : std::filesystem::directory_iterator(from, ec)) {
    if (ec) return;
    const auto dest = to / entry.path().filename();
    if (entry.is_directory()) {
      CopyRecursive(entry.path(), dest, ec);
    } else if (entry.is_regular_file()) {
      std::filesystem::copy_file(entry.path(), dest,
                                 std::filesystem::copy_options::overwrite_existing,
                                 ec);
    }
  }
}

std::string NormalizeRelPath(std::string rel) {
  for (char& c : rel) {
    if (c == '\\') c = '/';
  }
  return rel;
}

std::string RelPathUtf8(const std::filesystem::path& path,
                        const std::filesystem::path& base) {
#ifdef _WIN32
  const std::wstring rel = std::filesystem::relative(path, base).wstring();
  if (rel.empty()) return std::string();
  const int size = WideCharToMultiByte(CP_UTF8, 0, rel.c_str(),
                                       static_cast<int>(rel.size()), nullptr, 0,
                                       nullptr, nullptr);
  if (size <= 0) return std::string();
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, rel.c_str(), static_cast<int>(rel.size()),
                      out.data(), size, nullptr, nullptr);
  return NormalizeRelPath(out);
#else
  return NormalizeRelPath(std::filesystem::relative(path, base).string());
#endif
}

bool ExtractJsonString(const std::string& json, size_t* pos, std::string* out) {
  const size_t start = json.find('"', *pos);
  if (start == std::string::npos) return false;
  size_t i = start + 1;
  std::string value;
  while (i < json.size()) {
    const char c = json[i];
    if (c == '"') {
      *pos = i + 1;
      *out = value;
      return true;
    }
    if (c == '\\' && i + 1 < json.size()) {
      const char esc = json[++i];
      switch (esc) {
        case '"': value.push_back('"'); break;
        case '\\': value.push_back('\\'); break;
        case '/': value.push_back('/'); break;
        case 'n': value.push_back('\n'); break;
        case 'r': value.push_back('\r'); break;
        case 't': value.push_back('\t'); break;
        default: value.push_back(esc); break;
      }
    } else {
      value.push_back(c);
    }
    ++i;
  }
  return false;
}

bool IsManifestMetaFile(const std::string& rel) {
  return rel == "full_manifest.json" || rel == "media_manifest.json";
}

bool ExtractJsonField(const std::string& object, const char* key, std::string* out) {
  const std::string needle = std::string("\"") + key + "\"";
  const size_t key_pos = object.find(needle);
  if (key_pos == std::string::npos) return false;
  size_t pos = key_pos + needle.size();
  while (pos < object.size() && object[pos] != ':') ++pos;
  if (pos >= object.size()) return false;
  ++pos;
  return ExtractJsonString(object, &pos, out);
}

}  // namespace

std::filesystem::path FixtureRoot() {
#ifdef EBTEST_FIXTURE_DIR
  return std::filesystem::path(EBTEST_FIXTURE_DIR);
#else
  return std::filesystem::path("engine_cpp/tests/fixtures/real_world");
#endif
}

Status CopyFixtureTree(const std::string& name, const std::string& dest) {
  const auto src = FixtureRoot() / name;
  if (!std::filesystem::exists(src)) {
    return Status::NotFound("fixture not found: " + src.string());
  }
  std::error_code ec;
  CopyRecursive(src, std::filesystem::path(dest), ec);
  if (ec) return Status::IoError("fixture copy failed: " + ec.message());
  if (name == "mixed") {
    const std::string bin_path = dest + "/image_stub.bin";
    if (!std::filesystem::exists(bin_path)) {
      static const uint8_t kPngStub[] = {
          0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
          0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
          0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89};
      std::ofstream out(bin_path, std::ios::binary);
      out.write(reinterpret_cast<const char*>(kPngStub), sizeof(kPngStub));
      out.write(std::string(512, '\xAB').data(), 512);
    }
  }
  return Status::Ok();
}

Status CopyEngineSourceSample(const std::string& dest) {
  const auto src_dir = EngineCppRoot() / "src" / "engine";
  if (!std::filesystem::exists(src_dir)) {
    return Status::NotFound("engine source dir missing");
  }
  std::error_code ec;
  int copied = 0;
  for (const auto& entry : std::filesystem::directory_iterator(src_dir, ec)) {
    if (ec || !entry.is_regular_file()) continue;
    if (entry.path().extension() != ".cc") continue;
    const auto out = std::filesystem::path(dest) / entry.path().filename();
    std::filesystem::create_directories(out.parent_path(), ec);
    std::filesystem::copy_file(entry.path(), out,
                               std::filesystem::copy_options::overwrite_existing,
                               ec);
    if (ec) return Status::IoError("engine sample copy failed");
    if (++copied >= 20) break;
  }
  if (copied == 0) return Status::NotFound("no engine .cc files copied");
  return Status::Ok();
}

Status HashFixtureTree(
    const std::string& root,
    const std::function<Status(const std::string& rel_path,
                               const std::string& sha256_hex)>& fn) {
  const auto base = std::filesystem::path(root);
  std::error_code ec;
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(base, ec)) {
    if (ec) return Status::IoError("walk failed");
    if (!entry.is_regular_file()) continue;
    const auto rel = RelPathUtf8(entry.path(), base);
    std::ifstream in(entry.path(), std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    const Status st = fn(rel, Sha256HexString(bytes));
    if (!st.ok()) return st;
  }
  return Status::Ok();
}

Status LoadFixtureManifest(const std::string& manifest_path,
                           std::vector<FixtureFileExpectation>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  std::ifstream in(manifest_path, std::ios::binary);
  if (!in) return Status::NotFound("manifest not found: " + manifest_path);
  const std::string json((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());

  size_t pos = 0;
  while (pos < json.size()) {
    const size_t obj_start = json.find('{', pos);
    if (obj_start == std::string::npos) break;
    const size_t obj_end = json.find('}', obj_start);
    if (obj_end == std::string::npos) {
      return Status::Corrupt("manifest object unterminated");
    }
    const std::string object = json.substr(obj_start, obj_end - obj_start + 1);

    std::string path;
    std::string sha256;
    if (!ExtractJsonField(object, "path", &path) ||
        !ExtractJsonField(object, "sha256", &sha256)) {
      return Status::Corrupt("manifest entry missing path or sha256");
    }

    FixtureFileExpectation entry{};
    entry.path = NormalizeRelPath(path);
    entry.sha256 = sha256;
    out->push_back(entry);
    pos = obj_end + 1;
  }

  if (out->empty()) return Status::Corrupt("manifest empty or invalid");
  return Status::Ok();
}

Status AssertTreeMatchesManifest(const std::string& root,
                                 const std::string& manifest_path) {
  std::vector<FixtureFileExpectation> expected;
  const Status load = LoadFixtureManifest(manifest_path, &expected);
  if (!load.ok()) return load;

  std::unordered_map<std::string, std::string> actual;
  const Status walk = HashFixtureTree(
      root, [&](const std::string& rel, const std::string& sha) -> Status {
        const std::string norm = NormalizeRelPath(rel);
        if (IsManifestMetaFile(norm)) return Status::Ok();
        actual[norm] = sha;
        return Status::Ok();
      });
  if (!walk.ok()) return walk;

  for (const auto& exp : expected) {
    const auto it = actual.find(exp.path);
    if (it == actual.end()) {
      return Status::Corrupt("missing file in tree: " + exp.path);
    }
    if (it->second != exp.sha256) {
      return Status::Corrupt("hash mismatch: " + exp.path);
    }
  }

  for (const auto& kv : actual) {
    if (IsManifestMetaFile(kv.first)) continue;
    bool found = false;
    for (const auto& exp : expected) {
      if (exp.path == kv.first) {
        found = true;
        break;
      }
    }
    if (!found) {
      return Status::Corrupt("unexpected file in tree: " + kv.first);
    }
  }

  return Status::Ok();
}

}  // namespace test
}  // namespace ebbackup
