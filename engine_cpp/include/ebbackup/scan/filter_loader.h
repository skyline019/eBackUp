#pragma once

#include <cstdint>
#include <string>

#include "ebbackup/common/status.h"
#include "ebbackup/scan/backup_filter.h"

namespace ebbackup {

Status LoadFilterFromFile(const std::string& path, BackupFilterOptions* out);

void MergeCliFilterFlags(int argc, char** argv, BackupFilterOptions* filter);

Status ApplyFilterFileIfFlag(int argc, char** argv, const char* flag,
                             BackupFilterOptions* filter);

Status LoadFilterFromCli(int argc, char** argv, BackupFilterOptions* filter);

}  // namespace ebbackup
