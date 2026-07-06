#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"

namespace ebbackup {

struct EbBundleOptions {
  bool encrypt_bundle{false};
  std::string password;
};

Status ExportRepoToBundle(const std::string& repo_path,
                          const std::string& bundle_path,
                          const EbBundleOptions& options = EbBundleOptions{});

Status ImportBundleToRepo(const std::string& bundle_path,
                          const std::string& repo_path);

}  // namespace ebbackup
