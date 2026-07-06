#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ebbackup/archive/eb_bundle.h"
#include "ebbackup/chunk/eb_hcrbo.h"
#include "ebbackup/chunk/fast_cdc.h"
#include "ebbackup/common/status.h"
#include "ebbackup/daemon/backup_daemon.h"
#include "ebbackup/eb_backup.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/scan/filter_loader.h"

namespace {

void PrintUsage() {
  std::fprintf(stderr,
               "Usage:\n"
               "  eb init <repo>\n"
               "  eb backup <repo> <source> [--incremental] [--progress] [--lz4]\n"
               "      [--pipeline] [--encrypt] [--password-env VAR]\n"
               "      [--password-file PATH] [--filter-file PATH]\n"
               "      [--include PATH] [--exclude PATH]\n"
               "      [--include-glob GLOB] [--exclude-glob GLOB]\n"
               "      [--ext EXT] [--min-size N] [--max-size N]\n"
               "      [--mtime-after UNIX] [--mtime-before UNIX] [--uid N]\n"
               "  eb verify <repo> [--require-anchor] [--password-env VAR]\n"
               "      [--password-file PATH]\n"
               "  eb recover <repo>\n"
               "  eb restore <repo> <dest> [--password-env VAR] [--password-file PATH]\n"
               "      [--filter-file PATH] [--include PATH] [--exclude PATH]\n"
               "      [--include-glob GLOB] [--exclude-glob GLOB]\n"
               "      [--verify-content] [--skip-content-verify]\n"
               "      [--ext EXT] [--min-size N] [--max-size N]\n"
               "      [--mtime-after UNIX] [--mtime-before UNIX] [--uid N]\n"
               "  eb export <repo> <bundle.ebb> [--encrypt-bundle]\n"
               "  eb import <bundle.ebb> <repo>\n"
               "  eb schedule <config> [--once]\n"
               "  eb watch <repo> <source> [--debounce-ms N] [--once]\n"
               "      [--lz4] [--pipeline] [--encrypt] [--password-env VAR]\n"
               "      [--password-file PATH] [--filter-file PATH]\n"
               "      [--include PATH] [--exclude PATH] [--include-glob GLOB]\n"
               "      [--exclude-glob GLOB] [--ext EXT] [--min-size N] [--max-size N]\n"
               "      [--mtime-after UNIX] [--mtime-before UNIX] [--uid N]\n"
               "  eb gc-orphans <repo> [--dry-run]\n"
               "  eb bench cdc <file>\n"
               "  eb bench hcrbo <file> <delta_offset>\n");
}

void ProgressPrinter(uint64_t pct, void*) {
  std::fprintf(stderr, "\rprogress: %llu/1000", static_cast<unsigned long long>(pct));
  if (pct >= 1000) std::fprintf(stderr, "\n");
  std::fflush(stderr);
}

void PrintStats(const ebbackup::BackupStats& stats) {
  std::printf(
      "stats: files=%llu chunks_written=%llu chunks_reused=%llu "
      "cfi_reused=%llu bytes=%llu orphan_hint=%llu\n",
      static_cast<unsigned long long>(stats.files_processed),
      static_cast<unsigned long long>(stats.chunks_written),
      static_cast<unsigned long long>(stats.chunks_reused),
      static_cast<unsigned long long>(stats.chunks_reused_from_cfi),
      static_cast<unsigned long long>(stats.bytes_processed),
      static_cast<unsigned long long>(stats.orphan_chunks_hint));
}

int StatusExit(const ebbackup::Status& st) {
  if (st.ok()) return 0;
  std::fprintf(stderr, "error: %s\n", st.message().c_str());
  return 1;
}

bool HasFlag(int argc, char** argv, const char* flag) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], flag) == 0) return true;
  }
  return false;
}

const char* GetFlagValue(int argc, char** argv, const char* flag) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], flag) == 0) return argv[i + 1];
  }
  return nullptr;
}

std::string ReadPassword(int argc, char** argv) {
  if (const char* env_name = GetFlagValue(argc, argv, "--password-env")) {
    if (const char* val = std::getenv(env_name)) return val;
  }
  if (const char* file = GetFlagValue(argc, argv, "--password-file")) {
    std::ifstream in(file);
    std::string password;
    std::getline(in, password);
    return password;
  }
  return {};
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage();
    return 1;
  }

  const std::string cmd = argv[1];

  if (cmd == "init") {
    if (argc != 3) {
      PrintUsage();
      return 1;
    }
    return eb_backup_init_repo(argv[2]) == EB_OK ? 0 : 1;
  }

  if (cmd == "backup") {
    if (argc < 4) {
      PrintUsage();
      return 1;
    }
    ebbackup::BackupOptions opts{};
    opts.use_lz4 = HasFlag(argc, argv, "--lz4");
    opts.use_pipeline = HasFlag(argc, argv, "--pipeline");
    opts.use_encryption = HasFlag(argc, argv, "--encrypt");
    opts.encryption_password = ReadPassword(argc, argv);
    const ebbackup::Status filter_st = ebbackup::LoadFilterFromCli(argc, argv, &opts.filter);
    if (!filter_st.ok()) return StatusExit(filter_st);

    ebbackup::BackupEngine engine(argv[2]);
    const ebbackup::Status open_st = engine.Open();
    if (!open_st.ok()) return StatusExit(open_st);
    if (HasFlag(argc, argv, "--progress")) {
      engine.SetProgressCallback(ProgressPrinter, nullptr);
    }
    const auto mode = HasFlag(argc, argv, "--incremental")
                          ? ebbackup::BackupMode::kIncremental
                          : ebbackup::BackupMode::kFull;
    const ebbackup::Status st = engine.RunBackup(argv[3], mode, opts);
    if (st.ok()) PrintStats(engine.stats());
    return StatusExit(st);
  }

  if (cmd == "verify") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    ebbackup::BackupOptions opts{};
    opts.require_anchor = HasFlag(argc, argv, "--require-anchor");
    opts.encryption_password = ReadPassword(argc, argv);
    ebbackup::BackupEngine engine(argv[2]);
    const ebbackup::Status open_st = engine.Open();
    if (!open_st.ok()) return StatusExit(open_st);
    const ebbackup::Status st = engine.Verify(opts);
    if (st.ok()) std::printf("verify: OK\n");
    return StatusExit(st);
  }

  if (cmd == "recover") {
    if (argc != 3) {
      PrintUsage();
      return 1;
    }
    EbBackupEngine* eng = eb_backup_open(argv[2]);
    if (!eng) {
      std::fprintf(stderr, "error: cannot open repo\n");
      return 1;
    }
    const EbStatus st = eb_backup_recover(eng);
    if (st != EB_OK) {
      char* err = eb_backup_last_error(eng);
      if (err) {
        std::fprintf(stderr, "error: %s\n", err);
        eb_backup_free_string(err);
      }
    } else {
      std::printf("recover: OK\n");
    }
    eb_backup_close(eng);
    return st == EB_OK ? 0 : 1;
  }

  if (cmd == "restore") {
    if (argc < 4) {
      PrintUsage();
      return 1;
    }
    ebbackup::RestoreOptions opts{};
    opts.encryption_password = ReadPassword(argc, argv);
    const ebbackup::Status filter_st = ebbackup::LoadFilterFromCli(argc, argv, &opts.filter);
    if (!filter_st.ok()) return StatusExit(filter_st);
    if (HasFlag(argc, argv, "--skip-content-verify")) {
      opts.verify_subset_merkle = false;
      opts.verify_restored_content = false;
    } else if (HasFlag(argc, argv, "--verify-content")) {
      opts.verify_restored_content = true;
    }
    ebbackup::BackupEngine engine(argv[2]);
    const ebbackup::Status open_st = engine.Open();
    if (!open_st.ok()) return StatusExit(open_st);
    const ebbackup::Status st = engine.Restore(argv[3], opts);
    if (st.ok()) std::printf("restore: OK\n");
    return StatusExit(st);
  }

  if (cmd == "export") {
    if (argc < 4) {
      PrintUsage();
      return 1;
    }
    ebbackup::EbBundleOptions opts{};
    opts.encrypt_bundle = HasFlag(argc, argv, "--encrypt-bundle");
    opts.password = ReadPassword(argc, argv);
    return StatusExit(ebbackup::ExportRepoToBundle(argv[2], argv[3], opts));
  }

  if (cmd == "import") {
    if (argc != 4) {
      PrintUsage();
      return 1;
    }
    return StatusExit(ebbackup::ImportBundleToRepo(argv[2], argv[3]));
  }

  if (cmd == "schedule") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    ebbackup::ScheduleConfig cfg{};
    const ebbackup::Status load_st = ebbackup::LoadScheduleConfigAuto(argv[2], &cfg);
    if (!load_st.ok()) return StatusExit(load_st);
    const int max_runs = HasFlag(argc, argv, "--once") ? 1 : -1;
    return StatusExit(ebbackup::RunScheduledBackup(cfg, max_runs));
  }

  if (cmd == "watch") {
    if (argc < 4) {
      PrintUsage();
      return 1;
    }
    ebbackup::BackupOptions opts{};
    opts.use_lz4 = HasFlag(argc, argv, "--lz4");
    opts.use_pipeline = HasFlag(argc, argv, "--pipeline");
    opts.use_encryption = HasFlag(argc, argv, "--encrypt");
    opts.encryption_password = ReadPassword(argc, argv);
    const ebbackup::Status filter_st = ebbackup::LoadFilterFromCli(argc, argv, &opts.filter);
    if (!filter_st.ok()) return StatusExit(filter_st);
    int debounce_ms = 2000;
    if (const char* val = GetFlagValue(argc, argv, "--debounce-ms")) {
      debounce_ms = std::max(0, std::atoi(val));
    }
    const int max_triggers = HasFlag(argc, argv, "--once") ? 1 : -1;
    return StatusExit(
        ebbackup::RunWatchBackup(argv[3], argv[2], opts, debounce_ms, max_triggers));
  }

  if (cmd == "gc-orphans") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    const bool dry_run = HasFlag(argc, argv, "--dry-run");
    EbBackupEngine* eng = eb_backup_open(argv[2]);
    if (!eng) {
      std::fprintf(stderr, "error: cannot open repo\n");
      return 1;
    }
    const EbStatus st = eb_backup_gc_orphans(eng, dry_run ? 1 : 0);
    if (st != EB_OK) {
      char* err = eb_backup_last_error(eng);
      if (err) {
        std::fprintf(stderr, "error: %s\n", err);
        eb_backup_free_string(err);
      }
    } else {
      std::printf("gc-orphans: OK (%s)\n", dry_run ? "dry-run" : "apply");
    }
    eb_backup_close(eng);
    return st == EB_OK ? 0 : 1;
  }

  if (cmd == "bench" && argc >= 4 && std::strcmp(argv[2], "cdc") == 0) {
    const std::string file = argv[3];
    std::ifstream in(file, std::ios::binary);
    if (!in) {
      std::fprintf(stderr, "error: cannot open %s\n", file.c_str());
      return 1;
    }
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    ebbackup::FastCdcSlice chunker;
    std::vector<ebbackup::ChunkDescriptor> chunks;
    const auto t0 = std::chrono::steady_clock::now();
    const ebbackup::Status st = chunker.Chunk(data.data(), data.size(), &chunks);
    const auto t1 = std::chrono::steady_clock::now();
    if (!st.ok()) return StatusExit(st);
    const double sec = std::chrono::duration<double>(t1 - t0).count();
    const double mb = static_cast<double>(data.size()) / (1024.0 * 1024.0);
    uint64_t total_len = 0;
    for (const auto& c : chunks) total_len += c.length;
    const double avg =
        chunks.empty() ? 0.0 : static_cast<double>(total_len) / chunks.size();
    std::printf(
        "bench cdc: file_bytes=%zu chunks=%zu avg_chunk=%.0f throughput=%.2f "
        "MB/s\n",
        data.size(), chunks.size(), avg, sec > 0 ? mb / sec : 0.0);
    return 0;
  }

  if (cmd == "bench" && argc >= 5 && std::strcmp(argv[2], "hcrbo") == 0) {
    const std::string file = argv[3];
    const size_t delta_offset = static_cast<size_t>(std::stoull(argv[4]));
    std::ifstream in(file, std::ios::binary);
    if (!in) {
      std::fprintf(stderr, "error: cannot open %s\n", file.c_str());
      return 1;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    if (delta_offset < data.size()) data[delta_offset] ^= 0x5A;

    ebbackup::EbHcrboChunker chunker;
    ebbackup::CfiIndex cfi;
    std::vector<ebbackup::ChunkDescriptor> full;
    (void)chunker.ChunkFull(data.data(), data.size(), &full, &cfi, nullptr);

    const auto t0 = std::chrono::steady_clock::now();
    std::vector<ebbackup::ChunkDescriptor> incr;
    ebbackup::CfiIndex cfi_out;
    ebbackup::EbHcrboStats stats{};
    const ebbackup::Status st = chunker.ChunkIncremental(
        data.data(), data.size(), cfi, &incr, &cfi_out, &stats);
    const auto t1 = std::chrono::steady_clock::now();
    if (!st.ok()) return StatusExit(st);
    const double sec = std::chrono::duration<double>(t1 - t0).count();
    const double mb = static_cast<double>(data.size()) / (1024.0 * 1024.0);
    const double reuse =
        full.empty() ? 0.0
                     : 100.0 * stats.chunks_reused_from_cfi / full.size();
    std::printf(
        "bench hcrbo: file_bytes=%zu chunks=%zu reused=%.1f%% throughput=%.2f "
        "MB/s rolling_skip_hits=%llu indexed_scan=true\n",
        data.size(), incr.size(), reuse, sec > 0 ? mb / sec : 0.0,
        static_cast<unsigned long long>(stats.cfi_rolling_skip_hits));
    return 0;
  }

  PrintUsage();
  return 1;
}
