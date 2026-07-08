#include "ebbackup/winmeta/sparse_file.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>

#include "ebbackup/common/path_encoding.h"

namespace ebbackup {
namespace winmeta {
namespace {

struct SparseRetrievalPointers {
  DWORD ExtentCount;
  LARGE_INTEGER StartingVcn;
  struct {
    LARGE_INTEGER NextVcn;
    LARGE_INTEGER Lcn;
  } Extents[1];
};

Status OpenFileNoFollow(const std::wstring& wide, HANDLE* out) {
  if (!out) return Status::InvalidArgument("out is null");
  *out = CreateFileW(wide.c_str(), GENERIC_READ,
                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                     nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (*out == INVALID_HANDLE_VALUE) {
    return Status::IoError("CreateFile failed for sparse query");
  }
  return Status::Ok();
}

}  // namespace

bool IsSparseFilePath(const std::string& path_utf8) {
  const DWORD attrs = GetFileAttributesW(Utf8ToWide(path_utf8).c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return false;
  return (attrs & FILE_ATTRIBUTE_SPARSE_FILE) != 0;
}

Status QuerySparseRuns(const std::string& path_utf8, uint64_t* logical_size,
                       std::vector<SparseRun>* runs) {
  if (!logical_size || !runs) return Status::InvalidArgument("null out");
  runs->clear();
  *logical_size = 0;

  HANDLE h = INVALID_HANDLE_VALUE;
  const Status open_st = OpenFileNoFollow(Utf8ToWide(path_utf8), &h);
  if (!open_st.ok()) return open_st;

  LARGE_INTEGER size{};
  if (!GetFileSizeEx(h, &size)) {
    CloseHandle(h);
    return Status::IoError("GetFileSizeEx failed");
  }
  *logical_size = static_cast<uint64_t>(size.QuadPart);

  if (!IsSparseFilePath(path_utf8)) {
    if (*logical_size > 0) {
      runs->push_back({0, *logical_size});
    }
    CloseHandle(h);
    return Status::Ok();
  }

  STARTING_VCN_INPUT_BUFFER in{};
  in.StartingVcn.QuadPart = 0;
  std::vector<uint8_t> buf(4096);
  DWORD ret = 0;
  while (true) {
    if (!DeviceIoControl(h, FSCTL_GET_RETRIEVAL_POINTERS, &in,
                         sizeof(in), buf.data(),
                         static_cast<DWORD>(buf.size()), &ret, nullptr)) {
      const DWORD err = GetLastError();
      if (err == ERROR_HANDLE_EOF || err == ERROR_MORE_DATA) {
        break;
      }
      CloseHandle(h);
      return Status::IoError("FSCTL_GET_RETRIEVAL_POINTERS failed");
    }
    if (ret < sizeof(SparseRetrievalPointers)) break;
    const auto* rp = reinterpret_cast<const SparseRetrievalPointers*>(buf.data());
    const DWORD count = rp->ExtentCount;
    const size_t need = sizeof(SparseRetrievalPointers) +
                        (count > 0 ? (count - 1) * sizeof(rp->Extents[0]) : 0);
    if (ret < need) break;

    ULONGLONG vcn = static_cast<ULONGLONG>(rp->StartingVcn.QuadPart);
    for (DWORD i = 0; i < count; ++i) {
      const ULONGLONG next_vcn = static_cast<ULONGLONG>(rp->Extents[i].NextVcn.QuadPart);
      const LONGLONG lcn = rp->Extents[i].Lcn.QuadPart;
      if (lcn != -1) {
        const uint64_t clusters = next_vcn - vcn;
        DWORD sectors_per_cluster = 0;
        DWORD bytes_per_sector = 0;
        DWORD free_clusters = 0;
        DWORD total_clusters = 0;
        wchar_t root[] = L"X:\\";
        root[0] = Utf8ToWide(path_utf8)[0];
        uint64_t cluster = 4096;
        if (GetDiskFreeSpaceW(root, &sectors_per_cluster, &bytes_per_sector,
                              &free_clusters, &total_clusters)) {
          cluster = static_cast<uint64_t>(sectors_per_cluster) *
                    static_cast<uint64_t>(bytes_per_sector);
          if (cluster == 0) cluster = 4096;
        }
        const uint64_t run_off = vcn * cluster;
        const uint64_t run_len = clusters * cluster;
        if (run_off + run_len > *logical_size) {
          runs->push_back({run_off, *logical_size - run_off});
        } else {
          runs->push_back({run_off, run_len});
        }
      }
      vcn = next_vcn;
    }
    in.StartingVcn.QuadPart = static_cast<LONGLONG>(vcn);
    if (count == 0) break;
  }
  CloseHandle(h);

  if (runs->empty() && *logical_size > 0) {
    runs->push_back({0, *logical_size});
  }
  return Status::Ok();
}

Status ReadSparseFileBytes(const std::string& path_utf8,
                           const std::vector<SparseRun>& runs,
                           std::vector<uint8_t>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  uint64_t total = 0;
  for (const auto& r : runs) total += r.length;
  out->reserve(total);

  HANDLE h = INVALID_HANDLE_VALUE;
  const Status open_st = OpenFileNoFollow(Utf8ToWide(path_utf8), &h);
  if (!open_st.ok()) return open_st;

  for (const auto& run : runs) {
    LARGE_INTEGER pos{};
    pos.QuadPart = static_cast<LONGLONG>(run.offset);
    if (!SetFilePointerEx(h, pos, nullptr, FILE_BEGIN)) {
      CloseHandle(h);
      return Status::IoError("SetFilePointerEx failed");
    }
    std::vector<uint8_t> buf(static_cast<size_t>(run.length));
    DWORD read = 0;
    if (!ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr) ||
        read != buf.size()) {
      CloseHandle(h);
      return Status::IoError("ReadFile sparse run failed");
    }
    out->insert(out->end(), buf.begin(), buf.end());
  }
  CloseHandle(h);
  return Status::Ok();
}

}  // namespace winmeta
}  // namespace ebbackup

#endif
