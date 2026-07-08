#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

#include <filesystem>

#include "ebsync/pds/pds_client.h"
#include "ebsync/sync_agent.h"
#include "ebsync/sync_state.h"

namespace {

void PrintUsage() {
  std::fprintf(stderr,
               "eb-sync — outer cloud sync (non-kernel)\n"
               "  eb-sync init --repo PATH [--local-mirror DIR | --mode ferry | --pds]\n"
               "                 [--endpoint URL] [--bucket B] [--prefix P]\n"
               "                 [--access-key AK] [--secret-key SK] [--path-style]\n"
               "                 [--domain DOMAIN] [--credentials CSV] [--pds-prefix P]\n"
               "  eb-sync pds auth-url --repo PATH\n"
               "  eb-sync pds auth --repo PATH --code CODE\n"
               "  eb-sync pds setup-drive --repo PATH\n"
               "  eb-sync status --repo PATH [--json]\n"
               "  eb-sync plan --repo PATH [--base-at TXN] [--json]\n"
               "  eb-sync push --repo PATH [--once] [--drain]\n"
               "  eb-sync ferry export --repo PATH --out-dir DIR [--auto-base]\n"
               "                       [--base-at TXN] [--target-at TXN] [--also-mirror]\n"
               "  eb-sync ferry import --base PATH --delta PATH --dest-repo PATH\n"
               "  eb-sync verify-remote --repo PATH [--at TXN] [--json]\n"
               "  eb-sync pull --repo PATH --dest PATH [--at TXN]\n"
               "  eb-sync maintenance-check --repo PATH [--json]\n"
               "\n"
               "Env: EBSYNC_S3_* / EBSYNC_PDS_* overrides sync.json;\n"
               "     EBSYNC_LOCAL_ROOT for dir transport\n");
}

std::string ArgValue(int argc, char** argv, const char* flag) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], flag) == 0) return argv[i + 1];
  }
  return {};
}

bool HasFlag(int argc, char** argv, const char* flag) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], flag) == 0) return true;
  }
  return false;
}

uint64_t ArgU64(int argc, char** argv, const char* flag, uint64_t def = 0) {
  const std::string v = ArgValue(argc, argv, flag);
  if (v.empty()) return def;
  try {
    return std::stoull(v);
  } catch (...) {
    return def;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage();
    return 2;
  }

  const std::string cmd = argv[1];
  const std::string repo = ArgValue(argc, argv, "--repo");

  if (cmd == "init") {
    if (repo.empty()) {
      std::fprintf(stderr, "init requires --repo\n");
      return 2;
    }
    const std::string local_mirror = ArgValue(argc, argv, "--local-mirror");
    const std::string mode = ArgValue(argc, argv, "--mode");
    if (!local_mirror.empty()) {
      if (!ebsync::InitSyncRepoLocal(repo, local_mirror)) {
        std::fprintf(stderr, "init failed\n");
        return 1;
      }
      std::printf("sync init OK (local_mirror): %s -> %s\n", repo.c_str(),
                  local_mirror.c_str());
      return 0;
    }
    if (mode == "ferry") {
      if (!ebsync::InitSyncRepoFerry(repo)) {
        std::fprintf(stderr, "init failed\n");
        return 1;
      }
      std::printf("sync init OK (ferry): %s\n", repo.c_str());
      return 0;
    }
    if (HasFlag(argc, argv, "--pds") || mode == "pds") {
      const std::string domain = ArgValue(argc, argv, "--domain");
      const std::string credentials = ArgValue(argc, argv, "--credentials");
      if (domain.empty() || credentials.empty()) {
        std::fprintf(stderr, "pds init requires --domain and --credentials CSV\n");
        return 2;
      }
      ebsync::PdsRemoteConfig pds;
      pds.domain_id = domain;
      if (!ebsync::ImportPdsCredentialsCsv(credentials, &pds)) {
        std::fprintf(stderr, "cannot read credentials CSV\n");
        return 1;
      }
      pds.root_prefix = ArgValue(argc, argv, "--pds-prefix");
      const std::string repo_label =
          std::filesystem::path(repo).filename().string();
      if (!ebsync::InitSyncRepoPds(repo, pds, repo_label)) {
        std::fprintf(stderr, "init failed\n");
        return 1;
      }
      std::printf("sync init OK (pds): %s domain=%s prefix=%s\n", repo.c_str(),
                  domain.c_str(), pds.root_prefix.c_str());
      std::printf("next: eb-sync pds auth-url --repo %s\n", repo.c_str());
      return 0;
    }
    ebsync::SyncState cfg;
    cfg.remote_type = "s3";
    cfg.s3.endpoint = ArgValue(argc, argv, "--endpoint");
    cfg.s3.bucket = ArgValue(argc, argv, "--bucket");
    cfg.s3.prefix = ArgValue(argc, argv, "--prefix");
    cfg.s3.region = ArgValue(argc, argv, "--region");
    if (cfg.s3.region.empty()) cfg.s3.region = "us-east-1";
    cfg.s3.access_key = ArgValue(argc, argv, "--access-key");
    cfg.s3.secret_key = ArgValue(argc, argv, "--secret-key");
    cfg.s3.path_style = HasFlag(argc, argv, "--path-style");
    if (!ebsync::InitSyncRepo(repo, cfg)) {
      std::fprintf(stderr, "init failed\n");
      return 1;
    }
    std::printf("sync init OK (s3): %s\n", repo.c_str());
    return 0;
  }

  if (cmd == "status") {
    if (repo.empty()) {
      std::fprintf(stderr, "status requires --repo\n");
      return 2;
    }
    const std::string json = ebsync::BuildStatusJson(repo);
    std::cout << json;
    return 0;
  }

  if (cmd == "plan") {
    if (repo.empty()) {
      std::fprintf(stderr, "plan requires --repo\n");
      return 2;
    }
    std::string json;
    const uint64_t base = ArgU64(argc, argv, "--base-at", 0);
    if (!ebsync::BuildPlanJson(repo, base, &json)) {
      std::fprintf(stderr, "plan failed\n");
      return 1;
    }
    std::cout << json;
    return 0;
  }

  if (cmd == "push") {
    if (repo.empty()) {
      std::fprintf(stderr, "push requires --repo\n");
      return 2;
    }
    ebsync::PushOptions opts;
    opts.once = HasFlag(argc, argv, "--once");
    opts.drain = HasFlag(argc, argv, "--drain");
    if (opts.drain) opts.once = false;
    const ebsync::PushReport rep = ebsync::PushSync(repo, opts);
    if (!rep.ok) {
      std::fprintf(stderr, "push failed: %s\n", rep.error.c_str());
      return 1;
    }
    std::printf("push OK synced_txn=%llu chunks=%llu\n",
                static_cast<unsigned long long>(rep.synced_txn),
                static_cast<unsigned long long>(rep.chunks_uploaded));
    return 0;
  }

  if (cmd == "ferry" && argc >= 3 && std::string(argv[2]) == "export") {
    if (repo.empty()) {
      std::fprintf(stderr, "ferry export requires --repo\n");
      return 2;
    }
    const std::string out_dir = ArgValue(argc, argv, "--out-dir");
    if (out_dir.empty()) {
      std::fprintf(stderr, "ferry export requires --out-dir\n");
      return 2;
    }
    ebsync::FerryExportOptions opts;
    opts.auto_base = HasFlag(argc, argv, "--auto-base");
    opts.base_txn = ArgU64(argc, argv, "--base-at", 0);
    opts.target_txn = ArgU64(argc, argv, "--target-at", 0);
    opts.also_mirror = HasFlag(argc, argv, "--also-mirror");
    std::string summary;
    const bool ok = ebsync::FerryExport(repo, out_dir, opts, &summary);
    if (!ok) {
      std::fprintf(stderr, "ferry export failed: %s\n", summary.c_str());
      return 1;
    }
    std::printf("%s\n", summary.c_str());
    return 0;
  }

  if (cmd == "ferry" && argc >= 3 && std::string(argv[2]) == "import") {
    const std::string base = ArgValue(argc, argv, "--base");
    const std::string delta = ArgValue(argc, argv, "--delta");
    const std::string dest = ArgValue(argc, argv, "--dest-repo");
    if (base.empty() || delta.empty() || dest.empty()) {
      std::fprintf(stderr, "ferry import requires --base --delta --dest-repo\n");
      return 2;
    }
    std::string summary;
    if (!ebsync::FerryImport(base, delta, dest, &summary)) {
      std::fprintf(stderr, "ferry import failed: %s\n", summary.c_str());
      return 1;
    }
    std::printf("%s\n", summary.c_str());
    return 0;
  }

  if (cmd == "verify-remote") {
    if (repo.empty()) {
      std::fprintf(stderr, "verify-remote requires --repo\n");
      return 2;
    }
    std::string json;
    if (!ebsync::VerifyRemote(repo, ArgU64(argc, argv, "--at", 0), &json)) {
      std::fprintf(stderr, "verify-remote failed\n");
      return 1;
    }
    std::cout << json;
    return 0;
  }

  if (cmd == "pull") {
    if (repo.empty()) {
      std::fprintf(stderr, "pull requires --repo\n");
      return 2;
    }
    const std::string dest = ArgValue(argc, argv, "--dest");
    if (dest.empty()) {
      std::fprintf(stderr, "pull requires --dest\n");
      return 2;
    }
    std::string summary;
    if (!ebsync::PullRemote(repo, dest, ArgU64(argc, argv, "--at", 0), &summary)) {
      std::fprintf(stderr, "pull failed\n");
      return 1;
    }
    std::printf("%s\n", summary.c_str());
    return 0;
  }

  if (cmd == "pds" && argc >= 3) {
    const std::string sub = argv[2];
    if (repo.empty()) {
      std::fprintf(stderr, "pds requires --repo\n");
      return 2;
    }
    if (sub == "auth-url") {
      std::string url;
      if (!ebsync::PdsBuildAuthUrl(repo, &url)) {
        std::fprintf(stderr, "pds auth-url failed\n");
        return 1;
      }
      std::printf("%s\n", url.c_str());
      return 0;
    }
    if (sub == "auth") {
      const std::string code = ArgValue(argc, argv, "--code");
      if (code.empty()) {
        std::fprintf(stderr, "pds auth requires --code\n");
        return 2;
      }
      std::string summary;
      if (!ebsync::PdsAuthExchangeCode(repo, code, &summary)) {
        std::fprintf(stderr, "pds auth failed: %s\n", summary.c_str());
        return 1;
      }
      std::printf("%s\n", summary.c_str());
      return 0;
    }
    if (sub == "setup-drive") {
      std::string summary;
      if (!ebsync::PdsSetupDrive(repo, &summary)) {
        std::fprintf(stderr, "pds setup-drive failed: %s\n", summary.c_str());
        return 1;
      }
      std::printf("%s\n", summary.c_str());
      return 0;
    }
    std::fprintf(stderr, "unknown pds subcommand: %s\n", sub.c_str());
    return 2;
  }

  if (cmd == "maintenance-check") {
    if (repo.empty()) {
      std::fprintf(stderr, "maintenance-check requires --repo\n");
      return 2;
    }
    std::string reason;
    const bool blocked = ebsync::ShouldBlockMaintenance(repo, &reason);
    if (HasFlag(argc, argv, "--json")) {
      std::printf("{\"blocked\":%s,\"reason\":\"", blocked ? "true" : "false");
      for (char c : reason) {
        if (c == '"' || c == '\\') std::putchar('\\');
        std::putchar(c);
      }
      std::printf("\"}\n");
      return 0;
    }
    if (blocked) {
      std::printf("BLOCKED: %s\n", reason.c_str());
      return 1;
    }
    std::printf("OK\n");
    return 0;
  }

  PrintUsage();
  return 2;
}
