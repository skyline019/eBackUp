#include "subprocess_util.h"

#include <vector>

namespace ebbackup {
namespace test {

#ifdef _WIN32
#include <windows.h>

Status RunBackupSubprocessAndKill(const std::string& repo,
                                  const std::string& source, BackupMode mode,
                                  const BackupOptions& options, int delay_ms) {
  std::string cmd =
      std::string("\"" EBTEST_EB_EXE "\" backup \"") + repo + "\" \"" + source + "\"";
  if (mode == BackupMode::kIncremental) cmd += " --incremental";
  if (options.disable_pipeline) cmd += " --no-pipeline";
  else if (options.use_pipeline) cmd += " --pipeline";
  if (options.use_encryption) cmd += " --encrypt";
  if (options.compress_mode == CompressMode::kAuto) cmd += " --compress auto";
  if (options.durability == DurabilityMode::kBalanced) cmd += " --durability balanced";
  std::vector<char> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back('\0');
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, 0,
                      nullptr, nullptr, &si, &pi)) {
    return Status::IoError("CreateProcess failed");
  }
  Sleep(static_cast<DWORD>(delay_ms));
  TerminateProcess(pi.hProcess, 1);
  WaitForSingleObject(pi.hProcess, 5000);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return Status::Ok();
}

Status RunCliSubprocessAndKill(const std::string& args, int delay_ms) {
  std::string cmd = std::string("\"" EBTEST_EB_EXE "\" ") + args;
  std::vector<char> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back('\0');
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, 0,
                      nullptr, nullptr, &si, &pi)) {
    return Status::IoError("CreateProcess failed");
  }
  Sleep(static_cast<DWORD>(delay_ms));
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

Status RunBackupSubprocessAndKill(const std::string& repo,
                                  const std::string& source, BackupMode mode,
                                  const BackupOptions& options, int delay_ms) {
  const pid_t pid = fork();
  if (pid < 0) return Status::IoError("fork failed");
  if (pid == 0) {
    BackupEngine engine(repo);
    if (!engine.Open().ok()) _exit(2);
    if (!engine.RunBackup(source, mode, options).ok()) _exit(3);
    _exit(0);
  }
  usleep(static_cast<useconds_t>(delay_ms * 1000));
  kill(pid, SIGKILL);
  int status = 0;
  waitpid(pid, &status, 0);
  return Status::Ok();
}

Status RunCliSubprocessAndKill(const std::string& args, int delay_ms) {
  const pid_t pid = fork();
  if (pid < 0) return Status::IoError("fork failed");
  if (pid == 0) {
    const std::string cmd = std::string(EBTEST_EB_EXE) + " " + args;
    execl("/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  usleep(static_cast<useconds_t>(delay_ms * 1000));
  kill(pid, SIGKILL);
  int status = 0;
  waitpid(pid, &status, 0);
  return Status::Ok();
}
#endif

}  // namespace test
}  // namespace ebbackup
