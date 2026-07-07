#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ebbackup/archive/eb_bundle.h"
#include "ebbackup/bench/throughput.h"
#include "ebbackup/chunk/eb_hcrbo.h"
#include "ebbackup/chunk/fast_cdc.h"
#include "ebbackup/common/status.h"
#include "ebbackup/daemon/backup_daemon.h"
#include "ebbackup/eb_backup.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/scan/filter_loader.h"
#include "ebbackup/store/chunk_compactor.h"
#include "ebbackup/store/retention_policy.h"
#include "ebbackup/store/snapshot_store.h"

namespace {

void PrintUsage() {
  std::fprintf(stderr,
               "Usage:\n"
               "  eb init <repo> [--legacy-digest] [--legacy-init]\n"
               "  eb backup <repo> <source> [--incremental] [--progress] [--lz4]\n"
               "      [--compress auto|lz4|zstd|off] [--cpu-budget PCT]\n"
               "      [--durability strict|balanced]\n"
               "      [--pipeline] [--no-pipeline] [--encrypt] [--password-env VAR]\n"
               "      [--password-file PATH] [--filter-file PATH]\n"
               "      [--include PATH] [--exclude PATH]\n"
               "      [--include-glob GLOB] [--exclude-glob GLOB]\n"
               "      [--ext EXT] [--min-size N] [--max-size N]\n"
               "      [--mtime-after UNIX] [--mtime-before UNIX] [--uid N]\n"
               "  eb verify <repo> [--require-anchor] [--at TXN] [--password-env VAR]\n"
               "      [--password-file PATH]\n"
               "  eb recover <repo>\n"
               "  eb restore <repo> <dest> [--password-env VAR] [--password-file PATH]\n"
               "      [--filter-file PATH] [--include PATH] [--exclude PATH]\n"
               "      [--include-glob GLOB] [--exclude-glob GLOB]\n"
               "      [--verify-content] [--skip-content-verify] [--at TXN]\n"
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
               "  eb gc-orphans <repo> [--dry-run] [--latest-only]\n"
               "  eb compact <repo> [--dry-run] [--wait-idle SEC]\n"
               "  eb repo-stats <repo>\n"
               "  eb list-snapshots <repo> [--json]\n"
               "  eb prune-snapshots <repo> [--dry-run] [--retain-min N]\n"
               "      [--retention-tiers SPEC]\n"
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

void ApplyCompressCli(int argc, char** argv, ebbackup::BackupOptions* opts) {
  if (!opts) return;
  if (const char* val = GetFlagValue(argc, argv, "--compress")) {
    if (std::strcmp(val, "auto") == 0) {
      opts->compress_mode = ebbackup::CompressMode::kAuto;
      if (opts->cpu_budget_permille == 1000) opts->cpu_budget_permille = 600;
    } else if (std::strcmp(val, "lz4") == 0) {
      opts->compress_mode = ebbackup::CompressMode::kLz4;
      opts->use_lz4 = true;
    } else if (std::strcmp(val, "zstd") == 0) {
      opts->compress_mode = ebbackup::CompressMode::kZstd;
    } else if (std::strcmp(val, "off") == 0) {
      opts->compress_mode = ebbackup::CompressMode::kOff;
    }
  }
  if (const char* budget = GetFlagValue(argc, argv, "--cpu-budget")) {
    const int pct = std::atoi(budget);
    if (pct > 0) opts->cpu_budget_permille = static_cast<uint32_t>(pct * 10);
  }
  if (const char* dur = GetFlagValue(argc, argv, "--durability")) {
    if (std::strcmp(dur, "balanced") == 0) {
      opts->durability = ebbackup::DurabilityMode::kBalanced;
    } else {
      opts->durability = ebbackup::DurabilityMode::kStrict;
    }
  }
}

uint64_t ParseAtTxn(int argc, char** argv) {
  if (const char* at = GetFlagValue(argc, argv, "--at")) {
    if (std::strcmp(at, "latest") == 0) return 0;
    return std::strtoull(at, nullptr, 10);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage();
    return 1;
  }

  const std::string cmd = argv[1];

  if (cmd == "init") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    ebbackup::RepoInitOptions opts{};
    opts.standard_digest = !HasFlag(argc, argv, "--legacy-digest");
    if (!HasFlag(argc, argv, "--legacy-init")) {
      opts.persistent_index = true;
      opts.manifest_binary = true;
      opts.snapshots = true;
      opts.ebpack = true;
      opts.coalesced_meta = true;
    }
    return StatusExit(ebbackup::BackupEngine::InitRepoEx(argv[2], opts));
  }

  if (cmd == "backup") {
    if (argc < 4) {
      PrintUsage();
      return 1;
    }
    ebbackup::BackupOptions opts{};
    opts.use_lz4 = HasFlag(argc, argv, "--lz4");
    opts.use_pipeline = HasFlag(argc, argv, "--pipeline");
    opts.disable_pipeline = HasFlag(argc, argv, "--no-pipeline");
    opts.use_encryption = HasFlag(argc, argv, "--encrypt");
    opts.encryption_password = ReadPassword(argc, argv);
    ApplyCompressCli(argc, argv, &opts);
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
    opts.snapshot_txn_id = ParseAtTxn(argc, argv);
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
    opts.snapshot_txn_id = ParseAtTxn(argc, argv);
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
    opts.disable_pipeline = HasFlag(argc, argv, "--no-pipeline");
    opts.use_encryption = HasFlag(argc, argv, "--encrypt");
    opts.encryption_password = ReadPassword(argc, argv);
    ApplyCompressCli(argc, argv, &opts);
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

  if (cmd == "list-snapshots") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    ebbackup::BackupEngine engine(argv[2]);
    const ebbackup::Status open_st = engine.Open();
    if (!open_st.ok()) return StatusExit(open_st);
    std::vector<ebbackup::SnapshotEntry> snaps;
    const ebbackup::Status st = engine.ListSnapshots(&snaps);
    if (!st.ok()) return StatusExit(st);
    const ebbackup::RetentionPolicy policy = ebbackup::DefaultRetentionPolicy();
    const bool json = HasFlag(argc, argv, "--json");
    if (json) std::printf("[\n");
    for (size_t i = 0; i < snaps.size(); ++i) {
      const auto& s = snaps[i];
      const bool kept = ebbackup::IsKeptByPolicy(snaps, policy, s.txn_id);
      if (json) {
        std::printf(
            "  {\"txn_id\":%llu,\"created_at\":%lld,\"files\":%u,"
            "\"crc32\":\"%08x\",\"kept_by_policy\":%s}%s\n",
            static_cast<unsigned long long>(s.txn_id),
            static_cast<long long>(s.created_at_unix), s.file_count,
            static_cast<unsigned>(s.manifest_crc32), kept ? "true" : "false",
            i + 1 < snaps.size() ? "," : "");
      } else {
        std::printf(
            "snapshot txn=%llu time=%lld files=%u crc=%08x kept=%s\n",
            static_cast<unsigned long long>(s.txn_id),
            static_cast<long long>(s.created_at_unix), s.file_count,
            static_cast<unsigned>(s.manifest_crc32), kept ? "yes" : "no");
      }
    }
    if (json) std::printf("]\n");
    return 0;
  }

  if (cmd == "prune-snapshots") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    ebbackup::RetentionPolicy policy = ebbackup::DefaultRetentionPolicy();
    if (const char* tiers = GetFlagValue(argc, argv, "--retention-tiers")) {
      const ebbackup::Status parse_st =
          ebbackup::ParseRetentionTiers(tiers, &policy);
      if (!parse_st.ok()) return StatusExit(parse_st);
    }
    if (const char* min_val = GetFlagValue(argc, argv, "--retain-min")) {
      policy.retain_min = std::max(1, std::atoi(min_val));
    }
    const bool dry_run = HasFlag(argc, argv, "--dry-run");
    ebbackup::PruneReport report{};
    const ebbackup::Status st =
        ebbackup::PruneSnapshots(argv[2], policy, dry_run, &report);
    if (!st.ok()) return StatusExit(st);
    std::printf("prune-snapshots: kept=%llu pruned=%llu (%s)\n",
                static_cast<unsigned long long>(report.kept_count),
                static_cast<unsigned long long>(report.pruned_count),
                dry_run ? "dry-run" : "apply");
    return 0;
  }

  if (cmd == "gc-orphans") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    const bool dry_run = HasFlag(argc, argv, "--dry-run");
    const bool latest_only = HasFlag(argc, argv, "--latest-only");
    ebbackup::BackupEngine engine(argv[2]);
    const ebbackup::Status open_st = engine.Open();
    if (!open_st.ok()) return StatusExit(open_st);
    ebbackup::OrphanGcReport report{};
    const ebbackup::Status st =
        engine.GcOrphans(dry_run, &report, latest_only);
    if (!st.ok()) return StatusExit(st);
    std::printf("gc-orphans: OK (%s) referenced=%llu orphan=%llu\n",
                dry_run ? "dry-run" : "apply",
                static_cast<unsigned long long>(report.referenced_count),
                static_cast<unsigned long long>(report.orphan_count));
    return 0;
  }

  if (cmd == "compact") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    const bool dry_run = HasFlag(argc, argv, "--dry-run");
    if (const char* wait_val = GetFlagValue(argc, argv, "--wait-idle")) {
      const int wait_sec = std::atoi(wait_val);
      const ebbackup::Status wait_st =
          ebbackup::WaitForRepoIdle(argv[2], wait_sec);
      if (!wait_st.ok()) return StatusExit(wait_st);
    }
    ebbackup::CompactReport report{};
    const ebbackup::Status st =
        ebbackup::CompactChunkStore(argv[2], dry_run, &report);
    if (!st.ok()) return StatusExit(st);
    std::printf(
        "compact: before=%llu after=%llu live=%llu records=%llu "
        "ampl_before=%.3f ampl_after=%.3f (%s)\n",
        static_cast<unsigned long long>(report.physical_before),
        static_cast<unsigned long long>(report.physical_after),
        static_cast<unsigned long long>(report.live_bytes),
        static_cast<unsigned long long>(report.records_copied),
        report.ampl_ratio_before, report.ampl_ratio_after,
        dry_run ? "dry-run" : "apply");
    return 0;
  }

  if (cmd == "repo-stats") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    ebbackup::RepoStats stats{};
    const ebbackup::Status st = ebbackup::ComputeRepoStats(argv[2], &stats);
    if (!st.ok()) return StatusExit(st);
    std::printf(
        "repo-stats: physical=%llu live=%llu orphan=%llu manifest=%llu "
        "unique_chunks=%llu tombstoned=%llu ampl_ratio=%.3f\n",
        static_cast<unsigned long long>(stats.physical_bytes),
        static_cast<unsigned long long>(stats.live_bytes),
        static_cast<unsigned long long>(stats.orphan_bytes),
        static_cast<unsigned long long>(stats.manifest_bytes),
        static_cast<unsigned long long>(stats.unique_chunks),
        static_cast<unsigned long long>(stats.tombstoned_chunks),
        stats.ampl_ratio);
    return 0;
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
    const uint64_t nbytes = static_cast<uint64_t>(data.size());
    const double throughput_MBps =
        ebbackup::bench::ThroughputMBps(nbytes, sec);
    const double throughput_MiBps =
        ebbackup::bench::ThroughputMiBps(nbytes, sec);
    uint64_t total_len = 0;
    for (const auto& c : chunks) total_len += c.length;
    const double avg =
        chunks.empty() ? 0.0 : static_cast<double>(total_len) / chunks.size();
    std::printf(
        "bench cdc: file_bytes=%zu chunks=%zu avg_chunk=%.0f throughput=%.2f "
        "MB/s (MiBps=%.2f)\n",
        data.size(), chunks.size(), avg, throughput_MBps, throughput_MiBps);
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
    const uint64_t nbytes = static_cast<uint64_t>(data.size());
    const double throughput_MBps =
        ebbackup::bench::ThroughputMBps(nbytes, sec);
    const double throughput_MiBps =
        ebbackup::bench::ThroughputMiBps(nbytes, sec);
    const double reuse =
        full.empty() ? 0.0
                     : 100.0 * stats.chunks_reused_from_cfi / full.size();
    std::printf(
        "bench hcrbo: file_bytes=%zu chunks=%zu reused=%.1f%% throughput=%.2f "
        "MB/s (MiBps=%.2f) rolling_skip_hits=%llu indexed_scan=true\n",
        data.size(), incr.size(), reuse, throughput_MBps, throughput_MiBps,
        static_cast<unsigned long long>(stats.cfi_rolling_skip_hits));
    return 0;
  }

  PrintUsage();
  return 1;
}
