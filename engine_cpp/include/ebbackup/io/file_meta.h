#pragma once

#include "ebbackup/common/status.h"
#include "ebbackup/engine/manifest.h"

namespace ebbackup {

Status ApplyFileMeta(const std::string& path, const ManifestFileEntry& entry);

}  // namespace ebbackup
