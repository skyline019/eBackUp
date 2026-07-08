#include "ebbackup/winmeta/vss_shadow_storage.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <comdef.h>
#include <Wbemidl.h>

#pragma comment(lib, "wbemuuid.lib")

#include <array>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "ebbackup/common/path_encoding.h"

namespace ebbackup {
namespace winmeta {
namespace {

uint64_t ParseSizeToken(const std::string& token) {
  double value = 0;
  std::string unit;
  std::istringstream iss(token);
  iss >> value >> unit;
  uint64_t mult = 1;
  if (unit.find("KB") != std::string::npos || unit.find("KB") != std::string::npos) {
    mult = 1024;
  } else if (unit.find("MB") != std::string::npos) {
    mult = 1024ULL * 1024ULL;
  } else if (unit.find("GB") != std::string::npos) {
    mult = 1024ULL * 1024ULL * 1024ULL;
  } else if (unit.find("TB") != std::string::npos) {
    mult = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
  } else if (unit.find("BYTES") != std::string::npos || unit == "B") {
    mult = 1;
  }
  return static_cast<uint64_t>(value * static_cast<double>(mult));
}

std::string RunVssAdminListShadowStorage() {
  std::array<char, 4096> buf{};
  std::string output;
  FILE* pipe = _popen("vssadmin list shadowstorage", "r");
  if (!pipe) return output;
  while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe)) {
    output += buf.data();
  }
  _pclose(pipe);
  return output;
}

void ParseShadowStorageText(const std::string& text,
                            std::vector<VssShadowStorageInfo>* out) {
  if (!out) return;
  VssShadowStorageInfo current{};
  auto flush = [&]() {
    if (!current.volume.empty()) out->push_back(current);
    current = VssShadowStorageInfo{};
  };
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.find("For volume") != std::string::npos) {
      flush();
      const size_t p1 = line.find('(');
      const size_t p2 = line.find(')', p1);
      if (p1 != std::string::npos && p2 != std::string::npos) {
        current.volume = line.substr(p1 + 1, p2 - p1 - 1);
      }
      const size_t s = line.find("on volume");
      if (s != std::string::npos) {
        const size_t p1b = line.find('(', s);
        const size_t p2b = line.find(')', p1b);
        if (p1b != std::string::npos && p2b != std::string::npos) {
          current.diff_volume = line.substr(p1b + 1, p2b - p1b - 1);
        }
      }
    } else if (line.find("Used Shadow Copy Storage space:") != std::string::npos) {
      const size_t colon = line.find(':');
      if (colon != std::string::npos) {
        current.used_bytes = ParseSizeToken(line.substr(colon + 1));
      }
    } else if (line.find("Maximum Shadow Copy Storage space:") != std::string::npos) {
      const size_t colon = line.find(':');
      if (colon != std::string::npos) {
        current.max_bytes = ParseSizeToken(line.substr(colon + 1));
      }
    } else if (line.find("Allocated Shadow Copy Storage space:") != std::string::npos) {
      const size_t colon = line.find(':');
      if (colon != std::string::npos) {
        current.allocated_bytes = ParseSizeToken(line.substr(colon + 1));
      }
    }
  }
  flush();
}

std::string JsonEscapeSimple(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '\\' || c == '"') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

bool VolumeMatches(const std::string& spec_mount, const std::string& assoc_volume) {
  if (spec_mount.empty() || assoc_volume.empty()) return false;
  std::string a = spec_mount;
  std::string b = assoc_volume;
  if (!a.empty() && a.back() != '\\') a.push_back('\\');
  if (!b.empty() && b.back() != '\\') b.push_back('\\');
  return _stricmp(a.c_str(), b.c_str()) == 0 ||
         a.find(b) != std::string::npos || b.find(a) != std::string::npos;
}

std::string BstrToUtf8(BSTR bstr) {
  if (!bstr) return {};
  const int wide_len = SysStringLen(bstr);
  if (wide_len <= 0) return {};
  const int utf8_len =
      WideCharToMultiByte(CP_UTF8, 0, bstr, wide_len, nullptr, 0, nullptr, nullptr);
  if (utf8_len <= 0) return {};
  std::string out(static_cast<size_t>(utf8_len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, bstr, wide_len, out.data(), utf8_len, nullptr, nullptr);
  return out;
}

uint64_t VariantToU64(const VARIANT& v) {
  switch (v.vt) {
    case VT_UI8:
      return static_cast<uint64_t>(v.ullVal);
    case VT_I8:
      return static_cast<uint64_t>(v.llVal);
    case VT_UI4:
      return static_cast<uint64_t>(v.ulVal);
    case VT_I4:
      return static_cast<uint64_t>(v.lVal);
    case VT_BSTR: {
      if (!v.bstrVal) return 0;
      return static_cast<uint64_t>(_wcstoui64(v.bstrVal, nullptr, 10));
    }
    default:
      return 0;
  }
}

Status QueryShadowStorageViaWmi(std::vector<VssShadowStorageInfo>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();

  const HRESULT co_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool co_owned = SUCCEEDED(co_hr) || co_hr == RPC_E_CHANGED_MODE;

  IWbemLocator* locator = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IWbemLocator, reinterpret_cast<void**>(&locator));
  if (FAILED(hr) || !locator) {
    if (co_owned && SUCCEEDED(co_hr)) CoUninitialize();
    return Status::IoError("WMI CoCreateInstance failed");
  }

  IWbemServices* services = nullptr;
  hr = locator->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr, 0,
                              nullptr, nullptr, &services);
  locator->Release();
  if (FAILED(hr) || !services) {
    if (co_owned && SUCCEEDED(co_hr)) CoUninitialize();
    return Status::IoError("WMI ConnectServer failed");
  }

  hr = CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                         RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr,
                         EOAC_NONE);
  if (FAILED(hr)) {
    services->Release();
    if (co_owned && SUCCEEDED(co_hr)) CoUninitialize();
    return Status::IoError("WMI CoSetProxyBlanket failed");
  }

  IEnumWbemClassObject* enumerator = nullptr;
  hr = services->ExecQuery(
      _bstr_t(L"WQL"),
      _bstr_t(L"SELECT AllocatedSpace,DiffVolume,MaxSpace,UsedSpace,Volume "
              L"FROM Win32_ShadowStorage"),
      WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &enumerator);
  services->Release();
  if (FAILED(hr) || !enumerator) {
    if (co_owned && SUCCEEDED(co_hr)) CoUninitialize();
    return Status::IoError("WMI ExecQuery failed");
  }

  IWbemClassObject* obj = nullptr;
  ULONG returned = 0;
  while (enumerator->Next(WBEM_INFINITE, 1, &obj, &returned) == S_OK && returned > 0) {
    VssShadowStorageInfo info{};
    VARIANT var{};
    if (SUCCEEDED(obj->Get(L"Volume", 0, &var, nullptr, nullptr))) {
      info.volume = BstrToUtf8(var.bstrVal);
      VariantClear(&var);
    }
    if (SUCCEEDED(obj->Get(L"DiffVolume", 0, &var, nullptr, nullptr))) {
      info.diff_volume = BstrToUtf8(var.bstrVal);
      VariantClear(&var);
    }
    if (SUCCEEDED(obj->Get(L"UsedSpace", 0, &var, nullptr, nullptr))) {
      info.used_bytes = VariantToU64(var);
      VariantClear(&var);
    }
    if (SUCCEEDED(obj->Get(L"MaxSpace", 0, &var, nullptr, nullptr))) {
      info.max_bytes = VariantToU64(var);
      VariantClear(&var);
    }
    if (SUCCEEDED(obj->Get(L"AllocatedSpace", 0, &var, nullptr, nullptr))) {
      info.allocated_bytes = VariantToU64(var);
      VariantClear(&var);
    }
    if (!info.volume.empty()) out->push_back(std::move(info));
    obj->Release();
    obj = nullptr;
  }
  enumerator->Release();
  if (co_owned && SUCCEEDED(co_hr)) CoUninitialize();
  return out->empty() ? Status::NotFound("WMI shadow storage empty") : Status::Ok();
}

}  // namespace

Status QueryShadowStorage(std::vector<VssShadowStorageInfo>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  const Status wmi = QueryShadowStorageViaWmi(out);
  if (wmi.ok() && !out->empty()) return Status::Ok();
  out->clear();
  ParseShadowStorageText(RunVssAdminListShadowStorage(), out);
  return Status::Ok();
}

Status CheckShadowStoragePreflightEx(const std::vector<VssVolumeSpec>& volumes,
                                     uint64_t min_free_bytes,
                                     std::vector<VssShadowStorageInfo>* storage_out) {
  std::vector<VssShadowStorageInfo> all;
  (void)QueryShadowStorage(&all);
  if (storage_out) *storage_out = all;

  for (const auto& vol : volumes) {
    const VssShadowStorageInfo* match = nullptr;
    for (const auto& s : all) {
      if (VolumeMatches(vol.mount_point_utf8, s.volume) ||
          VolumeMatches(vol.mount_point_utf8, s.diff_volume)) {
        match = &s;
        break;
      }
    }
    if (match && match->max_bytes > 0) {
      const uint64_t headroom =
          match->max_bytes > match->used_bytes ? match->max_bytes - match->used_bytes
                                               : 0;
      if (headroom < min_free_bytes) {
        return Status::InvalidArgument(
            "vss_shadow_storage_low on " + vol.mount_point_utf8 + " headroom=" +
            std::to_string(headroom));
      }
      continue;
    }
    ULARGE_INTEGER free_bytes{};
    ULARGE_INTEGER total{};
    ULARGE_INTEGER total_free{};
    const std::wstring mount = Utf8ToWide(vol.mount_point_utf8);
    if (!GetDiskFreeSpaceExW(mount.c_str(), &free_bytes, &total, &total_free)) {
      return Status::IoError("GetDiskFreeSpaceEx failed for " + vol.mount_point_utf8);
    }
    if (free_bytes.QuadPart < min_free_bytes) {
      return Status::InvalidArgument(
          "vss_shadow_association_missing on " + vol.mount_point_utf8);
    }
  }
  return Status::Ok();
}

std::string FormatShadowStorageStatusJson(
    const std::vector<VssShadowStorageInfo>& entries) {
  std::ostringstream out;
  out << "{\"ok\":true,\"entries\":[";
  for (size_t i = 0; i < entries.size(); ++i) {
    if (i) out << ',';
    const auto& e = entries[i];
    out << "{\"volume\":\"" << JsonEscapeSimple(e.volume) << "\",\"diff_volume\":\""
        << JsonEscapeSimple(e.diff_volume) << "\",\"used_bytes\":" << e.used_bytes
        << ",\"max_bytes\":" << e.max_bytes << ",\"allocated_bytes\":" << e.allocated_bytes
        << "}";
  }
  out << "]}";
  return out.str();
}

}  // namespace winmeta
}  // namespace ebbackup

#endif
