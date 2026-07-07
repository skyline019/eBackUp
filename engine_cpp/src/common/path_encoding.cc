#include "ebbackup/common/path_encoding.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ebbackup {

#ifdef _WIN32

std::wstring Utf8ToWide(const std::string& utf8) {
  if (utf8.empty()) return std::wstring();
  const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                       static_cast<int>(utf8.size()), nullptr, 0);
  if (size <= 0) return std::wstring();
  std::wstring out(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                      out.data(), size);
  return out;
}

std::string WideToUtf8(const std::wstring& wide) {
  if (wide.empty()) return std::string();
  const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                       static_cast<int>(wide.size()), nullptr, 0,
                                       nullptr, nullptr);
  if (size <= 0) return std::string();
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

std::filesystem::path PathFromUtf8(const std::string& utf8) {
  return std::filesystem::path(Utf8ToWide(utf8));
}

std::string PathToUtf8(const std::filesystem::path& path) {
  return WideToUtf8(path.wstring());
}

#else

std::filesystem::path PathFromUtf8(const std::string& utf8) {
  return std::filesystem::path(utf8);
}

std::string PathToUtf8(const std::filesystem::path& path) {
  return path.string();
}

#endif

}  // namespace ebbackup
