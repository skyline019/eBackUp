#pragma once

namespace ebbackup {

int EbServiceRun(const char* config_path);
int EbServiceInstall(const char* config_path, const char* name,
                     const char* display_name);
int EbServiceUninstall(const char* name);
int EbServiceStatus(const char* name);

}  // namespace ebbackup
