#include "eb_service.h"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

#include "ebbackup/daemon/backup_daemon.h"

namespace ebbackup {
namespace {

constexpr const char* kDefaultServiceName = "EbbackupDaemon";
SERVICE_STATUS g_service_status{};
SERVICE_STATUS_HANDLE g_service_status_handle = nullptr;
HANDLE g_service_stop_event = nullptr;
std::string g_service_config_path;
std::mutex g_log_mu;

void ServiceLog(const char* msg) {
  std::lock_guard<std::mutex> lock(g_log_mu);
  std::error_code ec;
  const std::string dir = "C:\\ProgramData\\ebbackup";
  std::filesystem::create_directories(dir, ec);
  std::ofstream out(dir + "\\service.log", std::ios::app);
  if (out) {
    out << msg << '\n';
  }
  std::fprintf(stderr, "%s\n", msg);
}

void ReportServiceState(DWORD state, DWORD exit_code = NO_ERROR,
                        DWORD checkpoint = 0) {
  g_service_status.dwCurrentState = state;
  g_service_status.dwWin32ExitCode = exit_code;
  g_service_status.dwCheckPoint = checkpoint;
  if (g_service_status_handle) {
    SetServiceStatus(g_service_status_handle, &g_service_status);
  }
}

VOID WINAPI ServiceCtrlHandler(DWORD ctrl) {
  if (ctrl == SERVICE_CONTROL_STOP) {
    ReportServiceState(SERVICE_STOP_PENDING);
    RequestDaemonStop();
    if (g_service_stop_event) SetEvent(g_service_stop_event);
  }
}

VOID WINAPI ServiceMain(DWORD, LPTSTR*) {
  g_service_status_handle =
      RegisterServiceCtrlHandlerA(kDefaultServiceName, ServiceCtrlHandler);
  if (!g_service_status_handle) return;

  g_service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  g_service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
  g_service_status.dwServiceSpecificExitCode = 0;
  ReportServiceState(SERVICE_START_PENDING);

  g_service_stop_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  if (!g_service_stop_event) {
    ReportServiceState(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 1);
    return;
  }

  ReportServiceState(SERVICE_RUNNING);
  ServiceLog("ebbackup service starting");

  ScheduleConfig cfg{};
  const Status load_st = LoadScheduleConfigAuto(g_service_config_path, &cfg);
  if (!load_st.ok()) {
    ServiceLog(("config load failed: " + load_st.message()).c_str());
    ReportServiceState(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 2);
    CloseHandle(g_service_stop_event);
    return;
  }

  const Status run_st = RunScheduleConfig(cfg, -1);
  if (!run_st.ok()) {
    ServiceLog(("daemon run failed: " + run_st.message()).c_str());
  } else {
    ServiceLog("ebbackup service stopped cleanly");
  }

  CloseHandle(g_service_stop_event);
  g_service_stop_event = nullptr;
  ReportServiceState(SERVICE_STOPPED);
}

std::string QuoteForSc(const std::string& path) {
  return "\"" + path + "\"";
}

std::string ResolveEbExePath() {
  char buf[MAX_PATH]{};
  const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return "eb.exe";
  return std::string(buf);
}

}  // namespace

int EbServiceRun(const char* config_path) {
  if (!config_path || !config_path[0]) {
    std::fprintf(stderr, "error: --config required\n");
    return 1;
  }
  g_service_config_path = config_path;

  SERVICE_TABLE_ENTRYA table[] = {
      {const_cast<LPSTR>(kDefaultServiceName), ServiceMain},
      {nullptr, nullptr},
  };
  if (!StartServiceCtrlDispatcherA(table)) {
    const DWORD err = GetLastError();
    std::fprintf(stderr, "error: StartServiceCtrlDispatcher failed (%lu)\n", err);
    return 1;
  }
  return 0;
}

int EbServiceInstall(const char* config_path, const char* name,
                     const char* display_name) {
  if (!config_path || !config_path[0]) {
    std::fprintf(stderr, "error: --config required\n");
    return 1;
  }
  const std::string svc_name = (name && name[0]) ? name : kDefaultServiceName;
  const std::string svc_display =
      (display_name && display_name[0]) ? display_name : "ebbackup Daemon";
  const std::string exe = ResolveEbExePath();
  const std::string bin_path = QuoteForSc(exe) + " service run --config " +
                               QuoteForSc(config_path);

  SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
  if (!scm) {
    std::fprintf(stderr, "error: OpenSCManager failed\n");
    return 1;
  }
  SC_HANDLE svc = CreateServiceA(
      scm, svc_name.c_str(), svc_display.c_str(), SERVICE_ALL_ACCESS,
      SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
      bin_path.c_str(), nullptr, nullptr, nullptr, "NT AUTHORITY\\LocalService",
      nullptr);
  if (!svc) {
    const DWORD err = GetLastError();
    if (err == ERROR_SERVICE_EXISTS) {
      std::fprintf(stderr, "service already exists: %s\n", svc_name.c_str());
      CloseServiceHandle(scm);
      return 1;
    }
    std::fprintf(stderr, "error: CreateService failed (%lu)\n", err);
    CloseServiceHandle(scm);
    return 1;
  }
  std::printf("installed service %s\n", svc_name.c_str());
  std::printf("binPath: %s\n", bin_path.c_str());
  CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  return 0;
}

int EbServiceUninstall(const char* name) {
  const std::string svc_name = (name && name[0]) ? name : kDefaultServiceName;
  SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) return 1;
  SC_HANDLE svc =
      OpenServiceA(scm, svc_name.c_str(), DELETE | SERVICE_STOP | SERVICE_QUERY_STATUS);
  if (!svc) {
    CloseServiceHandle(scm);
    return 1;
  }
  SERVICE_STATUS status{};
  ControlService(svc, SERVICE_CONTROL_STOP, &status);
  const BOOL deleted = DeleteService(svc);
  CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  if (!deleted) return 1;
  std::printf("uninstalled service %s\n", svc_name.c_str());
  return 0;
}

int EbServiceStatus(const char* name) {
  const std::string svc_name = (name && name[0]) ? name : kDefaultServiceName;
  SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) return 1;
  SC_HANDLE svc = OpenServiceA(scm, svc_name.c_str(), SERVICE_QUERY_STATUS);
  if (!svc) {
    std::fprintf(stderr, "service not found: %s\n", svc_name.c_str());
    CloseServiceHandle(scm);
    return 1;
  }
  SERVICE_STATUS status{};
  if (!QueryServiceStatus(svc, &status)) {
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return 1;
  }
  const char* state = "unknown";
  switch (status.dwCurrentState) {
    case SERVICE_STOPPED:
      state = "stopped";
      break;
    case SERVICE_START_PENDING:
      state = "start_pending";
      break;
    case SERVICE_STOP_PENDING:
      state = "stop_pending";
      break;
    case SERVICE_RUNNING:
      state = "running";
      break;
    default:
      break;
  }
  std::printf("service %s: %s\n", svc_name.c_str(), state);
  CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  return 0;
}

}  // namespace ebbackup

#else

namespace ebbackup {

int EbServiceRun(const char*) {
  std::fprintf(stderr, "eb service run is Windows-only; use systemd unit on Linux\n");
  return 1;
}
int EbServiceInstall(const char*, const char*, const char*) {
  std::fprintf(stderr, "use engine_cpp/deploy/ebbackup.service on Linux\n");
  return 1;
}
int EbServiceUninstall(const char*) { return 1; }
int EbServiceStatus(const char*) { return 1; }

}  // namespace ebbackup

#endif
