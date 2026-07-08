#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ebbackup/archive/eb_bundle.h"
#include "ebbackup/bench/throughput.h"
#include "ebbackup/chunk/eb_hcrbo.h"
#include "ebbackup/chunk/fast_cdc.h"
#include "ebbackup/common/status.h"
#include "ebbackup/daemon/backup_daemon.h"
#include "ebbackup/eb_backup.h"
#include "eb_service.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/job/job_config.h"
#include "ebbackup/plugin/plugin_registry.h"
#include "ebbackup/engine/restore_path_remap.h"
#include "ebbackup/restore/in_place_restore.h"
#include "ebbackup/scan/filter_loader.h"
#include "ebbackup/scan/exclude_suggestions.h"
#include "ebbackup/store/chunk_compactor.h"
#include "ebbackup/store/retention_policy.h"
#include "ebbackup/crypto/envelope.h"
#include "ebbackup/winmeta/vss_shadow_storage.h"

namespace {

void PrintUsage() {
  std::fprintf(stderr,
               "Usage:\n"
               "  eb init <repo> [--legacy-digest] [--legacy-init]\n"
               "      [--encrypt] [--password-env VAR] [--password-file PATH]\n"
               "      [--recovery-key-out PATH]\n"
               "  eb backup <repo> <source> [--incremental] [--progress] [--lz4]\n"
               "      [--job JOB_ID]  (with --job, source comes from jobs.json)\n"
               "      [--compress auto|lz4|zstd|off] [--compress-tier fast|balanced|max]\n"
               "      [--compress-level N] [--zstd-dict] [--no-zstd-dict] [--cpu-budget PCT]\n"
               "      [--durability strict|balanced]\n"
               "      [--pipeline] [--no-pipeline] [--encrypt] [--password-env VAR]\n"
               "      [--password-file PATH] [--filter-file PATH]\n"
               "      [--include PATH] [--exclude PATH]\n"
               "      [--include-glob GLOB] [--exclude-glob GLOB]\n"
               "      [--ext EXT] [--min-size N] [--max-size N]\n"
               "      [--mtime-after UNIX] [--mtime-before UNIX] [--uid N]\n"
               "      [--plugins ID,ID] [--pre-cmd CMD] [--post-cmd CMD]\n"
               "      [--vss] [--vss-mode crash|app|auto] [--vss-fallback-live]\n"
               "      [--vss-no-junction-volumes] [--sparse auto|off]\n"
               "      [--efs-export-keys]\n"
               "  eb verify <repo> [--require-anchor] [--at TXN] [--password-env VAR]\n"
               "      [--password-file PATH]\n"
               "  eb verify-chain <repo> [--at TXN] [--json]\n"
               "  eb rpo-summary <repo> [--json]\n"
               "  eb orphan-explain <repo> [--json] [--limit N]\n"
               "  eb audit-ops list <repo> [--json]\n"
               "  eb recover <repo>\n"
               "  eb unlock <repo> [--password-env VAR] [--password-file PATH]\n"
               "      [--recovery-key KEY]\n"
               "  eb rotate-password <repo> [--password-env VAR] [--password-file PATH]\n"
               "      --new-password PASS | --new-password-file PATH\n"
               "  eb vss status\n"
               "  eb restore <repo> <dest> [--password-env VAR] [--password-file PATH]\n"
               "      [--filter-file PATH] [--include PATH] [--exclude PATH]\n"
               "      [--include-glob GLOB] [--exclude-glob GLOB]\n"
               "      [--verify-content] [--skip-content-verify] [--at TXN]\n"
               "      [--strip-prefix PATH] [--flatten] [--remap-from PREFIX]\n"
               "      [--remap-to PREFIX] [--on-conflict fail|skip|suffix]\n"
               "      [--preview] [--in-place [--preview]]\n"
               "      [--base-at TXN] [--dry-run]\n"
               "      [--in-place-conflict skip|fail|overwrite]\n"
               "      [--in-place-orphans skip|delete]\n"
               "      [--acceptance-report PATH]\n"
               "      [--ext EXT] [--min-size N] [--max-size N]\n"
               "      [--mtime-after UNIX] [--mtime-before UNIX] [--uid N]\n"
               "  eb export <repo> <bundle.ebb> [--encrypt-bundle]\n"
               "      [--delta --base-at TXN]\n"
               "  eb import <bundle.ebb> <repo>\n"
               "  eb import <base.ebb> <delta.ebb> <repo>\n"
               "  eb import --delta <delta.ebb> <repo>\n"
               "  eb schedule <config> [--once]\n"
               "  eb service run --config PATH\n"
               "  eb service install --config PATH [--name NAME] [--display-name TEXT]\n"
               "  eb service uninstall [--name NAME]\n"
               "  eb service status [--name NAME]\n"
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
               "  eb diff <repo> --at TXN_A --at TXN_B\n"
               "  eb path-index <repo> [--rebuild]\n"
               "  eb path-history <repo> <path> [--offset N] [--limit N]\n"
               "  eb browse-page <repo> [--at TXN] [--prefix P] [--offset N] [--limit N]\n"
               "  eb prune-snapshots <repo> [--dry-run] [--retain-min N]\n"
               "      [--retention-tiers SPEC] [--audit-key KEY]\n"
               "  eb job list <repo>\n"
               "  eb job add <repo> <job.json>\n"
               "  eb job remove <repo> <job_id>\n"
               "  eb job reports <repo> <job_id> [--offset N] [--limit N]\n"
               "  eb plugin list\n"
               "  eb suggest-excludes <source> [--json] [--max-depth N] [--filter-file PATH] [--include-ide]\n"
               "  eb queue list <repo>\n"
               "  eb queue add <repo> <job_id> [--incremental]\n"
               "  eb queue run <repo> [--once|--drain] [--progress]\n"
               "  eb queue drain <repo> [--progress]\n"
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

void ApplyPluginsCli(int argc, char** argv, std::vector<std::string>* out) {
  if (!out) return;
  const char* val = GetFlagValue(argc, argv, "--plugins");
  if (!val || !val[0]) return;
  std::string item;
  for (const char* p = val; *p; ++p) {
    if (*p == ',') {
      if (!item.empty()) out->push_back(item);
      item.clear();
    } else if (*p != ' ') {
      item.push_back(*p);
    }
  }
  if (!item.empty()) out->push_back(item);
}

void ApplyVssCli(int argc, char** argv, ebbackup::BackupOptions* opts) {
  if (!opts) return;
  opts->use_vss = HasFlag(argc, argv, "--vss");
  opts->vss_fallback_live = HasFlag(argc, argv, "--vss-fallback-live");
  opts->vss_include_junction_volumes =
      !HasFlag(argc, argv, "--vss-no-junction-volumes");
  if (const char* mode = GetFlagValue(argc, argv, "--vss-mode")) {
    ebbackup::winmeta::VssConsistencyMode parsed{};
    if (ebbackup::winmeta::ParseVssConsistencyMode(mode, &parsed)) {
      opts->vss_mode = parsed;
    }
  }
  if (opts->use_vss && opts->vss_mode == ebbackup::winmeta::VssConsistencyMode::kCrash &&
      HasFlag(argc, argv, "--vss-app")) {
    opts->vss_mode = ebbackup::winmeta::VssConsistencyMode::kApp;
  }
}

void ApplySparseCli(int argc, char** argv, ebbackup::BackupOptions* opts) {
  if (!opts) return;
  if (const char* mode = GetFlagValue(argc, argv, "--sparse")) {
    if (std::strcmp(mode, "off") == 0) {
      opts->sparse_mode = ebbackup::SparseMode::kOff;
    } else {
      opts->sparse_mode = ebbackup::SparseMode::kAuto;
    }
  }
  opts->efs_export_keys = HasFlag(argc, argv, "--efs-export-keys");
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
  if (const char* tier = GetFlagValue(argc, argv, "--compress-tier")) {
    if (std::strcmp(tier, "balanced") == 0) {
      opts->compress_tier = ebbackup::CompressTier::kBalanced;
      opts->use_zstd_dict = true;
    } else if (std::strcmp(tier, "max") == 0) {
      opts->compress_tier = ebbackup::CompressTier::kMax;
      opts->use_zstd_dict = true;
    } else {
      opts->compress_tier = ebbackup::CompressTier::kFast;
    }
  }
  if (HasFlag(argc, argv, "--zstd-dict")) {
    opts->use_zstd_dict = true;
  }
  if (const char* level = GetFlagValue(argc, argv, "--compress-level")) {
    opts->compress_level = std::atoi(level);
  }
  if (HasFlag(argc, argv, "--no-zstd-dict")) {
    opts->use_zstd_dict = false;
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

bool ParseTwoAtTxns(int argc, char** argv, uint64_t* txn_a, uint64_t* txn_b) {
  if (!txn_a || !txn_b) return false;
  *txn_a = 0;
  *txn_b = 0;
  int seen = 0;
  for (int i = 1; i < argc - 1; ++i) {
    if (std::strcmp(argv[i], "--at") != 0) continue;
    const uint64_t v = std::strtoull(argv[i + 1], nullptr, 10);
    if (seen == 0) {
      *txn_a = v;
    } else if (seen == 1) {
      *txn_b = v;
      return *txn_a != 0 && *txn_b != 0;
    }
    ++seen;
  }
  return false;
}

uint64_t ParseU64Flag(int argc, char** argv, const char* flag, uint64_t def) {
  if (const char* v = GetFlagValue(argc, argv, flag)) {
    return std::strtoull(v, nullptr, 10);
  }
  return def;
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
    const ebbackup::Status init_st =
        ebbackup::BackupEngine::InitRepoEx(argv[2], opts);
    if (!init_st.ok()) return StatusExit(init_st);
    if (HasFlag(argc, argv, "--encrypt")) {
      const std::string password = ReadPassword(argc, argv);
      if (password.empty()) {
        std::fprintf(stderr, "error: --encrypt requires password\n");
        return 1;
      }
      std::string recovery_key;
      uint8_t master_key[32]{};
      const ebbackup::Status enc_st = ebbackup::crypto::CreateEnvelope(
          argv[2], password, &recovery_key, master_key);
      if (!enc_st.ok()) return StatusExit(enc_st);
      if (const char* out_path = GetFlagValue(argc, argv, "--recovery-key-out")) {
        std::ofstream out(out_path, std::ios::trunc);
        out << recovery_key << "\n";
      } else {
        std::fprintf(stderr, "recovery_key: %s\n", recovery_key.c_str());
      }
    }
    return 0;
  }

  if (cmd == "unlock") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    ebbackup::BackupEngine engine(argv[2]);
    const ebbackup::Status open_st = engine.Open();
    if (!open_st.ok()) return StatusExit(open_st);
    if (const char* rk = GetFlagValue(argc, argv, "--recovery-key")) {
      return StatusExit(engine.UnwrapWithRecoveryKey(rk));
    }
    const std::string password = ReadPassword(argc, argv);
    return StatusExit(engine.UnlockRepo(password));
  }

  if (cmd == "rotate-password") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    std::string new_password;
    if (const char* np = GetFlagValue(argc, argv, "--new-password")) {
      new_password = np;
    } else if (const char* npf = GetFlagValue(argc, argv, "--new-password-file")) {
      std::ifstream in(npf);
      std::getline(in, new_password);
    }
    if (new_password.empty()) {
      std::fprintf(stderr, "error: new password required\n");
      return 1;
    }
    ebbackup::BackupEngine engine(argv[2]);
    const ebbackup::Status open_st = engine.Open();
    if (!open_st.ok()) return StatusExit(open_st);
    const std::string old_password = ReadPassword(argc, argv);
    return StatusExit(engine.RotatePassword(old_password, new_password));
  }

#ifdef _WIN32
  if (cmd == "vss" && argc >= 3 && std::strcmp(argv[2], "status") == 0) {
    std::vector<ebbackup::winmeta::VssShadowStorageInfo> entries;
    const ebbackup::Status st = ebbackup::winmeta::QueryShadowStorage(&entries);
    if (!st.ok()) return StatusExit(st);
    std::printf("%s\n",
                ebbackup::winmeta::FormatShadowStorageStatusJson(entries).c_str());
    return 0;
  }
#endif

  if (cmd == "backup") {
    const char* job_id = GetFlagValue(argc, argv, "--job");
    if (job_id) {
      std::string repo;
      for (int i = 2; i + 2 < argc; ++i) {
        if (std::strcmp(argv[i], "--job") == 0) {
          repo = argv[i + 2];
          break;
        }
      }
      if (repo.empty()) {
        for (int i = 2; i < argc; ++i) {
          if (argv[i][0] != '-' && std::strcmp(argv[i], job_id) != 0) {
            repo = argv[i];
            break;
          }
        }
      }
      if (repo.empty()) {
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
      if (const char* pre = GetFlagValue(argc, argv, "--pre-cmd")) {
        opts.pre_backup_cmd = pre;
      }
      if (const char* post = GetFlagValue(argc, argv, "--post-cmd")) {
        opts.post_backup_cmd = post;
      }
      ApplyPluginsCli(argc, argv, &opts.plugins);
      ApplyVssCli(argc, argv, &opts);
      ApplySparseCli(argc, argv, &opts);
      const ebbackup::Status filter_st =
          ebbackup::LoadFilterFromCli(argc, argv, &opts.filter);
      if (!filter_st.ok()) return StatusExit(filter_st);

      ebbackup::BackupEngine engine(repo);
      const ebbackup::Status open_st = engine.Open();
      if (!open_st.ok()) return StatusExit(open_st);
      if (HasFlag(argc, argv, "--progress")) {
        engine.SetProgressCallback(ProgressPrinter, nullptr);
      }
      const auto mode = HasFlag(argc, argv, "--incremental")
                            ? ebbackup::BackupMode::kIncremental
                            : ebbackup::BackupMode::kFull;
      const ebbackup::Status st = engine.RunJob(job_id, mode, opts);
      if (st.ok()) PrintStats(engine.stats());
      return StatusExit(st);
    }
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
    if (const char* pre = GetFlagValue(argc, argv, "--pre-cmd")) {
      opts.pre_backup_cmd = pre;
    }
    if (const char* post = GetFlagValue(argc, argv, "--post-cmd")) {
      opts.post_backup_cmd = post;
    }
    ApplyPluginsCli(argc, argv, &opts.plugins);
    ApplyVssCli(argc, argv, &opts);
    ApplySparseCli(argc, argv, &opts);
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
    opts.verify_deep_content = HasFlag(argc, argv, "--deep");
    opts.encryption_password = ReadPassword(argc, argv);
    opts.snapshot_txn_id = ParseAtTxn(argc, argv);
    ebbackup::BackupEngine engine(argv[2]);
    const ebbackup::Status open_st = engine.Open();
    if (!open_st.ok()) return StatusExit(open_st);
    const ebbackup::Status st = engine.Verify(opts);
    if (st.ok()) std::printf("verify: OK\n");
    return StatusExit(st);
  }

  if (cmd == "verify-chain") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    const uint64_t txn = ParseAtTxn(argc, argv);
    const bool json = HasFlag(argc, argv, "--json");
    ebbackup::BackupEngine engine(argv[2]);
    const ebbackup::Status open_st = engine.Open();
    if (!open_st.ok()) return StatusExit(open_st);
    const std::string report = engine.SnapshotReachabilityJson(txn);
    if (json) {
      std::printf("%s\n", report.c_str());
    } else {
      const bool reachable = report.find("\"reachable\":true") != std::string::npos;
      const bool ok = report.find("\"ok\":true") != std::string::npos;
      if (!ok) {
        std::fprintf(stderr, "error: verify-chain failed\n");
        return 1;
      }
      std::printf("verify-chain: %s\n", reachable ? "REACHABLE" : "UNREACHABLE");
    }
    return report.find("\"ok\":true") != std::string::npos ? 0 : 1;
  }

  if (cmd == "rpo-summary") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    const bool json = HasFlag(argc, argv, "--json");
    ebbackup::BackupEngine engine(argv[2]);
    const ebbackup::Status open_st = engine.Open();
    if (!open_st.ok()) return StatusExit(open_st);
    const std::string summary = engine.RpoSummaryJson();
    if (json) {
      std::printf("%s\n", summary.c_str());
    } else {
      if (summary.find("\"ok\":true") == std::string::npos) {
        std::fprintf(stderr, "error: rpo-summary failed\n");
        return 1;
      }
      std::printf("%s\n", summary.c_str());
    }
    return summary.find("\"ok\":true") != std::string::npos ? 0 : 1;
  }

  if (cmd == "orphan-explain") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    const bool json = HasFlag(argc, argv, "--json");
    const uint64_t limit = ParseU64Flag(argc, argv, "--limit", 64);
    ebbackup::BackupEngine engine(argv[2]);
    const ebbackup::Status open_st = engine.Open();
    if (!open_st.ok()) return StatusExit(open_st);
    const std::string report = engine.OrphanExplainJson(limit);
    if (json) {
      std::printf("%s\n", report.c_str());
    } else {
      if (report.find("\"ok\":true") == std::string::npos) {
        std::fprintf(stderr, "error: orphan-explain failed\n");
        return 1;
      }
      std::printf("%s\n", report.c_str());
    }
    return report.find("\"ok\":true") != std::string::npos ? 0 : 1;
  }

  if (cmd == "audit-ops") {
    if (argc < 4 || std::string(argv[2]) != "list") {
      PrintUsage();
      return 1;
    }
    const bool json = HasFlag(argc, argv, "--json");
    ebbackup::BackupEngine engine(argv[3]);
    const ebbackup::Status open_st = engine.Open();
    if (!open_st.ok()) return StatusExit(open_st);
    const std::string list = engine.ListOpsAuditJson();
    if (json) {
      std::printf("%s\n", list.c_str());
    } else {
      if (list.find("\"ok\":true") == std::string::npos) {
        std::fprintf(stderr, "error: audit-ops list failed\n");
        return 1;
      }
      std::printf("%s\n", list.c_str());
    }
    return list.find("\"ok\":true") != std::string::npos ? 0 : 1;
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
    if (const char* strip = GetFlagValue(argc, argv, "--strip-prefix")) {
      opts.path_remap.mode = ebbackup::RestoreLayoutMode::kStripPrefix;
      opts.path_remap.strip_prefix = strip;
    } else if (HasFlag(argc, argv, "--flatten")) {
      opts.path_remap.mode = ebbackup::RestoreLayoutMode::kFlatten;
    }
    if (const char* from = GetFlagValue(argc, argv, "--remap-from")) {
      opts.path_remap.mode = ebbackup::RestoreLayoutMode::kRemapPrefix;
      opts.path_remap.map_from = from;
      if (const char* to = GetFlagValue(argc, argv, "--remap-to")) {
        opts.path_remap.map_to = to;
      }
    }
    if (const char* conflict = GetFlagValue(argc, argv, "--on-conflict")) {
      if (std::strcmp(conflict, "skip") == 0) {
        opts.path_remap.conflict = ebbackup::RestoreConflictPolicy::kSkip;
      } else if (std::strcmp(conflict, "suffix") == 0) {
        opts.path_remap.conflict = ebbackup::RestoreConflictPolicy::kSuffix;
      }
    }
    if (const char* sym_from = GetFlagValue(argc, argv, "--symlink-remap-from")) {
      opts.symlink_remap.map_from = sym_from;
      if (const char* sym_to = GetFlagValue(argc, argv, "--symlink-remap-to")) {
        opts.symlink_remap.map_to = sym_to;
      }
    }
    ebbackup::BackupEngine engine(argv[2]);
    const ebbackup::Status open_st = engine.Open();
    if (!open_st.ok()) return StatusExit(open_st);
    if (HasFlag(argc, argv, "--preview")) {
      if (HasFlag(argc, argv, "--in-place")) {
        ebbackup::restore::InPlacePreviewOptions preview_opts{};
        preview_opts.base_txn_id = ParseU64Flag(argc, argv, "--base-at", 0);
        ebbackup::restore::InPlacePreviewReport report{};
        const ebbackup::Status prev_st =
            engine.PreviewInPlaceRestore(opts.snapshot_txn_id, argv[3], opts,
                                         preview_opts, &report);
        if (!prev_st.ok()) return StatusExit(prev_st);
        std::printf("%s\n",
                    ebbackup::restore::InPlacePreviewReportToJson(report).c_str());
        return 0;
      }
      ebbackup::RestorePreviewReport preview{};
      const ebbackup::Status prev_st =
          engine.PreviewRestore(opts.snapshot_txn_id, opts, &preview);
      if (!prev_st.ok()) return StatusExit(prev_st);
      std::printf("preview: files=%llu dirs=%llu bytes=%llu\n",
                  static_cast<unsigned long long>(preview.file_count),
                  static_cast<unsigned long long>(preview.dir_count),
                  static_cast<unsigned long long>(preview.total_bytes));
      return 0;
    }
    if (HasFlag(argc, argv, "--in-place")) {
      ebbackup::restore::InPlaceApplyOptions apply_opts{};
      ebbackup::restore::InPlacePreviewOptions preview_opts{};
      preview_opts.base_txn_id = ParseU64Flag(argc, argv, "--base-at", 0);
      apply_opts.dry_run = HasFlag(argc, argv, "--dry-run");
      if (const char* conflict = GetFlagValue(argc, argv, "--in-place-conflict")) {
        if (std::strcmp(conflict, "skip") == 0) {
          apply_opts.conflict = ebbackup::restore::InPlaceConflictPolicy::kSkip;
        } else if (std::strcmp(conflict, "fail") == 0) {
          apply_opts.conflict = ebbackup::restore::InPlaceConflictPolicy::kFail;
        } else if (std::strcmp(conflict, "overwrite") == 0) {
          apply_opts.conflict =
              ebbackup::restore::InPlaceConflictPolicy::kOverwrite;
        } else {
          return StatusExit(ebbackup::Status::InvalidArgument(
              "invalid --in-place-conflict (use skip|fail|overwrite)"));
        }
      }
      if (const char* orphans = GetFlagValue(argc, argv, "--in-place-orphans")) {
        if (std::strcmp(orphans, "delete") == 0) {
          apply_opts.orphan = ebbackup::restore::InPlaceOrphanPolicy::kDelete;
        } else if (std::strcmp(orphans, "skip") != 0) {
          return StatusExit(ebbackup::Status::InvalidArgument(
              "invalid --in-place-orphans (use skip|delete)"));
        }
      }
      ebbackup::restore::InPlaceApplyReport report{};
      const ebbackup::Status apply_st = engine.ApplyInPlaceRestore(
          opts.snapshot_txn_id, argv[3], opts, preview_opts, apply_opts, &report);
      if (!apply_st.ok()) return StatusExit(apply_st);
      std::printf("%s\n",
                  ebbackup::restore::InPlaceApplyReportToJson(report).c_str());
      return 0;
    }
    const ebbackup::Status st = engine.Restore(argv[3], opts);
    if (st.ok()) {
      std::printf("restore: OK\n");
      if (const char* report_path = GetFlagValue(argc, argv, "--acceptance-report")) {
        const std::string json = engine.ExportRestoreReportJson();
        std::ofstream out(report_path, std::ios::trunc);
        if (!out) return StatusExit(ebbackup::Status::IoError("cannot write report"));
        out << json;
      }
    }
    return StatusExit(st);
  }

  if (cmd == "diff") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    uint64_t txn_a = 0;
    uint64_t txn_b = 0;
    if (!ParseTwoAtTxns(argc, argv, &txn_a, &txn_b)) {
      std::fprintf(stderr, "diff requires two --at TXN values\n");
      return 1;
    }
    ebbackup::BackupEngine engine(argv[2]);
    const ebbackup::Status open_st = engine.Open();
    if (!open_st.ok()) return StatusExit(open_st);
    const std::string json = engine.DiffSnapshotsJson(txn_a, txn_b);
    std::printf("%s\n", json.c_str());
    return 0;
  }

  if (cmd == "report") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    const uint64_t txn_id = ParseAtTxn(argc, argv);
    if (txn_id == 0) {
      std::fprintf(stderr, "report requires --at TXN\n");
      return 1;
    }
    ebbackup::BackupEngine engine(argv[2]);
    const ebbackup::Status open_st = engine.Open();
    if (!open_st.ok()) return StatusExit(open_st);
    const std::string json = engine.ExportBackupReportJson(txn_id);
    std::printf("%s\n", json.c_str());
    return 0;
  }

  if (cmd == "path-index") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    ebbackup::BackupEngine engine(argv[2]);
    const ebbackup::Status open_st = engine.Open();
    if (!open_st.ok()) return StatusExit(open_st);
    const bool rebuild = HasFlag(argc, argv, "--rebuild");
    const ebbackup::Status st = engine.BuildPathIndex(rebuild);
    if (st.ok()) std::printf("path-index: OK\n");
    return StatusExit(st);
  }

  if (cmd == "path-history") {
    if (argc < 4) {
      PrintUsage();
      return 1;
    }
    ebbackup::BackupEngine engine(argv[2]);
    const ebbackup::Status open_st = engine.Open();
    if (!open_st.ok()) return StatusExit(open_st);
    const uint64_t offset = ParseU64Flag(argc, argv, "--offset", 0);
    const uint64_t limit = ParseU64Flag(argc, argv, "--limit", 100);
    const std::string json = engine.QueryPathHistoryJson(argv[3], offset, limit);
    std::printf("%s\n", json.c_str());
    return 0;
  }

  if (cmd == "browse-page") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    ebbackup::BackupEngine engine(argv[2]);
    const ebbackup::Status open_st = engine.Open();
    if (!open_st.ok()) return StatusExit(open_st);
    const uint64_t txn = ParseAtTxn(argc, argv);
    const char* prefix = GetFlagValue(argc, argv, "--prefix");
    const uint64_t offset = ParseU64Flag(argc, argv, "--offset", 0);
    const uint64_t limit = ParseU64Flag(argc, argv, "--limit", 100);
    const std::string json =
        engine.ListManifestFilesPageJson(txn, prefix ? prefix : "", offset, limit);
    std::printf("%s\n", json.c_str());
    return 0;
  }

  if (cmd == "export") {
    if (argc < 4) {
      PrintUsage();
      return 1;
    }
    if (HasFlag(argc, argv, "--delta")) {
      ebbackup::EbBundleDeltaOptions opts{};
      opts.base_txn_id = ParseU64Flag(argc, argv, "--base-at", 0);
      if (opts.base_txn_id == 0) {
        std::fprintf(stderr, "export --delta requires --base-at TXN\n");
        return 1;
      }
      opts.encrypt_bundle = HasFlag(argc, argv, "--encrypt-bundle");
      opts.password = ReadPassword(argc, argv);
      ebbackup::EbBundleDeltaStats stats{};
      const ebbackup::Status st =
          ebbackup::ExportRepoDeltaToBundle(argv[2], argv[3], opts, &stats);
      if (!st.ok()) return StatusExit(st);
      std::printf(
          "export-delta: OK base=%llu target=%llu chunks=%llu bytes=%llu "
          "reuse_ratio=%.1f\n",
          static_cast<unsigned long long>(stats.base_txn_id),
          static_cast<unsigned long long>(stats.target_txn_id),
          static_cast<unsigned long long>(stats.delta_chunk_count),
          static_cast<unsigned long long>(stats.delta_bytes),
          stats.reuse_ratio);
      return 0;
    }
    ebbackup::EbBundleOptions opts{};
    opts.encrypt_bundle = HasFlag(argc, argv, "--encrypt-bundle");
    opts.password = ReadPassword(argc, argv);
    return StatusExit(ebbackup::ExportRepoToBundle(argv[2], argv[3], opts));
  }

  if (cmd == "import") {
    if (HasFlag(argc, argv, "--delta")) {
      if (argc < 4) {
        PrintUsage();
        return 1;
      }
      return StatusExit(
          ebbackup::ApplyDeltaBundleToRepo(argv[2], argv[3]));
    }
    if (argc == 5) {
      return StatusExit(
          ebbackup::ImportBundleDeltaToRepo(argv[2], argv[3], argv[4]));
    }
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
    return StatusExit(ebbackup::RunScheduleConfig(cfg, max_runs));
  }

  if (cmd == "service") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    const std::string sub = argv[2];
    if (sub == "run") {
      const char* cfg = GetFlagValue(argc, argv, "--config");
      if (!cfg) {
        std::fprintf(stderr, "error: service run requires --config\n");
        return 1;
      }
      return ebbackup::EbServiceRun(cfg);
    }
    if (sub == "install") {
      const char* cfg = GetFlagValue(argc, argv, "--config");
      if (!cfg) {
        std::fprintf(stderr, "error: service install requires --config\n");
        return 1;
      }
      const char* name = GetFlagValue(argc, argv, "--name");
      const char* display = GetFlagValue(argc, argv, "--display-name");
      return ebbackup::EbServiceInstall(cfg, name, display);
    }
    if (sub == "uninstall") {
      const char* name = GetFlagValue(argc, argv, "--name");
      return ebbackup::EbServiceUninstall(name);
    }
    if (sub == "status") {
      const char* name = GetFlagValue(argc, argv, "--name");
      return ebbackup::EbServiceStatus(name);
    }
    PrintUsage();
    return 1;
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
    ApplyVssCli(argc, argv, &opts);
    ApplySparseCli(argc, argv, &opts);
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

  if (cmd == "suggest-excludes") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    const std::string source = argv[2];
    const bool json_out = HasFlag(argc, argv, "--json");
    int max_depth = 4;
    if (const char* md = GetFlagValue(argc, argv, "--max-depth")) {
      max_depth = std::max(1, std::atoi(md));
    }
    ebbackup::BackupFilterOptions existing{};
    ebbackup::BackupFilterOptions* existing_ptr = nullptr;
    if (const char* filter_path = GetFlagValue(argc, argv, "--filter-file")) {
      const ebbackup::Status load_st =
          ebbackup::LoadBackupFilterFromFile(filter_path, &existing);
      if (!load_st.ok()) return StatusExit(load_st);
      existing_ptr = &existing;
    }
    ebbackup::SuggestExcludeFiltersOptions opts{};
    opts.max_depth = max_depth;
    opts.include_ide_dirs = HasFlag(argc, argv, "--include-ide");
    opts.existing = existing_ptr;
    ebbackup::ExcludeFilterSuggestions suggestions{};
    const ebbackup::Status st =
        ebbackup::SuggestExcludeFilters(source, opts, &suggestions);
    if (!st.ok()) return StatusExit(st);
    if (json_out) {
      std::printf("%s\n", ebbackup::ExcludeFilterSuggestionsToJson(suggestions).c_str());
      return 0;
    }
    for (const auto& item : suggestions.items) {
      std::printf("%s\t%s\t%s\t%s\t%u\n", item.apply_as.c_str(), item.pattern.c_str(),
                  item.kind.c_str(), item.example_path.c_str(), item.hit_count);
    }
    return 0;
  }

  if (cmd == "plugin") {
    if (argc < 3 || std::strcmp(argv[2], "list") != 0) {
      PrintUsage();
      return 1;
    }
    for (const std::string& id : ebbackup::plugin::ListBuiltinPluginIds()) {
      std::printf("%s\t%s\n", id.c_str(),
                  ebbackup::plugin::PluginDisplayName(id).c_str());
    }
    return 0;
  }

  if (cmd == "job") {
    if (argc < 4) {
      PrintUsage();
      return 1;
    }
    const std::string sub = argv[2];
    const std::string repo = argv[3];
    if (sub == "list") {
      char* json = eb_backup_list_jobs_json(repo.c_str());
      if (!json) return 1;
      std::printf("%s\n", json);
      eb_backup_free_string(json);
      return 0;
    }
    if (sub == "add") {
      if (argc < 5) {
        PrintUsage();
        return 1;
      }
      std::ifstream in(argv[4]);
      std::ostringstream ss;
      ss << in.rdbuf();
      ebbackup::job::BackupJob job{};
      const ebbackup::Status parse_st =
          ebbackup::job::ParseJobJson(ss.str(), &job);
      if (!parse_st.ok()) return StatusExit(parse_st);
      return StatusExit(ebbackup::job::UpsertJob(repo, job));
    }
    if (sub == "remove") {
      if (argc < 5) {
        PrintUsage();
        return 1;
      }
      return StatusExit(ebbackup::job::DeleteJob(repo, argv[4]));
    }
    if (sub == "reports") {
      if (argc < 5) {
        PrintUsage();
        return 1;
      }
      uint64_t offset = 0;
      uint64_t limit = 100;
      if (const char* off = GetFlagValue(argc, argv, "--offset")) {
        offset = static_cast<uint64_t>(std::strtoull(off, nullptr, 10));
      }
      if (const char* lim = GetFlagValue(argc, argv, "--limit")) {
        limit = static_cast<uint64_t>(std::strtoull(lim, nullptr, 10));
      }
      char* json = eb_backup_list_job_reports_json(repo.c_str(), argv[4], offset,
                                                   limit);
      if (!json) return 1;
      std::printf("%s\n", json);
      eb_backup_free_string(json);
      return 0;
    }
    PrintUsage();
    return 1;
  }

  if (cmd == "queue") {
    if (argc < 4) {
      PrintUsage();
      return 1;
    }
    const std::string sub = argv[2];
    const std::string repo = argv[3];
    if (sub == "list") {
      char* json = eb_backup_job_queue_status_json(repo.c_str());
      if (!json) return 1;
      std::printf("%s\n", json);
      eb_backup_free_string(json);
      return 0;
    }
    if (sub == "add") {
      if (argc < 5) {
        PrintUsage();
        return 1;
      }
      ebbackup::BackupEngine engine(repo);
      const ebbackup::Status open_st = engine.Open();
      if (!open_st.ok()) return StatusExit(open_st);
      const bool incremental = HasFlag(argc, argv, "--incremental");
      return StatusExit(engine.EnqueueJob(argv[4], incremental, 0));
    }
    if (sub == "run" || sub == "drain") {
      ebbackup::BackupEngine engine(repo);
      const ebbackup::Status open_st = engine.Open();
      if (!open_st.ok()) return StatusExit(open_st);
      if (HasFlag(argc, argv, "--progress")) {
        engine.SetProgressCallback(ProgressPrinter, nullptr);
      }
      ebbackup::BackupOptions opts{};
      opts.use_lz4 = HasFlag(argc, argv, "--lz4");
      opts.use_pipeline = HasFlag(argc, argv, "--pipeline");
      ApplyVssCli(argc, argv, &opts);
      ApplySparseCli(argc, argv, &opts);
      const bool drain = sub == "drain" || HasFlag(argc, argv, "--drain");
      const ebbackup::Status st = engine.RunJobQueue(drain, opts);
      if (st.ok()) std::printf("queue-run: OK\n");
      return StatusExit(st);
    }
    PrintUsage();
    return 1;
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
    ebbackup::BackupEngine engine(argv[2]);
    const ebbackup::Status open_st = engine.Open();
    if (!open_st.ok()) return StatusExit(open_st);
    if (const char* audit = GetFlagValue(argc, argv, "--audit-key")) {
      engine.SetAuditKey(audit);
    }
    const ebbackup::Status st =
        engine.PruneSnapshots(policy, dry_run, &report);
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
        "unique_chunks=%llu tombstoned=%llu ampl_ratio=%.3f "
        "live_uncompressed=%llu live_stored_payload=%llu compress_ratio=%.4f "
        "compressed_chunks=%llu raw_chunks=%llu zstd_dict=%s(%llu bytes)\n",
        static_cast<unsigned long long>(stats.physical_bytes),
        static_cast<unsigned long long>(stats.live_bytes),
        static_cast<unsigned long long>(stats.orphan_bytes),
        static_cast<unsigned long long>(stats.manifest_bytes),
        static_cast<unsigned long long>(stats.unique_chunks),
        static_cast<unsigned long long>(stats.tombstoned_chunks),
        stats.ampl_ratio,
        static_cast<unsigned long long>(stats.live_uncompressed_bytes),
        static_cast<unsigned long long>(stats.live_stored_payload_bytes),
        stats.compress_ratio,
        static_cast<unsigned long long>(stats.compressed_chunk_count),
        static_cast<unsigned long long>(stats.raw_chunk_count),
        stats.has_zstd_dict ? "yes" : "no",
        static_cast<unsigned long long>(stats.zstd_dict_bytes));
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
