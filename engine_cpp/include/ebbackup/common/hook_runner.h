#pragma once

#include <string>

#include "ebbackup/common/status.h"

namespace ebbackup {

Status RunShellCommand(const std::string& command, int* exit_code);

}  // namespace ebbackup
