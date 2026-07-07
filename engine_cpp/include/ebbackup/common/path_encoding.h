#pragma once

#include <filesystem>
#include <string>

namespace ebbackup {

// All C API / engine path strings are UTF-8 (no embedded NUL).

std::filesystem::path PathFromUtf8(const std::string& utf8);
std::string PathToUtf8(const std::filesystem::path& path);

#ifdef _WIN32
std::wstring Utf8ToWide(const std::string& utf8);
std::string WideToUtf8(const std::wstring& wide);
#endif

}  // namespace ebbackup
