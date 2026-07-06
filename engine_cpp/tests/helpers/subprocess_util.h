#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"
#include "ebbackup/engine/backup_engine.h"

namespace ebbackup {
namespace test {

#ifdef _WIN32
#include <windows.h>

inline Status RunBackupSubprocessAndKill(const std::string& repo,
                                         const std::string& source,
                                         uint32_t flags) {
  (void)flags;
  std::string cmd =
      std::string("\"" EBTEST_EB_EXE "\" backup \"") + repo + "\" \"" + source +
      "\"";
  std::vector<char> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back('\0');
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, 0,
                      nullptr, nullptr, &si, &pi)) {
    return Status::IoError("CreateProcess failed");
  }
  Sleep(50);
  TerminateProcess(pi.hProcess, 1);
  WaitForSingleObject(pi.hProcess, 5000);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return Status::Ok();
}

#else
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>

inline Status RunBackupSubprocessAndKill(const std::string& repo,
                                         const std::string& source,
                                         uint32_t flags) {
  (void)flags;
  const pid_t pid = fork();
  if (pid < 0) return Status::IoError("fork failed");
  if (pid == 0) {
    BackupEngine engine(repo);
    if (!engine.Open().ok()) _exit(2);
    if (!engine.RunBackup(source).ok()) _exit(3);
    _exit(0);
  }
  usleep(50000);
  kill(pid, SIGKILL);
  int status = 0;
  waitpid(pid, &status, 0);
  return Status::Ok();
}
#endif

}  // namespace test
}  // namespace ebbackup
