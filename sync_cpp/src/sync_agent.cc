#include "ebsync/sync_agent.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ebbackup/archive/eb_bundle.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/state/superblock.h"
#include "ebbackup/store/snapshot_store.h"

#include "ebsync/backoff.h"
#include "ebsync/ebb_reader.h"
#include "ebsync/pds/pds_client.h"
#include "ebsync/sync_outbox.h"
#include "ebsync/sync_state.h"

namespace ebsync {
namespace {

std::string JsonEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    o += c;
  }
  return o;
}

std::string ChunkObjectKey(const std::string& hex) {
  return "chunks/" + hex;
}

std::string MetaObjectKey(const std::string& rel) {
  if (rel.rfind("snapshots/", 0) == 0 || rel == "superblock.bin" ||
      rel.rfind("manifest", 0) == 0) {
    return "meta/" + rel;
  }
  return rel;
}

bool WriteRemoteIndex(IRemoteTransport* transport, const SyncState& state,
                      uint64_t target_txn, uint64_t chunk_count) {
  std::ostringstream body;
  body << "{\n";
  body << "  \"synced_txn\": " << target_txn << ",\n";
  body << "  \"generation\": " << state.generation << ",\n";
  body << "  \"chunk_count\": " << chunk_count << ",\n";
  body << "  \"updated_unix\": " << NowUnix() << "\n";
  body << "}\n";
  const std::string json = body.str();
  const PutOptions opts{};
  const TransportResult tr = transport->Put(
      "remote_index.json",
      reinterpret_cast<const uint8_t*>(json.data()), json.size(), opts);
  return tr.ok;
}

std::string ResolveMirrorRoot(const SyncState& state, const std::string& repo_path) {
  if (const char* local = std::getenv("EBSYNC_LOCAL_ROOT"); local && *local) {
    return local;
  }
  if (!state.local_mirror_root.empty()) return state.local_mirror_root;
  return repo_path + "/.sync_remote";
}

std::string ResolveTransportLabel(const SyncState& state, const std::string& repo_path) {
  if (std::getenv("EBSYNC_LOCAL_ROOT")) return "local_dir";
  if (!state.local_mirror_root.empty()) return "local_mirror";
  if (state.remote_type == "pds" && !state.pds.domain_id.empty()) return "pds";
  if (!state.s3.bucket.empty() && !state.s3.access_key.empty()) return "s3";
  if (IsSyncInitialized(state) && state.remote_type == "ferry") return "ferry";
  return "local_fallback";
}

std::string SyncModeLabel(const SyncState& state) {
  if (state.remote_type == "ferry") return "Delta ferry (offline)";
  if (state.remote_type == "local_mirror") return "Local mirror";
  if (state.remote_type == "pds") return "PDS cloud drive";
  if (state.remote_type == "s3") return "S3 online sync";
  if (!state.local_mirror_root.empty()) return "Local mirror";
  return "Not configured";
}

uint64_t ComputeRemoteLag(const SyncState& state, uint64_t latest) {
  if (state.remote_type == "ferry") {
    const uint64_t ack = state.last_ferry_target_txn;
    return latest > ack ? latest - ack : 0;
  }
  if (!IsSyncInitialized(state)) return 0;
  return latest > state.synced_txn ? latest - state.synced_txn : 0;
}

bool ParseRemoteIndexTxn(const std::vector<uint8_t>& body, uint64_t* synced_txn) {
  if (!synced_txn) return false;
  const std::string text(body.begin(), body.end());
  const std::string needle = "\"synced_txn\"";
  const size_t pos = text.find(needle);
  if (pos == std::string::npos) return false;
  size_t i = pos + needle.size();
  while (i < text.size() && (text[i] == ' ' || text[i] == ':' || text[i] == '\t')) ++i;
  try {
    *synced_txn = std::stoull(text.substr(i));
    return true;
  } catch (...) {
    return false;
  }
}

bool BuildBundleFromMirrorDir(const std::string& mirror_root, EbbBundle* bundle,
                              std::string* error) {
  if (!bundle) return false;
  bundle->toc.clear();
  bundle->payloads.clear();
  bundle->header = {};
  std::memcpy(bundle->header.magic, "EBB1", 4);
  bundle->header.version = 1;
  uint64_t offset = 0;
  if (!std::filesystem::exists(mirror_root)) {
    if (error) *error = "mirror not found";
    return false;
  }
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(mirror_root)) {
    if (!entry.is_regular_file()) continue;
    const std::string rel =
        std::filesystem::relative(entry.path(), mirror_root).generic_string();
    if (rel == "remote_index.json") continue;
    if (rel.rfind("bundles/", 0) == 0) continue;
    std::string toc_rel;
    if (rel.rfind("chunks/", 0) == 0) {
      toc_rel = rel;
    } else if (rel.rfind("meta/", 0) == 0) {
      toc_rel = rel.substr(5);
    } else {
      toc_rel = rel;
    }
    std::ifstream in(entry.path(), std::ios::binary);
    if (!in) {
      if (error) *error = "read failed: " + rel;
      return false;
    }
    in.seekg(0, std::ios::end);
    const auto len = in.tellg();
    if (len < 0) {
      if (error) *error = "tell failed: " + rel;
      return false;
    }
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(len));
    in.read(reinterpret_cast<char*>(bytes.data()), len);
    if (!in) {
      if (error) *error = "read failed: " + rel;
      return false;
    }
    EbbTocEntry toc_entry{};
    toc_entry.relative_path = toc_rel;
    toc_entry.offset = offset;
    toc_entry.size = bytes.size();
    toc_entry.crc32 = ebbackup::Crc32(bytes.data(), bytes.size());
    bundle->toc.push_back(std::move(toc_entry));
    bundle->payloads.push_back(std::move(bytes));
    offset += bundle->payloads.back().size();
  }
  bundle->header.count = static_cast<uint32_t>(bundle->toc.size());
  return !bundle->toc.empty();
}

}  // namespace

uint64_t ReadLatestTxnId(const std::string& repo_path) {
  const std::string sb_path =
      (std::filesystem::path(repo_path) / "superblock.bin").string();
  ebbackup::BackupSuperBlockStore store(sb_path);
  ebbackup::BackupSuperBlock sb{};
  if (!store.Load(&sb).ok()) return 0;
  return sb.critical.txn_id;
}

uint64_t ReadSyncedTxnHint(const std::string& repo_path) {
  SyncState st;
  if (LoadSyncState(repo_path, &st)) return st.synced_txn;
  return 0;
}

std::unique_ptr<IRemoteTransport> CreateTransportForRepo(const std::string& repo_path,
                                                         SyncState* state) {
  if (!state) return nullptr;
  if (!LoadSyncState(repo_path, state)) *state = SyncState{};
  LoadSyncConfig(repo_path, state);
  if (const char* local = std::getenv("EBSYNC_LOCAL_ROOT"); local && *local) {
    return CreateLocalDirTransport(local);
  }
  if (!state->local_mirror_root.empty()) {
    return CreateLocalDirTransport(state->local_mirror_root);
  }
  if (state->remote_type == "pds" && !state->pds.domain_id.empty()) {
    if (auto t = CreatePdsTransport(repo_path, &state->pds)) return t;
  }
  if (state->remote_type == "s3" && !state->s3.bucket.empty() &&
      !state->s3.access_key.empty()) {
    if (auto t = CreateS3Transport(state->s3)) return t;
  }
  if (!state->s3.bucket.empty() && !state->s3.access_key.empty()) {
    if (auto t = CreateS3Transport(state->s3)) return t;
  }
  const std::string fallback = repo_path + "/.sync_remote";
  if (state->remote_type.empty()) state->remote_type = "local_mirror";
  return CreateLocalDirTransport(fallback);
}

bool InitSyncRepo(const std::string& repo_path, const SyncState& cfg) {
  if (!SaveSyncConfig(repo_path, cfg)) return false;
  SyncState state;
  if (!LoadSyncState(repo_path, &state)) state = SyncState{};
  state.s3 = cfg.s3;
  state.pds = cfg.pds;
  state.remote_type = cfg.remote_type.empty() ? "s3" : cfg.remote_type;
  state.local_mirror_root = cfg.local_mirror_root;
  return SaveSyncState(repo_path, state);
}

bool InitSyncRepoLocal(const std::string& repo_path, const std::string& mirror_root) {
  SyncState cfg;
  cfg.remote_type = "local_mirror";
  cfg.local_mirror_root = mirror_root;
  return InitSyncRepo(repo_path, cfg);
}

bool InitSyncRepoFerry(const std::string& repo_path) {
  SyncState cfg;
  cfg.remote_type = "ferry";
  return InitSyncRepo(repo_path, cfg);
}

bool InitSyncRepoPds(const std::string& repo_path, const PdsRemoteConfig& pds_cfg,
                     const std::string& repo_label) {
  SyncState cfg;
  cfg.remote_type = "pds";
  cfg.pds = pds_cfg;
  if (cfg.pds.api_endpoint.empty() && !cfg.pds.domain_id.empty()) {
    cfg.pds.api_endpoint = DefaultPdsApiEndpoint(cfg.pds.domain_id);
  }
  if (cfg.pds.redirect_uri.empty() && !cfg.pds.domain_id.empty()) {
    cfg.pds.redirect_uri = DefaultPdsRedirectUri(cfg.pds.domain_id);
  }
  if (!repo_label.empty()) {
    cfg.pds.root_prefix = "ebbackup/" + repo_label;
  } else if (cfg.pds.root_prefix.empty()) {
    cfg.pds.root_prefix = "ebbackup";
  }
  return InitSyncRepo(repo_path, cfg);
}

bool PdsBuildAuthUrl(const std::string& repo_path, std::string* url_out) {
  SyncState state;
  LoadSyncState(repo_path, &state);
  LoadSyncConfig(repo_path, &state);
  PdsClient client(repo_path, &state.pds);
  std::string err;
  if (!client.BuildAuthorizeUrl(url_out, &err)) return false;
  return true;
}

bool PdsAuthExchangeCode(const std::string& repo_path, const std::string& code,
                         std::string* summary_out) {
  SyncState state;
  LoadSyncState(repo_path, &state);
  LoadSyncConfig(repo_path, &state);
  PdsClient client(repo_path, &state.pds);
  std::string err;
  if (!client.ExchangeAuthCode(code, &err)) {
    if (summary_out) *summary_out = err;
    return false;
  }
  if (!client.ResolveDriveId(&err)) {
    if (summary_out) *summary_out = "auth OK but drive resolve failed: " + err;
    return false;
  }
  if (summary_out) {
    *summary_out = "pds auth OK drive_id=" + state.pds.drive_id;
  }
  return true;
}

bool PdsSetupDrive(const std::string& repo_path, std::string* summary_out) {
  SyncState state;
  LoadSyncState(repo_path, &state);
  LoadSyncConfig(repo_path, &state);
  PdsClient client(repo_path, &state.pds);
  std::string err;
  if (!client.ResolveDriveId(&err)) {
    if (summary_out) *summary_out = err;
    return false;
  }
  if (summary_out) *summary_out = "drive_id=" + state.pds.drive_id;
  return true;
}

std::string BuildStatusJson(const std::string& repo_path) {
  SyncState state;
  LoadSyncState(repo_path, &state);
  LoadSyncConfig(repo_path, &state);
  const uint64_t latest = ReadLatestTxnId(repo_path);
  const uint64_t remote_lag = ComputeRemoteLag(state, latest);
  const std::string transport = ResolveTransportLabel(state, repo_path);
  const std::string sync_mode = SyncModeLabel(state);
  std::ostringstream o;
  o << "{\n";
  o << "  \"latest_txn\": " << latest << ",\n";
  o << "  \"synced_txn\": " << state.synced_txn << ",\n";
  o << "  \"pending_txn\": " << state.pending_txn << ",\n";
  o << "  \"last_export_base_txn\": " << state.last_export_base_txn << ",\n";
  o << "  \"last_ferry_target_txn\": " << state.last_ferry_target_txn << ",\n";
  o << "  \"pending_chunk_count\": " << state.pending_chunk_count << ",\n";
  o << "  \"remote_lag_txn\": " << remote_lag << ",\n";
  o << "  \"remote_type\": \"" << JsonEscape(state.remote_type) << "\",\n";
  o << "  \"local_mirror_root\": \"" << JsonEscape(state.local_mirror_root) << "\",\n";
  o << "  \"pds_domain_id\": \"" << JsonEscape(state.pds.domain_id) << "\",\n";
  o << "  \"pds_drive_id\": \"" << JsonEscape(state.pds.drive_id) << "\",\n";
  o << "  \"pds_authed\": "
    << ((!state.pds.refresh_token.empty()) ? "true" : "false") << ",\n";
  o << "  \"sync_mode_label\": \"" << JsonEscape(sync_mode) << "\",\n";
  o << "  \"generation\": " << state.generation << ",\n";
  o << "  \"last_success_unix\": " << state.last_success_unix << ",\n";
  o << "  \"backoff_until_unix\": " << state.backoff_until_unix << ",\n";
  o << "  \"last_error\": \"" << JsonEscape(state.last_error) << "\",\n";
  o << "  \"maintenance_blocked\": "
    << (ShouldBlockMaintenance(repo_path, nullptr) ? "true" : "false") << ",\n";
  o << "  \"transport\": \"" << transport << "\"\n";
  o << "}\n";
  return o.str();
}

bool BuildPlanJson(const std::string& repo_path, uint64_t base_txn,
                   std::string* json_out) {
  if (!json_out) return false;
  const uint64_t target = ReadLatestTxnId(repo_path);
  if (base_txn == 0) base_txn = ReadSyncedTxnHint(repo_path);
  if (target <= base_txn) {
    *json_out = "{\"base_txn\":" + std::to_string(base_txn) +
                ",\"target_txn\":" + std::to_string(target) + ",\"chunk_count\":0}\n";
    return true;
  }
  const std::string temp =
      (std::filesystem::temp_directory_path() / "eb_sync_plan.ebb").string();
  ebbackup::EbBundleDeltaOptions opts{};
  opts.base_txn_id = base_txn;
  opts.target_txn_id = target;
  ebbackup::EbBundleDeltaStats stats{};
  const ebbackup::Status st =
      ebbackup::ExportRepoDeltaToBundle(repo_path, temp, opts, &stats);
  if (!st.ok()) return false;
  EbbBundle bundle;
  std::string err;
  if (!ReadEbbBundle(temp, &bundle, &err)) return false;
  uint64_t chunks = 0;
  for (const auto& e : bundle.toc) {
    std::string hex;
    if (IsChunkTocPath(e.relative_path, &hex)) ++chunks;
  }
  std::filesystem::remove(temp);
  std::ostringstream o;
  o << "{\n  \"base_txn\": " << base_txn << ",\n  \"target_txn\": " << target
    << ",\n  \"chunk_count\": " << chunks << ",\n  \"delta_bytes\": " << stats.delta_bytes
    << "\n}\n";
  *json_out = o.str();
  return true;
}

PushReport PushSync(const std::string& repo_path, const PushOptions& opts) {
  PushReport report;
  SyncState state;
  auto transport = CreateTransportForRepo(repo_path, &state);
  if (!transport) {
    report.error = "no transport";
    return report;
  }

  if (state.backoff_until_unix > NowUnix()) {
    report.error = "backoff active";
    return report;
  }

  const uint64_t latest = ReadLatestTxnId(repo_path);
  if (latest > state.synced_txn) {
    EnqueueSyncOutbox(repo_path, state.synced_txn, latest);
    state.pending_txn = latest;
    SaveSyncState(repo_path, state);
  }

  std::vector<SyncOutboxEntry> entries;
  LoadSyncOutbox(repo_path, &entries);
  int attempt = 0;
  for (auto& entry : entries) {
    if (entry.state == OutboxState::kDone) continue;
    if (opts.once && entry.state == OutboxState::kFailed) continue;
    entry.state = OutboxState::kUploading;
    SaveSyncOutbox(repo_path, entries);

    const std::string temp =
        (std::filesystem::temp_directory_path() /
         ("eb_sync_" + std::to_string(entry.target_txn) + ".ebb"))
            .string();
    ebbackup::EbBundleDeltaStats stats{};
    ebbackup::Status exp;
    if (entry.base_txn == 0) {
      exp = ebbackup::ExportRepoToBundle(repo_path, temp, {});
      stats.delta_chunk_count = 0;
      EbbBundle bundle;
      std::string err;
      if (exp.ok() && ReadEbbBundle(temp, &bundle, &err)) {
        for (const auto& toc : bundle.toc) {
          std::string hex;
          if (IsChunkTocPath(toc.relative_path, &hex)) ++stats.delta_chunk_count;
        }
      }
    } else {
      ebbackup::EbBundleDeltaOptions delta_opts{};
      delta_opts.base_txn_id = entry.base_txn;
      delta_opts.target_txn_id = entry.target_txn;
      exp = ebbackup::ExportRepoDeltaToBundle(repo_path, temp, delta_opts, &stats);
    }
    if (!exp.ok()) {
      entry.state = OutboxState::kFailed;
      entry.last_error = exp.message();
      state.last_error = exp.message();
      SaveSyncOutbox(repo_path, entries);
      SaveSyncState(repo_path, state);
      report.error = exp.message();
      return report;
    }

    EbbBundle bundle;
    std::string err;
    if (!ReadEbbBundle(temp, &bundle, &err)) {
      entry.state = OutboxState::kFailed;
      entry.last_error = err;
      state.last_error = err;
      SaveSyncOutbox(repo_path, entries);
      SaveSyncState(repo_path, state);
      report.error = err;
      std::filesystem::remove(temp);
      return report;
    }

    uint64_t uploaded = 0;
    for (size_t i = 0; i < bundle.toc.size(); ++i) {
      const auto& toc = bundle.toc[i];
      std::string hex;
      std::string key;
      if (IsChunkTocPath(toc.relative_path, &hex)) {
        key = ChunkObjectKey(hex);
      } else if (toc.relative_path.rfind("chunks/", 0) == 0) {
        continue;
      } else {
        key = MetaObjectKey(toc.relative_path);
      }
      PutOptions put_opts{};
      TransportResult tr = transport->Put(key, bundle.payloads[i].data(),
                                          bundle.payloads[i].size(), put_opts);
      if (!tr.ok) {
        ++attempt;
        if (tr.retryable && attempt < opts.max_attempts) {
          state.backoff_until_unix = NowUnix() + ComputeBackoffSeconds(attempt);
          state.last_error = tr.error;
          entry.state = OutboxState::kPending;
          SaveSyncState(repo_path, state);
          SaveSyncOutbox(repo_path, entries);
          std::filesystem::remove(temp);
          report.error = tr.error;
          return report;
        }
        entry.state = OutboxState::kFailed;
        entry.last_error = tr.error;
        state.last_error = tr.error;
        SaveSyncOutbox(repo_path, entries);
        SaveSyncState(repo_path, state);
        std::filesystem::remove(temp);
        report.error = tr.error;
        return report;
      }
      if (IsChunkTocPath(toc.relative_path, &hex)) ++uploaded;
    }

    if (!WriteRemoteIndex(transport.get(), state, entry.target_txn, stats.delta_chunk_count)) {
      report.error = "remote_index write failed";
      entry.state = OutboxState::kFailed;
      SaveSyncOutbox(repo_path, entries);
      std::filesystem::remove(temp);
      return report;
    }

    std::filesystem::remove(temp);
    entry.state = OutboxState::kDone;
    state.synced_txn = entry.target_txn;
    state.pending_txn = 0;
    state.pending_chunk_count = 0;
    state.last_success_unix = NowUnix();
    state.backoff_until_unix = 0;
    state.last_error.clear();
    SaveSyncOutbox(repo_path, entries);
    SaveSyncState(repo_path, state);
    report.chunks_uploaded += uploaded;
    report.synced_txn = entry.target_txn;
    report.ok = true;
    if (opts.once) break;
  }

  if (!report.ok && report.error.empty()) report.ok = true;
  return report;
}

bool FerryExport(const std::string& repo_path, const std::string& out_dir,
                 const FerryExportOptions& opts, std::string* summary_out) {
  SyncState state;
  LoadSyncState(repo_path, &state);
  LoadSyncConfig(repo_path, &state);
  uint64_t base_txn = opts.base_txn;
  uint64_t target_txn = opts.target_txn;
  if (opts.auto_base && base_txn == 0) base_txn = state.last_export_base_txn;
  if (base_txn == 0) base_txn = state.synced_txn;
  if (target_txn == 0) target_txn = ReadLatestTxnId(repo_path);
  if (target_txn <= base_txn) {
    if (summary_out) *summary_out = "no delta: target <= base";
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(out_dir, ec);
  const std::string out_path =
      (std::filesystem::path(out_dir) /
       ("delta_" + std::to_string(base_txn) + "_" + std::to_string(target_txn) + ".ebb"))
          .string();
  ebbackup::EbBundleDeltaOptions delta_opts{};
  delta_opts.base_txn_id = base_txn;
  delta_opts.target_txn_id = target_txn;
  ebbackup::EbBundleDeltaStats stats{};
  const ebbackup::Status st =
      ebbackup::ExportRepoDeltaToBundle(repo_path, out_path, delta_opts, &stats);
  if (!st.ok()) return false;
  state.last_export_base_txn = base_txn;
  state.last_ferry_target_txn = target_txn;
  state.pending_txn = target_txn;
  state.pending_chunk_count = stats.delta_chunk_count;
  if (state.remote_type.empty()) state.remote_type = "ferry";
  SaveSyncState(repo_path, state);

  if (opts.also_mirror) {
    auto transport = CreateTransportForRepo(repo_path, &state);
    if (transport) {
      std::ifstream in(out_path, std::ios::binary);
      in.seekg(0, std::ios::end);
      const auto len = in.tellg();
      in.seekg(0, std::ios::beg);
      std::vector<uint8_t> bytes(static_cast<size_t>(len));
      in.read(reinterpret_cast<char*>(bytes.data()), len);
      const std::string bundle_key =
          "bundles/" + std::to_string(base_txn) + "-" + std::to_string(target_txn) + ".ebb";
      transport->Put(bundle_key, bytes.data(), bytes.size(), PutOptions{});
      WriteRemoteIndex(transport.get(), state, target_txn, stats.delta_chunk_count);
    }
  }

  if (summary_out) {
    std::ostringstream o;
    o << "exported " << out_path << " chunks=" << stats.delta_chunk_count
      << " bytes=" << stats.delta_bytes;
    *summary_out = o.str();
  }
  return true;
}

bool FerryImport(const std::string& base_path, const std::string& delta_path,
                 const std::string& dest_repo, std::string* summary_out) {
  const ebbackup::Status st =
      ebbackup::ImportBundleDeltaToRepo(base_path, delta_path, dest_repo);
  if (!st.ok()) {
    if (summary_out) *summary_out = st.message();
    return false;
  }
  ebbackup::BackupEngine engine(dest_repo);
  if (!engine.Open().ok() || !engine.Verify().ok()) {
    if (summary_out) *summary_out = "imported but verify failed";
    return false;
  }
  if (summary_out) *summary_out = "import OK: " + dest_repo;
  return true;
}

bool VerifyRemote(const std::string& repo_path, uint64_t at_txn, std::string* json_out) {
  SyncState state;
  auto transport = CreateTransportForRepo(repo_path, &state);
  if (!transport) return false;
  if (at_txn == 0) at_txn = state.synced_txn;
  std::vector<uint8_t> body;
  const TransportResult tr = transport->Get("remote_index.json", &body);
  if (!tr.ok) return false;
  const std::string text(body.begin(), body.end());
  if (json_out) {
    std::ostringstream o;
    o << "{\n  \"remote_index\": " << text << ",\n  \"requested_txn\": " << at_txn
      << ",\n  \"ok\": true\n}\n";
    *json_out = o.str();
  }
  return true;
}

bool PullRemote(const std::string& repo_path, const std::string& dest_repo,
                uint64_t at_txn, std::string* summary_out) {
  SyncState state;
  auto transport = CreateTransportForRepo(repo_path, &state);
  if (!transport) return false;
  std::vector<uint8_t> index_bytes;
  if (!transport->Get("remote_index.json", &index_bytes).ok) return false;
  uint64_t remote_txn = 0;
  if (!ParseRemoteIndexTxn(index_bytes, &remote_txn)) return false;
  if (at_txn == 0) at_txn = remote_txn;
  std::error_code ec;
  std::filesystem::create_directories(dest_repo, ec);

  const std::string mirror_root = ResolveMirrorRoot(state, repo_path);
  for (const auto& entry : std::filesystem::directory_iterator(
           std::filesystem::path(mirror_root) / "bundles", ec)) {
    if (!entry.is_regular_file()) continue;
    const std::string name = entry.path().filename().string();
    if (name.size() < 5 || name.compare(name.size() - 4, 4, ".ebb") != 0) continue;
    const size_t dash = name.find('-');
    if (dash == std::string::npos) continue;
    const uint64_t target =
        std::stoull(name.substr(dash + 1, name.size() - dash - 5));
    if (target != at_txn) continue;
    const std::string bundle_key = "bundles/" + name;
    std::vector<uint8_t> bundle;
    if (!transport->Get(bundle_key, &bundle).ok || bundle.empty()) continue;
    const std::string path = dest_repo + "/pulled.ebb";
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bundle.data()),
              static_cast<std::streamsize>(bundle.size()));
    const ebbackup::Status imp = ebbackup::ImportBundleToRepo(path, dest_repo);
    std::filesystem::remove(path, ec);
    if (!imp.ok()) {
      if (summary_out) *summary_out = imp.message();
      return false;
    }
    ebbackup::BackupEngine engine(dest_repo);
    if (!engine.Open().ok() || !engine.Verify().ok()) {
      if (summary_out) *summary_out = "imported but verify failed";
      return false;
    }
    if (summary_out) *summary_out = "pull OK from bundle: " + dest_repo;
    return true;
  }

  EbbBundle bundle;
  std::string err;
  if (!BuildBundleFromMirrorDir(mirror_root, &bundle, &err)) {
    if (summary_out) *summary_out = err.empty() ? "mirror empty" : err;
    return false;
  }
  const std::string temp =
      (std::filesystem::temp_directory_path() / "eb_sync_pull.ebb").string();
  if (!WriteEbbBundle(temp, bundle, &err)) {
    if (summary_out) *summary_out = err;
    return false;
  }
  const ebbackup::Status imp = ebbackup::ImportBundleToRepo(temp, dest_repo);
  std::filesystem::remove(temp, ec);
  if (!imp.ok()) {
    if (summary_out) *summary_out = imp.message();
    return false;
  }
  ebbackup::BackupEngine engine(dest_repo);
  if (!engine.Open().ok() || !engine.Verify().ok()) {
    if (summary_out) *summary_out = "imported but verify failed";
    return false;
  }
  if (summary_out) {
    *summary_out = "pull OK from mirror objects txn=" + std::to_string(at_txn);
  }
  return true;
}

bool ShouldBlockMaintenance(const std::string& repo_path, std::string* reason_out) {
  SyncState state;
  if (!LoadSyncState(repo_path, &state)) return false;
  if (!IsSyncInitialized(state)) return false;
  const uint64_t latest = ReadLatestTxnId(repo_path);
  if (state.remote_type == "ferry") {
    if (latest > state.last_ferry_target_txn) {
      if (reason_out) {
        std::ostringstream o;
        o << "unexported txn " << state.last_ferry_target_txn + 1 << ".." << latest
          << "; run eb-sync ferry export first";
        *reason_out = o.str();
      }
      return true;
    }
    return false;
  }
  if (latest > state.synced_txn) {
    if (reason_out) {
      std::ostringstream o;
      o << "unsynced txn " << state.synced_txn + 1 << ".." << latest;
      if (state.remote_type == "local_mirror") {
        o << "; run eb-sync push first";
      } else {
        o << "; run eb-sync push first";
      }
      *reason_out = o.str();
    }
    return true;
  }
  return false;
}

}  // namespace ebsync
