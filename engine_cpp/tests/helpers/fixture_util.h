#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"

namespace ebbackup {
namespace test {

struct FixtureFileExpectation {
  std::string path;
  std::string sha256;
};

std::filesystem::path FixtureRoot();

Status CopyFixtureTree(const std::string& name, const std::string& dest);

Status CopyEngineSourceSample(const std::string& dest);

Status HashFixtureTree(const std::string& root,
                       const std::function<Status(const std::string& rel_path,
                                                  const std::string& sha256_hex)>& fn);

Status LoadFixtureManifest(const std::string& manifest_path,
                           std::vector<FixtureFileExpectation>* out);

Status AssertTreeMatchesManifest(const std::string& root,
                                 const std::string& manifest_path);

}  // namespace test
}  // namespace ebbackup
