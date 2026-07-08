#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include "ebbackup/common/status.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/report/backup_report.h"

namespace {

void PrintUsage() {
  std::fprintf(stderr,
               "ebrecover — minimal disaster-recovery runtime\n"
               "Usage:\n"
               "  ebrecover list <repo> [--at TXN]\n"
               "  ebrecover browse <repo> [--at TXN]\n"
               "  ebrecover restore <repo> <dest> [--at TXN]\n"
               "      [--password-env VAR] [--password-file PATH]\n"
               "      [--recovery-key KEY]\n"
               "  ebrecover verify <repo> [--at TXN]\n"
               "      [--password-env VAR] [--password-file PATH]\n"
               "      [--recovery-key KEY]\n");
}

int StatusExit(const ebbackup::Status& st) {
  if (st.ok()) return 0;
  std::fprintf(stderr, "error: %s\n", st.message().c_str());
  return 1;
}

uint64_t ParseAt(int argc, char** argv) {
  for (int i = 1; i < argc - 1; ++i) {
    if (std::strcmp(argv[i], "--at") == 0) {
      return std::strtoull(argv[i + 1], nullptr, 10);
    }
  }
  return 0;
}

std::string ReadPassword(int argc, char** argv) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--password-env") == 0) {
      if (const char* val = std::getenv(argv[i + 1])) return val;
    }
  }
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--password-file") == 0) {
      std::ifstream in(argv[i + 1]);
      std::string password;
      std::getline(in, password);
      return password;
    }
  }
  return {};
}

const char* RecoveryKeyArg(int argc, char** argv) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--recovery-key") == 0) return argv[i + 1];
  }
  return nullptr;
}

void ProgressPrinter(uint64_t pct, void*) {
  std::fprintf(stderr, "\rrestore: %llu/1000",
               static_cast<unsigned long long>(pct));
  if (pct >= 1000) std::fprintf(stderr, "\n");
  std::fflush(stderr);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    PrintUsage();
    return 1;
  }
  const std::string cmd = argv[1];
  const std::string repo = argv[2];
  ebbackup::BackupEngine engine(repo);
  const ebbackup::Status open_st = engine.Open();
  if (!open_st.ok()) return StatusExit(open_st);

  if (const char* rk = RecoveryKeyArg(argc, argv)) {
    const ebbackup::Status st = engine.UnwrapWithRecoveryKey(rk);
    if (!st.ok()) return StatusExit(st);
  } else {
    const std::string password = ReadPassword(argc, argv);
    if (!password.empty()) {
      const ebbackup::Status st = engine.UnlockRepo(password);
      if (!st.ok()) return StatusExit(st);
    }
  }

  if (cmd == "list") {
    const uint64_t txn = ParseAt(argc, argv);
    const std::string page =
        engine.ListManifestFilesPageJson(txn, "", 0, 20);
    std::printf("%s\n", page.c_str());
    if (txn != 0) {
      ebbackup::report::BackupReport br{};
      if (ebbackup::report::LoadBackupReport(repo, txn, &br).ok()) {
        std::printf("txn=%llu backed_up=%llu skipped=%llu\n",
                    static_cast<unsigned long long>(br.txn_id),
                    static_cast<unsigned long long>(br.backed_up),
                    static_cast<unsigned long long>(br.skipped));
      }
    }
    return 0;
  }
  if (cmd == "browse") {
    const uint64_t txn = ParseAt(argc, argv);
    const std::string json = engine.ListManifestFilesPageJson(txn, "", 0, 50);
    std::printf("%s\n", json.c_str());
    return 0;
  }
  if (cmd == "restore") {
    if (argc < 4) {
      PrintUsage();
      return 1;
    }
    ebbackup::RestoreOptions opts{};
    opts.snapshot_txn_id = ParseAt(argc, argv);
    opts.encryption_password = ReadPassword(argc, argv);
    if (const char* rk = RecoveryKeyArg(argc, argv)) opts.recovery_key = rk;
    engine.SetProgressCallback(ProgressPrinter, nullptr);
    return StatusExit(engine.Restore(argv[3], opts));
  }
  if (cmd == "verify") {
    ebbackup::BackupOptions opts{};
    opts.snapshot_txn_id = ParseAt(argc, argv);
    opts.encryption_password = ReadPassword(argc, argv);
    return StatusExit(engine.Verify(opts));
  }
  PrintUsage();
  return 1;
}
