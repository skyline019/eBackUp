#pragma once

#include <string>

#include "ebbackup/common/status.h"

namespace ebbackup {

Status FsyncPath(const std::string& path);

}  // namespace ebbackup
