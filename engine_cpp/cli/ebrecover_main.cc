#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "ebbackup/common/status.h"
#include "ebbackup/engine/backup_engine.h"

namespace {

void PrintUsage() {
  std::fprintf(stderr,
               "ebrecover — minimal disaster-recovery runtime\n"
               "Usage:\n"
               "  ebrecover browse <repo> [--at TXN]\n"
               "  ebrecover restore <repo> <dest> [--at TXN]\n"
               "  ebrecover verify <repo> [--at TXN]\n");
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
    return StatusExit(engine.Restore(argv[3], opts));
  }
  if (cmd == "verify") {
    ebbackup::BackupOptions opts{};
    opts.snapshot_txn_id = ParseAt(argc, argv);
    return StatusExit(engine.Verify(opts));
  }
  PrintUsage();
  return 1;
}
