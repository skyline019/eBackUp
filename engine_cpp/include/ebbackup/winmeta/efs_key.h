#pragma once

#include <string>

#include "ebbackup/common/status.h"

namespace ebbackup {
namespace winmeta {

#ifdef _WIN32
Status ExportEfsKeyBlob(const std::string& path_utf8, std::string* out_b64);
Status ImportEfsKeyBlob(const std::string& path_utf8, const std::string& b64);
#endif

}  // namespace winmeta
}  // namespace ebbackup
