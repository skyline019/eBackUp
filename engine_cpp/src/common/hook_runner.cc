#include "ebbackup/common/hook_runner.h"

#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace ebbackup {

Status RunShellCommand(const std::string& command, int* exit_code) {
  if (command.empty()) return Status::Ok();
  if (!exit_code) return Status::InvalidArgument("exit_code is null");
  *exit_code = 0;

#ifdef _WIN32
  std::string cmdline = "cmd.exe /c " + command;
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::vector<char> buf(cmdline.begin(), cmdline.end());
  buf.push_back('\0');
  if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      nullptr, &si, &pi)) {
    return Status::IoError("CreateProcess failed for hook");
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  *exit_code = static_cast<int>(code);
  return Status::Ok();
#else
  const int rc = std::system(command.c_str());
  if (rc == -1) return Status::IoError("system() failed for hook");
  *exit_code = WIFEXITED(rc) ? WEXITSTATUS(rc) : rc;
  return Status::Ok();
#endif
}

}  // namespace ebbackup
