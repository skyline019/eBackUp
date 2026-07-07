#include "tree_util.h"

#include <fstream>
#include <functional>

#include "ebbackup/common/digest.h"
#include "test_util.h"

namespace ebbackup {
namespace test {

namespace {

Status HashRegularFile(const std::filesystem::path& path, std::string* hex) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::IoError("cannot read: " + path.string());
  const std::string bytes((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
  *hex = Sha256HexString(bytes);
  return Status::Ok();
}

void WalkRegularFiles(const std::filesystem::path& root,
                      const std::function<void(const std::filesystem::path&)>& fn) {
  std::error_code ec;
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(root, ec)) {
    if (ec) break;
    if (entry.is_regular_file()) fn(entry.path());
  }
}

}  // namespace

Status BuildNestedTree(const std::string& root, int depth, int breadth,
                       size_t file_size) {
  if (depth <= 0 || breadth <= 0) {
    return Status::InvalidArgument("depth and breadth must be positive");
  }
  std::function<void(const std::string&, int)> build;
  build = [&](const std::string& path, int level) {
    if (level >= depth) {
      WriteFile(path + "/leaf.bin", MakeSyntheticData(file_size, static_cast<uint8_t>(level)));
      return;
    }
    for (int i = 0; i < breadth; ++i) {
      const std::string child = path + "/d" + std::to_string(level) + "_" + std::to_string(i);
      std::filesystem::create_directories(child);
      build(child, level + 1);
    }
  };
  std::filesystem::create_directories(root);
  build(root, 0);
  return Status::Ok();
}

Status BuildUnicodePathTree(const std::string& root) {
  std::error_code ec;
  std::filesystem::create_directories(root + "/docs/中文", ec);
  if (ec) return Status::IoError("unicode path create failed");
  WriteFile(root + "/docs/中文/read me.txt", "unicode-content");
  std::filesystem::create_directories(root + "/a b/c.d", ec);
  if (ec) return Status::IoError("space path create failed");
  WriteFile(root + "/a b/c.d/e.txt", "space-path");
  return Status::Ok();
}

Status CompareDirectoryTrees(const std::string& expected_root,
                             const std::string& actual_root) {
  const auto exp = std::filesystem::path(expected_root);
  const auto act = std::filesystem::path(actual_root);
  const size_t exp_count = CountRegularFiles(expected_root);

  size_t matched = 0;
  WalkRegularFiles(exp, [&](const std::filesystem::path& path) {
    const auto rel = std::filesystem::relative(path, exp).string();
    const auto other = act / rel;
    if (!std::filesystem::is_regular_file(other)) return;
    std::string exp_hash;
    std::string act_hash;
    if (!HashRegularFile(path, &exp_hash).ok()) return;
    if (!HashRegularFile(other, &act_hash).ok()) return;
    if (exp_hash == act_hash) ++matched;
  });

  const size_t act_count = CountRegularFiles(actual_root);
  if (exp_count != act_count || matched != exp_count) {
    return Status::Corrupt("directory tree mismatch");
  }
  return Status::Ok();
}

size_t CountRegularFiles(const std::string& root) {
  size_t count = 0;
  WalkRegularFiles(std::filesystem::path(root),
                   [&](const std::filesystem::path&) { ++count; });
  return count;
}

}  // namespace test
}  // namespace ebbackup
