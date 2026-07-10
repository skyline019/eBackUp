#include "ebbackup/engine/backup_engine.h"

#include "ebbackup/plugin/plugin_registry.h"
#include "ebbackup/scan/scan_hint_options.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ebbackup/audit/carl_anchor.h"
#include "ebbackup/audit/merkle.h"
#include "ebbackup/audit/rar_chain.h"
#include "ebbackup/catalog/manifest_diff.h"
#include "ebbackup/catalog/manifest_browse_index.h"
#include "ebbackup/catalog/path_index.h"
#include "ebbackup/catalog/restore_acceptance.h"
#include "ebbackup/catalog/snapshot_meta.h"
#include "ebbackup/catalog/job_report.h"
#include "ebbackup/catalog/snapshot_reachability.h"
#include "ebbackup/report/rpo_summary.h"
#include "ebbackup/store/orphan_explain.h"
#include "ebbackup/job/job_config.h"
#include "ebbackup/job/backup_window.h"
#include "ebbackup/queue/job_queue.h"
#include "ebbackup/chunk/chunk_profile.h"
#include "ebbackup/chunk/rolling_checksum.h"
#include "ebbackup/codec/content_class.h"
#include "ebbackup/codec/zstd_dict.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/fsync.h"
#include "ebbackup/common/hook_runner.h"
#include "ebbackup/common/path_encoding.h"
#include "ebbackup/common/path_util.h"
#include "ebbackup/crypto/aes_gcm.h"
#include "ebbackup/crypto/envelope.h"
#include "ebbackup/engine/restore_engine.h"
#include "ebbackup/io/mmap_reader.h"
#include "ebbackup/pipeline/backup_pipeline.h"
#include "ebbackup/pipeline/file_scheduler.h"
#include "ebbackup/pipeline/pipeline_phase_stats.h"
#include "ebbackup/scan/backup_filter.h"
#include "ebbackup/store/snapshot_store.h"
#include "ebbackup/scan/scan_entry.h"
#include "ebbackup/winmeta/sparse_file.h"
#include "ebbackup/winmeta/efs_key.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace ebbackup {

namespace {

std::string RepoJoin(const std::string& repo, const std::string& name) {
  return PathToUtf8(PathFromUtf8(repo) / PathFromUtf8(name));
}

std::vector<uint8_t> ReadFileBytes(const std::string& path) {
#ifdef _WIN32
  const size_t colon = path.find(':');
  if (colon != std::string::npos && colon > 1) {
    const std::wstring wide = Utf8ToWide(path);
    HANDLE h = CreateFileW(wide.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
      LARGE_INTEGER size{};
      if (GetFileSizeEx(h, &size) && size.QuadPart >= 0 &&
          size.QuadPart <= static_cast<LONGLONG>(SIZE_MAX)) {
        std::vector<uint8_t> out(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) &&
            read == out.size()) {
          CloseHandle(h);
          return out;
        }
      }
      CloseHandle(h);
    }
  }
#endif
  MmapReader reader;
  if (reader.Open(path).ok()) {
    return std::vector<uint8_t>(reader.data(), reader.data() + reader.size());
  }
  std::ifstream in(PathFromUtf8(path), std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
}

std::unordered_set<std::string> BuildIssuePathSet(
    const std::vector<report::BackupPathIssue>& issues) {
  std::unordered_set<std::string> paths;
  for (const auto& issue : issues) {
    if (!issue.path.empty()) paths.insert(issue.path);
  }
  return paths;
}

#ifdef _WIN32
Status ReadFileRangeBytes(const std::string& path, uint64_t offset, uint64_t length,
                          std::vector<uint8_t>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->assign(static_cast<size_t>(length), 0);
  if (length == 0) return Status::Ok();
  HANDLE h = CreateFileW(Utf8ToWide(path).c_str(), GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return Status::IoError("ReadFileRange open failed");
  }
  LARGE_INTEGER pos{};
  pos.QuadPart = static_cast<LONGLONG>(offset);
  if (!SetFilePointerEx(h, pos, nullptr, FILE_BEGIN)) {
    CloseHandle(h);
    return Status::IoError("ReadFileRange seek failed");
  }
  DWORD read = 0;
  if (!ReadFile(h, out->data(), static_cast<DWORD>(out->size()), &read, nullptr) ||
      read != out->size()) {
    CloseHandle(h);
    return Status::IoError("ReadFileRange read failed");
  }
  CloseHandle(h);
  return Status::Ok();
}

Status ChunkSparseFile(EbHcrboChunker* chunker, BackupMode mode,
                       const std::string& file_path,
                       const std::vector<winmeta::SparseRun>& runs,
                       const CfiIndex& history,
                       std::vector<ChunkDescriptor>* out_chunks,
                       CfiIndex* out_cfi, EbHcrboStats* stats) {
  if (!chunker || !out_chunks || !out_cfi) {
    return Status::InvalidArgument("invalid ChunkSparseFile args");
  }
  out_chunks->clear();
  out_cfi->anchors.clear();
  for (const auto& run : runs) {
    std::vector<uint8_t> run_bytes;
    const Status rd = ReadFileRangeBytes(file_path, run.offset, run.length, &run_bytes);
    if (!rd.ok()) return rd;
    std::vector<ChunkDescriptor> run_chunks;
    CfiIndex run_cfi;
    Status ch_st;
    if (mode == BackupMode::kIncremental) {
      ch_st = chunker->ChunkIncremental(run_bytes.data(), run_bytes.size(), history,
                                        &run_chunks, &run_cfi, stats);
    } else {
      ch_st = chunker->ChunkFull(run_bytes.data(), run_bytes.size(), &run_chunks,
                                 &run_cfi, stats);
    }
    if (!ch_st.ok()) return ch_st;
    for (auto& c : run_chunks) c.offset += run.offset;
    for (auto& a : run_cfi.anchors) a.offset += run.offset;
    out_chunks->insert(out_chunks->end(), run_chunks.begin(), run_chunks.end());
    out_cfi->anchors.insert(out_cfi->anchors.end(), run_cfi.anchors.begin(),
                            run_cfi.anchors.end());
  }
  return Status::Ok();
}
#endif

class VssReleaseGuard {
 public:
  VssReleaseGuard(winmeta::VssSession* session, bool* active,
                  std::vector<report::BackupPathIssue>* issues,
                  winmeta::VssSessionInfo* info_out = nullptr)
      : session_(session),
        active_(active),
        issues_(issues),
        info_out_(info_out) {}

  ~VssReleaseGuard() { release(); }

  void release() {
    if (released_ || !active_ || !*active_ || !session_) return;
    if (!session_->backup_finished()) {
      (void)session_->FinishBackup();
    }
    if (info_out_) *info_out_ = session_->info();
    if (issues_) {
      for (auto& issue : *issues_) {
        if (!issue.path.empty()) {
          issue.path = session_->MapToLogicalForReport(issue.path);
        }
      }
    }
    (void)session_->End();
    *active_ = false;
    released_ = true;
  }

 private:
  winmeta::VssSession* session_{nullptr};
  bool* active_{nullptr};
  std::vector<report::BackupPathIssue>* issues_{nullptr};
  winmeta::VssSessionInfo* info_out_{nullptr};
  bool released_{false};
};

uint64_t CountBackedUpManifestFiles(const std::vector<ManifestFileEntry>& files) {
  uint64_t count = 0;
  for (const auto& f : files) {
    if (f.file_type == FileType::kRegular) {
      if (f.size > 0 || !f.chunk_hashes_hex.empty()) ++count;
    } else {
      ++count;
    }
  }
  return count;
}

CfiIndex FindPriorCfi(const ManifestDocument& prior,
                      const std::string& relative_path) {
  for (const auto& f : prior.files) {
    if (f.relative_path == relative_path) return f.cfi;
  }
  return CfiIndex{};
}

void PopulateAnchorChecksums(const uint8_t* data, size_t len, CfiIndex* cfi) {
  if (!cfi) return;
  for (auto& a : cfi->anchors) {
    if (a.offset + a.length <= len) {
      a.rolling_checksum = RollingChecksum(data + a.offset, a.length);
    }
  }
}

Status ReadManifestBodyCrc32FromFile(const std::string& path, uint32_t* out) {
  if (!out) return Status::InvalidArgument("out is null");
  std::ifstream in(PathFromUtf8(path), std::ios::binary);
  if (!in) return Status::IoError("cannot open manifest: " + path);
  std::string header;
  if (!std::getline(in, header)) return Status::Corrupt("empty manifest");
  if (header == "EBMANIFEST4" || header == "EBMANIFEST5") {
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    if (raw.size() < 4) return Status::Corrupt("manifest v4 too short");
    *out = static_cast<uint32_t>(raw[raw.size() - 4]) |
           (static_cast<uint32_t>(raw[raw.size() - 3]) << 8) |
           (static_cast<uint32_t>(raw[raw.size() - 2]) << 16) |
           (static_cast<uint32_t>(raw[raw.size() - 1]) << 24);
    raw.resize(raw.size() - 4);
    uint32_t actual = 0;
    const Status crc_st = ComputeManifestV4BodyCrc32(raw, &actual);
    if (!crc_st.ok()) return crc_st;
    if (actual != *out) return Status::Corrupt("manifest v4 crc mismatch");
    return Status::Ok();
  }
  if (header != "EBMANIFEST2" && header != "EBMANIFEST3") {
    return Status::Corrupt("manifest v2/v3/v4 required for crc");
  }
  std::ostringstream body_stream;
  std::string line;
  std::string footer_line;
  while (std::getline(in, line)) {
    if (line.rfind("footer\t", 0) == 0) {
      footer_line = line;
      break;
    }
    body_stream << line << "\n";
  }
  if (footer_line.empty()) return Status::Corrupt("manifest footer missing");
  *out = static_cast<uint32_t>(
      std::stoul(footer_line.substr(7), nullptr, 16));
  const std::string body = body_stream.str();
  uint32_t actual = 0;
  const Status crc_st = ComputeManifestBodyCrc32(body, &actual);
  if (!crc_st.ok()) return crc_st;
  if (actual != *out) return Status::Corrupt("manifest crc mismatch");
  return Status::Ok();
}

void NormalizeCompressOptions(BackupOptions* opts) {
  if (!opts) return;
  if (opts->compress_mode == CompressMode::kOff && opts->use_lz4) {
    opts->compress_mode = CompressMode::kLz4;
  }
}

ChunkStorePutOptions BuildPutOptions(const BackupOptions& options,
                                     const std::string& path_hint,
                                     ContentClassStats* stats,
                                     const ZstdDictionary* zstd_dict,
                                     ZstdDictTrainer* dict_trainer) {
  ChunkStorePutOptions put{};
  put.use_encryption = options.use_encryption;
  put.compress_mode = options.compress_mode;
  put.compress_tier = options.compress_tier;
  put.compress_level = options.compress_level;
  put.use_zstd_dict = options.use_zstd_dict;
  put.cpu_budget_permille = options.cpu_budget_permille;
  if (!path_hint.empty()) put.path_hint = path_hint.c_str();
  put.content_stats = stats;
  if (options.use_zstd_dict && zstd_dict && !zstd_dict->empty()) {
    put.zstd_dict = zstd_dict;
  }
  if (options.use_zstd_dict) {
    put.dict_trainer = dict_trainer;
  }
  return put;
}

BackupOptions ResolveBackupOptions(const BackupSuperBlock& sb,
                                   const BackupOptions& options) {
  BackupOptions effective = options;
  NormalizeCompressOptions(&effective);
  if (effective.compress_mode == CompressMode::kOff && !effective.use_lz4) {
    if (sb.ext.default_codec == kDefaultCodecAuto) {
      effective.compress_mode = CompressMode::kAuto;
      if (effective.cpu_budget_permille == 1000) {
        effective.cpu_budget_permille = 600;
      }
    } else if (sb.ext.default_codec == kDefaultCodecLz4) {
      effective.use_lz4 = true;
    } else if (sb.ext.default_codec == kDefaultCodecZstd) {
      effective.compress_mode = CompressMode::kZstd;
    }
  }
  NormalizeCompressOptions(&effective);
  if (RepoUsesEbPack(sb) && !options.disable_pipeline) {
    effective.use_pipeline = true;
  }
  if (effective.compress_tier != CompressTier::kFast) {
    effective.use_zstd_dict = true;
  }
  return effective;
}

}  // namespace

BackupEngine::BackupEngine(std::string repo_path)
    : repo_path_(std::move(repo_path)) {}

void BackupEngine::SetProgressCallback(ProgressCallback cb, void* user_data) {
  progress_cb_ = std::move(cb);
  progress_user_ = user_data;
}

void BackupEngine::EmitProgress(uint64_t permille) {
  if (progress_cb_) progress_cb_(permille, progress_user_);
}

Status BackupEngine::MaybeTestAbortAfter(BackupPhase phase) const {
  const char* env = std::getenv("EBTEST_ABORT_AFTER");
  if (!env || env[0] == '\0') return Status::Ok();
  const int target = std::atoi(env);
  if (target == static_cast<int>(phase)) {
    return Status::Internal("test abort after phase");
  }
  return Status::Ok();
}

void BackupEngine::ResetBackupWindowState() {
  active_window_ = {};
  window_end_unix_ = 0;
  durability_downgraded_ = false;
  window_truncated_ = false;
}

void BackupEngine::InitBackupWindow(const job::BackupWindowPolicy& policy) {
  active_window_ = policy;
  if (active_window_.deadline_grace_seconds <= 0) {
    active_window_.deadline_grace_seconds = 300;
  }
  window_end_unix_ = job::WindowEndUnix(std::time(nullptr), active_window_);
}

void BackupEngine::MaybeAdaptBackupWindow() {
  if (!job::HasBackupWindow(active_window_)) return;
  const std::time_t now = std::time(nullptr);
  if (active_window_.durability_adaptive && !durability_downgraded_ &&
      job::ShouldDowngradeDurability(now, active_window_)) {
    chunk_store_->SetDurabilityMode(DurabilityMode::kBalanced);
    durability_downgraded_ = true;
  }
  if (!active_window_.window_end.empty() && window_end_unix_ > 0 &&
      now >= window_end_unix_) {
    window_truncated_ = true;
  }
}

void BackupEngine::TruncatePendingFilesAt(size_t index) {
  pending_files_.resize(index);
  pending_file_bytes_.resize(index);
  pending_chunks_.resize(index);
  pending_cfi_.resize(index);
  pending_manifest_.resize(index);
}

Status BackupEngine::InitRepo(const std::string& repo_path,
                              bool standard_digest) {
  RepoInitOptions opts{};
  opts.standard_digest = standard_digest;
  return InitRepoEx(repo_path, opts);
}

Status BackupEngine::InitRepoEx(const std::string& repo_path,
                                const RepoInitOptions& options) {
  std::error_code ec;
  std::filesystem::create_directories(PathFromUtf8(RepoJoin(repo_path, "data")), ec);
  if (ec) return Status::IoError("cannot create repo data dir");
  std::filesystem::create_directories(PathFromUtf8(RepoJoin(repo_path, "audit")), ec);
  if (ec) return Status::IoError("cannot create repo audit dir");
  BackupSuperBlockStore sb_store(RepoJoin(repo_path, "superblock.bin"));
  BackupSuperBlock sb{};
  SetPhase(&sb, BackupPhase::kIdle);
  sb.format_version = kBackupSuperBlockFormatV2;
  sb.ext.next_txn_id = 1;
  if (options.standard_digest) {
    sb.ext.backup_features |= kBackupFeatureDigestStandard;
  }
  if (options.persistent_index) {
    sb.ext.backup_features |= kBackupFeaturePersistentIndex;
  }
  if (options.manifest_binary) {
    sb.ext.backup_features |= kBackupFeatureManifestBinary;
  }
  if (options.snapshots) {
    sb.ext.backup_features |= kBackupFeatureSnapshots;
  }
  if (options.ebpack) {
    sb.ext.backup_features |= kBackupFeatureEbPack;
    std::filesystem::create_directories(PathFromUtf8(RepoJoin(repo_path, "data/packs")), ec);
    if (ec) return Status::IoError("cannot create repo packs dir");
  }
  if (options.coalesced_meta) {
    sb.ext.backup_features |= kBackupFeatureCoalescedMeta;
  }
  if (options.persistent_index || options.manifest_binary) {
    sb.ext.default_codec = kDefaultCodecAuto;
  }
  return sb_store.Commit(sb);
}

Status BackupEngine::StartupSelfCheck() {
  const std::string temp = RepoJoin(repo_path_, "manifest.new");
  std::error_code ec;
  if (std::filesystem::exists(temp)) {
    std::filesystem::remove(temp, ec);
  }

  const std::string manifest_path = RepoJoin(repo_path_, "manifest");
  const bool has_manifest = std::filesystem::exists(manifest_path);

  const bool in_progress =
      phase_ == BackupPhase::kScanning || phase_ == BackupPhase::kChunking ||
      phase_ == BackupPhase::kStoring || phase_ == BackupPhase::kCommittingMeta ||
      phase_ == BackupPhase::kAuditing;

  if (in_progress) {
    ManifestDocument manifest{};
    const Status manifest_st = ReadManifestAuto(manifest_path, &manifest);
    if (manifest_st.ok() && manifest.txn_id > sb_.critical.txn_id) {
      sb_.critical.txn_id = manifest.txn_id;
      SetNextTxnId(&sb_, manifest.txn_id + 1);
      SetPhase(&sb_, BackupPhase::kIdle);
      sb_.critical.chunks_written = 0;
      sb_.critical.bytes_processed = 0;
      return superblock_store_->Commit(sb_);
    }

    stats_.orphan_chunks_hint = sb_.critical.chunks_written;
    SetPhase(&sb_, BackupPhase::kAborted);
    return superblock_store_->Commit(sb_);
  }

  if (RepoUsesCoalescedMeta(sb_) && phase_ == BackupPhase::kIdle) {
    if (!has_manifest && RepoUsesEbPack(sb_)) {
      const std::string packs_dir = RepoJoin(repo_path_, "data/packs");
      if (std::filesystem::exists(packs_dir)) {
        for (const auto& ent : std::filesystem::directory_iterator(packs_dir, ec)) {
          if (ec) break;
          if (ent.is_regular_file() && ent.path().extension() == ".ebpack") {
            stats_.orphan_chunks_hint = sb_.critical.chunks_written;
            SetPhase(&sb_, BackupPhase::kAborted);
            return superblock_store_->Commit(sb_);
          }
        }
      }
    }
  }
  return Status::Ok();
}

Status BackupEngine::Open() {
  superblock_store_ =
      std::make_unique<BackupSuperBlockStore>(RepoJoin(repo_path_, "superblock.bin"));
  chunk_store_ =
      std::make_unique<ChunkStore>(RepoJoin(repo_path_, "data/chunks"));
  const Status st = superblock_store_->Load(&sb_);
  if (!st.ok()) return st;
  digest_algo_ = DigestAlgoFromSuperBlock(sb_);
  chunk_store_->SetDigestAlgo(digest_algo_);
  if (RepoUsesPersistentIndex(sb_)) {
    chunk_store_->SetUsePersistentIndex(true);
  }
  if (RepoUsesBalancedDurability(sb_)) {
    chunk_store_->SetDurabilityMode(DurabilityMode::kBalanced);
  }
  if (RepoUsesEbPack(sb_)) {
    chunk_store_->SetUseEbPack(true);
  }
  const Status cs = chunk_store_->Open();
  if (!cs.ok()) return cs;
  if (const Status dict_st = LoadRepoZstdDictionary(repo_path_, &zstd_dict_);
      !dict_st.ok()) {
    return dict_st;
  }
  chunk_store_->SetZstdDictionary(zstd_dict_.empty() ? nullptr : &zstd_dict_);
  phase_ = GetPhase(sb_);
  const bool needs_self_check =
      (phase_ != BackupPhase::kIdle && phase_ != BackupPhase::kComplete &&
       phase_ != BackupPhase::kAborted) ||
      (phase_ == BackupPhase::kIdle && RepoUsesCoalescedMeta(sb_));
  if (needs_self_check) {
    const Status self = StartupSelfCheck();
    if (!self.ok()) return self;
    const Status reload = superblock_store_->Load(&sb_);
    if (!reload.ok()) return reload;
    phase_ = GetPhase(sb_);
  }
  if (phase_ == BackupPhase::kAborted && sb_.critical.chunks_written > 0) {
    stats_.orphan_chunks_hint = sb_.critical.chunks_written;
  }
  RegisterBackupSyncRules(&sync_, this);
  BackupContext ctx{this, phase_};
  return sync_.Dispatch(BackupEvent::kOpen, &ctx);
}

Status BackupEngine::Recover() {
  SetPhase(&sb_, BackupPhase::kAborted);
  const Status st = superblock_store_->Commit(sb_);
  if (!st.ok()) return st;
  phase_ = BackupPhase::kAborted;
  BackupContext ctx{this, phase_};
  return sync_.Dispatch(BackupEvent::kRecover, &ctx);
}

Status BackupEngine::PersistSuperBlock(BackupPhase phase) {
  SetPhase(&sb_, phase);
  phase_ = phase;
  sb_.critical.chunks_written = stats_.chunks_written;
  sb_.critical.bytes_processed = stats_.bytes_processed;
  sb_.ext.last_manifest_crc32 = last_manifest_crc32_;
  std::memcpy(sb_.ext.merkle_root, last_merkle_root_, 32);
  if (RepoUsesCoalescedMeta(sb_) && phase != BackupPhase::kAborted &&
      phase != BackupPhase::kIdle) {
    return Status::Ok();
  }
  return superblock_store_->Commit(sb_);
}

Status BackupEngine::DispatchTransition(BackupEvent event) {
  BackupContext ctx{this, phase_};
  const BackupPhase before = ctx.phase;
  const Status st = sync_.Dispatch(event, &ctx);
  if (!st.ok()) return st;
  if (ctx.phase == BackupPhase::kAborted && before != BackupPhase::kAborted) {
    ++stats_.unexpected_transitions;
    return Status::Internal("invalid state transition");
  }
  phase_ = ctx.phase;
  return Status::Ok();
}

Status BackupEngine::SetupEncryption(const BackupOptions& options) {
  if (!options.use_encryption) return Status::Ok();
  if (options.encryption_password.empty()) {
    return Status::InvalidArgument("encryption password required");
  }
  if (crypto::EnvelopeExists(repo_path_)) {
    const Status unwrap = crypto::UnwrapMasterKeyWithPassword(
        repo_path_, options.encryption_password, content_key_, digest_algo_);
    if (!unwrap.ok()) return unwrap;
  } else {
    const std::string salt_path = RepoJoin(repo_path_, "crypto.salt");
    if (std::filesystem::exists(PathFromUtf8(salt_path))) {
      const Status up = crypto::UpgradeLegacyToEnvelope(
          repo_path_, options.encryption_password, &last_recovery_key_);
      if (!up.ok()) return up;
      const Status unwrap = crypto::UnwrapMasterKeyWithPassword(
          repo_path_, options.encryption_password, content_key_, digest_algo_);
      if (!unwrap.ok()) return unwrap;
    } else {
      const Status created = crypto::CreateEnvelope(
          repo_path_, options.encryption_password, &last_recovery_key_,
          content_key_);
      if (!created.ok()) return created;
    }
  }
  has_content_key_ = true;
  chunk_store_->SetContentKey(content_key_);
  sb_.ext.backup_features |= kBackupFeatureEncrypted;
  return Status::Ok();
}

void BackupEngine::ClearEncryption() {
  std::memset(content_key_, 0, sizeof(content_key_));
  has_content_key_ = false;
  if (chunk_store_) chunk_store_->ClearContentKey();
}

Status BackupEngine::EnsureRepoContentKey(const std::string& password) {
  const std::string salt_path = RepoJoin(repo_path_, "crypto.salt");
  const bool has_salt = std::filesystem::exists(PathFromUtf8(salt_path));
  const bool has_envelope = crypto::EnvelopeExists(repo_path_);
  if (!has_salt && !has_envelope) return Status::Ok();
  if (password.empty()) {
    return Status::InvalidArgument("encrypted repo requires password");
  }
  if (has_content_key_) {
    chunk_store_->ClearContentKey();
    has_content_key_ = false;
  }
  Status key_st;
  if (has_envelope) {
    key_st = crypto::UnwrapMasterKeyWithPassword(repo_path_, password,
                                                 content_key_, digest_algo_);
  } else {
    uint8_t salt[16];
    const Status salt_st = crypto::LoadOrCreateRepoSalt(repo_path_, salt);
    if (!salt_st.ok()) return salt_st;
    key_st = crypto::DeriveContentKey(password, salt, content_key_, digest_algo_);
  }
  if (!key_st.ok()) return key_st;
  has_content_key_ = true;
  chunk_store_->SetContentKey(content_key_);
  return Status::Ok();
}

Status BackupEngine::UnlockRepo(const std::string& password) {
  return EnsureRepoContentKey(password);
}

Status BackupEngine::UnwrapWithRecoveryKey(const std::string& recovery_key) {
  if (recovery_key.empty()) {
    return Status::InvalidArgument("recovery key required");
  }
  if (!crypto::EnvelopeExists(repo_path_)) {
    return Status::NotFound("repo has no crypto.envelope.json");
  }
  if (has_content_key_) {
    chunk_store_->ClearContentKey();
    has_content_key_ = false;
  }
  const Status st = crypto::UnwrapMasterKeyWithRecoveryKey(
      repo_path_, recovery_key, content_key_, digest_algo_);
  if (!st.ok()) return st;
  has_content_key_ = true;
  chunk_store_->SetContentKey(content_key_);
  return Status::Ok();
}

Status BackupEngine::RotatePassword(const std::string& old_password,
                                    const std::string& new_password) {
  if (!crypto::EnvelopeExists(repo_path_)) {
    return Status::NotFound("repo has no envelope; use --encrypt first");
  }
  const Status st =
      crypto::RotateEnvelopePassword(repo_path_, old_password, new_password,
                                     digest_algo_);
  if (!st.ok()) return st;
  return EnsureRepoContentKey(new_password);
}

Status BackupEngine::ScanFiles(const std::string& source_path,
                               const BackupOptions& options,
                               const std::vector<std::string>& extra_roots,
                               const std::vector<plugin::ScanHint>& scan_hints,
                               const winmeta::VssSession* vss) {
  pending_scan_entries_.clear();
  pending_files_.clear();
  pending_meta_entries_.clear();
  scan_issues_.clear();

  ScanHintOptions hint_opts{};
  hint_opts.hints = scan_hints;

  auto walk_root_for = [&](const std::string& logical_root) {
    if (vss && vss->active()) return vss->MapToShadow(logical_root);
    return logical_root;
  };

  auto merge_scan = [&](const std::string& logical_scan_root,
                        const std::string& rebase_root) {
    ScanResult scan_result;
    ScanDirectoryOptions dir_opts{};
    dir_opts.logical_root = logical_scan_root;
    dir_opts.enable_sparse = options.sparse_mode != SparseMode::kOff;
    const std::string walk_root = walk_root_for(logical_scan_root);
    const Status scan_st =
        ScanDirectory(walk_root, &scan_result, &hint_opts, &dir_opts);
    if (!scan_st.ok()) return scan_st;
    for (auto& issue : scan_result.issues) scan_issues_.push_back(std::move(issue));
    for (auto& entry : scan_result.entries) {
      if (logical_scan_root != rebase_root) {
        std::error_code ec;
        const auto rel_base = std::filesystem::relative(
            PathFromUtf8(logical_scan_root), PathFromUtf8(rebase_root), ec);
        if (!ec && !entry.relative_path.empty()) {
          const std::string prefix = PathToUtf8(rel_base);
          if (prefix == ".") {
            /* keep relative_path */
          } else if (prefix.empty()) {
            /* keep */
          } else {
            entry.relative_path = prefix + "/" + entry.relative_path;
          }
        }
      }
      pending_scan_entries_.push_back(std::move(entry));
    }
    return Status::Ok();
  };

  Status st = merge_scan(source_path, source_path);
  if (!st.ok()) return st;
  for (const std::string& root : extra_roots) {
    if (root.empty() || root == source_path) continue;
    st = merge_scan(root, source_path);
    if (!st.ok()) {
      scan_issues_.push_back({root, "extra_scan_root_failed:" + st.message()});
    }
  }

  const Status filter_st =
      ApplyBackupFilter(options.filter, &pending_scan_entries_);
  if (!filter_st.ok()) return filter_st;

  const std::unordered_set<std::string> issue_paths = BuildIssuePathSet(scan_issues_);
  for (const auto& entry : pending_scan_entries_) {
    if (entry.efs_encrypted) {
      scan_issues_.push_back({entry.absolute_path, "efs_encrypted_skipped"});
    }
    if (entry.needs_chunking()) {
      if (issue_paths.count(entry.absolute_path) > 0) {
#ifdef _WIN32
        if (entry.reparse_tag != 0) {
          pending_meta_entries_.push_back(entry.ToManifestSkeleton());
        }
#endif
        continue;
      }
      pending_files_.push_back(entry.absolute_path);
    } else {
      pending_meta_entries_.push_back(entry.ToManifestSkeleton());
    }
  }
#ifdef _WIN32
  for (const auto& entry : pending_scan_entries_) {
    if (entry.reparse_tag == 0) continue;
    const ManifestFileEntry sk = entry.ToManifestSkeleton();
    const bool already = std::any_of(
        pending_meta_entries_.begin(), pending_meta_entries_.end(),
        [&](const ManifestFileEntry& m) { return m.relative_path == sk.relative_path; });
    if (!already) pending_meta_entries_.push_back(sk);
  }
#endif
#ifdef _WIN32
  if (options.efs_export_keys) {
    for (auto& meta : pending_meta_entries_) {
      if (!meta.efs_encrypted) continue;
      std::string abs_path;
      for (const auto& se : pending_scan_entries_) {
        if (se.relative_path == meta.relative_path) {
          abs_path = se.absolute_path;
          break;
        }
      }
      if (abs_path.empty()) continue;
      std::string blob;
      const Status ex = winmeta::ExportEfsKeyBlob(abs_path, &blob);
      if (ex.ok()) {
        meta.efs_key_blob_b64 = std::move(blob);
      } else {
        scan_issues_.push_back({abs_path, "efs_key_export_failed"});
      }
    }
  }
#endif
  EmitProgress(50);
  return Status::Ok();
}

Status BackupEngine::MergeMetaManifestEntries() {
  pending_manifest_.erase(
      std::remove_if(pending_manifest_.begin(), pending_manifest_.end(),
                     [](const ManifestFileEntry& e) { return e.relative_path.empty(); }),
      pending_manifest_.end());
  for (auto& mf : pending_manifest_) {
    for (const auto& se : pending_scan_entries_) {
      if (se.relative_path == mf.relative_path) {
        mf.file_type = se.type;
        mf.mode = se.mode;
        mf.uid = se.uid;
        mf.gid = se.gid;
        mf.mtime_unix = se.mtime_unix;
        mf.atime_unix = se.atime_unix;
        mf.security_descriptor_b64 = se.security_descriptor_b64;
        mf.inode_id = se.inode_id;
        mf.reparse_tag = se.reparse_tag;
        mf.reparse_target = se.reparse_target;
        mf.stream_name = se.stream_name;
        mf.efs_encrypted = se.efs_encrypted;
        break;
      }
    }
  }
  pending_manifest_.insert(pending_manifest_.end(), pending_meta_entries_.begin(),
                           pending_meta_entries_.end());
  std::sort(pending_manifest_.begin(), pending_manifest_.end(),
            [](const ManifestFileEntry& a, const ManifestFileEntry& b) {
              return a.relative_path < b.relative_path;
            });
  auto better_entry = [](const ManifestFileEntry& keep,
                         const ManifestFileEntry& cand) -> bool {
    if (cand.reparse_tag != 0 && keep.reparse_tag == 0) return true;
    if (keep.reparse_tag != 0 && cand.reparse_tag == 0) return false;
    if (cand.has_win_meta() && !keep.has_win_meta()) return true;
    if (keep.has_win_meta() && !cand.has_win_meta()) return false;
    return !keep.chunk_hashes_hex.empty() && cand.chunk_hashes_hex.empty();
  };
  std::vector<ManifestFileEntry> deduped;
  deduped.reserve(pending_manifest_.size());
  for (const auto& entry : pending_manifest_) {
    if (deduped.empty() || deduped.back().relative_path != entry.relative_path) {
      deduped.push_back(entry);
      continue;
    }
    if (better_entry(deduped.back(), entry)) {
      deduped.back() = entry;
    }
  }
  pending_manifest_ = std::move(deduped);
  return Status::Ok();
}

Status BackupEngine::LoadPriorManifest(ManifestDocument* out) const {
  const std::string path = RepoJoin(repo_path_, "manifest");
  if (!std::filesystem::exists(path)) {
    return Status::NotFound("prior manifest not found");
  }
  return ReadManifestAuto(path, out);
}

Status BackupEngine::ChunkPendingFiles(BackupMode mode,
                                       const BackupOptions& options) {
  pending_chunks_.clear();
  pending_cfi_.clear();
  pending_manifest_.clear();
  pending_file_bytes_.clear();
  const size_t file_count = pending_files_.size();
  pending_chunks_.resize(file_count);
  pending_cfi_.resize(file_count);
  pending_file_bytes_.resize(file_count);
  pending_manifest_.resize(file_count);

  ManifestDocument prior;
  if (mode == BackupMode::kIncremental) {
    const Status st = LoadPriorManifest(&prior);
    if (!st.ok()) return st;
  }

  struct InodeStreamKey {
    uint64_t inode_id{0};
    std::string stream_name;
    bool operator==(const InodeStreamKey& o) const {
      return inode_id == o.inode_id && stream_name == o.stream_name;
    }
  };
  struct InodeStreamHash {
    size_t operator()(const InodeStreamKey& k) const {
      return std::hash<uint64_t>{}(k.inode_id) ^
             (std::hash<std::string>{}(k.stream_name) << 1);
    }
  };
  std::unordered_map<InodeStreamKey, size_t, InodeStreamHash> inode_canonical_idx;
  std::vector<size_t> alias_of(file_count, static_cast<size_t>(-1));

  EbHcrboConfig hcrbo_cfg{};
  hcrbo_cfg.digest_algo = digest_algo_;
  for (size_t i = 0; i < file_count; ++i) {
    MaybeAdaptBackupWindow();
    if (window_truncated_) {
      TruncatePendingFilesAt(i);
      scan_issues_.push_back({"", "window_truncated"});
      break;
    }
    const auto& file_path = pending_files_[i];
    std::string rel_final;
    const Status rel_st =
        RelativePathFromRoot(source_root_, file_path, &rel_final);
    if (!rel_st.ok()) return rel_st;

    uint64_t expected_size = 0;
    uint64_t inode_id = 0;
    std::string stream_name;
    bool file_sparse = false;
    std::vector<winmeta::SparseRun> sparse_runs;
    for (const auto& se : pending_scan_entries_) {
      if (se.absolute_path == file_path) {
        expected_size = se.size;
        inode_id = se.inode_id;
        stream_name = se.stream_name;
        file_sparse = se.sparse;
        sparse_runs = se.sparse_runs;
        break;
      }
    }

    ManifestFileEntry entry;
    entry.relative_path = rel_final;
    entry.size = expected_size;
    entry.file_type = FileType::kRegular;
    for (const auto& se : pending_scan_entries_) {
      if (se.absolute_path == file_path) {
        entry.mode = se.mode;
        entry.uid = se.uid;
        entry.gid = se.gid;
        entry.mtime_unix = se.mtime_unix;
        entry.atime_unix = se.atime_unix;
        entry.sparse = se.sparse;
        for (const auto& r : se.sparse_runs) {
          entry.sparse_runs.push_back({r.offset, r.length});
        }
        break;
      }
    }
    pending_manifest_[i] = std::move(entry);

    if (inode_id != 0) {
      const InodeStreamKey key{inode_id, stream_name};
      const auto found = inode_canonical_idx.find(key);
      if (found != inode_canonical_idx.end()) {
        alias_of[i] = found->second;
        if (file_count > 0) {
          const uint64_t chunk_pct =
              50 + static_cast<uint64_t>((i + 1) * 350 / file_count);
          EmitProgress(chunk_pct);
        }
        continue;
      }
      inode_canonical_idx[key] = i;
    }

    hcrbo_cfg = EbHcrboConfigForFileSize(expected_size, options.chunk_profile,
                                         digest_algo_);
    EbHcrboChunker chunker(hcrbo_cfg);
    EbHcrboStats hstats{};
    Status ch_st;
#ifdef _WIN32
    if (options.sparse_mode != SparseMode::kOff && file_sparse && !sparse_runs.empty()) {
      const CfiIndex history =
          (mode == BackupMode::kIncremental) ? FindPriorCfi(prior, rel_final)
                                             : CfiIndex{};
      ch_st = ChunkSparseFile(&chunker, mode, file_path, sparse_runs, history,
                              &pending_chunks_[i], &pending_cfi_[i], &hstats);
      pending_file_bytes_[i].clear();
    } else
#endif
    {
      pending_file_bytes_[i] = ReadFileBytes(file_path);
      const auto& bytes = pending_file_bytes_[i];
      if (bytes.empty() && expected_size > 0) {
        scan_issues_.push_back({file_path, "unreadable"});
        pending_manifest_[i].relative_path.clear();
        continue;
      }
      if (mode == BackupMode::kIncremental) {
        const CfiIndex history = FindPriorCfi(prior, rel_final);
        ch_st = chunker.ChunkIncremental(bytes.data(), bytes.size(), history,
                                         &pending_chunks_[i], &pending_cfi_[i],
                                         &hstats);
      } else {
        ch_st = chunker.ChunkFull(bytes.data(), bytes.size(), &pending_chunks_[i],
                                  &pending_cfi_[i], &hstats);
      }
      if (!ch_st.ok()) return ch_st;
      PopulateAnchorChecksums(bytes.data(), bytes.size(), &pending_cfi_[i]);
    }
    if (!ch_st.ok()) return ch_st;
    stats_.chunks_reused_from_cfi += hstats.chunks_reused_from_cfi;

    pending_manifest_[i].size = expected_size > 0 ? expected_size : pending_manifest_[i].size;
    pending_manifest_[i].cfi = pending_cfi_[i];

    if (file_count > 0) {
      const uint64_t chunk_pct = 50 + static_cast<uint64_t>((i + 1) * 350 / file_count);
      EmitProgress(chunk_pct);
    }
  }

  for (size_t i = 0; i < file_count; ++i) {
    if (alias_of[i] == static_cast<size_t>(-1)) continue;
    const size_t canon = alias_of[i];
    pending_chunks_[i] = pending_chunks_[canon];
    pending_cfi_[i] = pending_cfi_[canon];
    pending_file_bytes_[i] = pending_file_bytes_[canon];
    pending_manifest_[i].cfi = pending_cfi_[i];
    pending_manifest_[i].size = pending_manifest_[canon].size;
    pending_manifest_[i].sparse = pending_manifest_[canon].sparse;
    pending_manifest_[i].sparse_runs = pending_manifest_[canon].sparse_runs;
  }
  return Status::Ok();
}

Status BackupEngine::StorePendingChunks(const BackupOptions& options) {
  if (has_content_key_) chunk_store_->SetContentKey(content_key_);
  chunk_store_->SetDurabilityMode(options.durability);
  const size_t file_count = pending_chunks_.size();
  for (size_t fi = 0; fi < file_count; ++fi) {
    auto& entry = pending_manifest_[fi];
    if (entry.relative_path.empty()) continue;
    const auto& bytes = pending_file_bytes_[fi];
    auto& chunks = pending_chunks_[fi];
    ChunkStorePutOptions put_opts = BuildPutOptions(
        options, entry.relative_path, &stats_.content_class,
        zstd_dict_.empty() ? nullptr : &zstd_dict_, &zstd_dict_trainer_);
    if (has_content_key_) put_opts.content_key = content_key_;
    entry.chunk_hashes_hex.clear();
    for (const auto& chunk : chunks) {
      entry.chunk_hashes_hex.push_back(BytesToHex(chunk.hash, 32));
      if (entry.sparse) entry.sparse_chunk_offsets.push_back(chunk.offset);
      if (chunk.reused_from_cfi && chunk_store_->Exists(chunk.hash)) {
        ++stats_.chunks_reused;
        continue;
      }
      std::vector<uint8_t> slice;
      const uint8_t* ptr = nullptr;
      if (entry.sparse) {
#ifdef _WIN32
        const Status rd = ReadFileRangeBytes(pending_files_[fi], chunk.offset,
                                             chunk.length, &slice);
        if (!rd.ok()) return rd;
        ptr = slice.data();
#else
        return Status::InvalidArgument("sparse restore unsupported on this platform");
#endif
      } else {
        ptr = bytes.data() + chunk.offset;
      }
      bool newly_written = false;
      const Status put_st = chunk_store_->PutKnownHash(
          ptr, chunk.length, chunk.hash, &newly_written, &put_opts);
      if (!put_st.ok()) return put_st;
      if (newly_written) {
        ++stats_.chunks_written;
      } else {
        ++stats_.chunks_reused;
      }
    }
    stats_.bytes_processed += bytes.size();
    ++stats_.files_processed;

    if (file_count > 0) {
      const uint64_t store_pct =
          400 + static_cast<uint64_t>((fi + 1) * 550 / file_count);
      EmitProgress(store_pct);
    }
  }
  return Status::Ok();
}

Status BackupEngine::RunPipelineBackup(BackupMode mode,
                                       const BackupOptions& options) {
  constexpr size_t kSequentialPipelineMaxBytes = 32u * 1024u * 1024u;
  if (options.worker_count == 0 && pending_files_.size() == 1) {
    uint64_t total_bytes = 0;
    for (const auto& fp : pending_files_) {
      std::error_code ec;
      total_bytes += std::filesystem::file_size(PathFromUtf8(fp), ec);
    }
    if (total_bytes <= kSequentialPipelineMaxBytes) {
      const Status ch = ChunkPendingFiles(mode, options);
      if (!ch.ok()) return ch;
      return StorePendingChunks(options);
    }
  }

  ManifestDocument prior;
  const ManifestDocument* prior_ptr = nullptr;
  if (mode == BackupMode::kIncremental) {
    const Status st = LoadPriorManifest(&prior);
    if (!st.ok()) return st;
    prior_ptr = &prior;
  }

  BackupPipelineOptions pipe_opts{};
  pipe_opts.use_lz4 = options.use_lz4;
  pipe_opts.compress_mode = options.compress_mode;
  pipe_opts.compress_tier = options.compress_tier;
  pipe_opts.compress_level = options.compress_level;
  pipe_opts.use_zstd_dict = options.use_zstd_dict;
  pipe_opts.cpu_budget_permille = options.cpu_budget_permille;
  pipe_opts.chunk_profile = options.chunk_profile;
  pipe_opts.durability = options.durability;
  pipe_opts.use_encryption = options.use_encryption;
  pipe_opts.content_key = has_content_key_ ? content_key_ : nullptr;
  pipe_opts.content_stats = &stats_.content_class;
  if (options.use_zstd_dict && !zstd_dict_.empty()) {
    pipe_opts.zstd_dict = &zstd_dict_;
  }
  if (options.use_zstd_dict) {
    pipe_opts.dict_trainer = &zstd_dict_trainer_;
  }
  pipe_opts.digest_algo = digest_algo_;
  pipe_opts.queue_depth = 32;
  pipe_opts.worker_count = options.worker_count;
  pipe_opts.store_shard_count = options.store_shard_count;
  pipe_opts.phase_stats = &pipeline_phase_stats_;
  pipe_opts.scan_issues = &scan_issues_;
  BackupPipelineResult pipe_result{};
  const Status pipe_st = RunBackupPipeline(
      pending_files_, source_root_, mode, prior_ptr, chunk_store_.get(),
      &stats_, pipe_opts, &pipe_result, progress_cb_, progress_user_);
  if (!pipe_st.ok()) return pipe_st;
  pending_manifest_ = std::move(pipe_result.manifest_files);
  return Status::Ok();
}

Status BackupEngine::CommitManifestFile() {
  ManifestDocument doc;
  doc.txn_id = sb_.critical.txn_id;
  doc.files = pending_manifest_;
  const std::string temp = RepoJoin(repo_path_, "manifest.new");
  const Status write_st =
      WriteManifestAuto(temp, doc, RepoUsesManifestBinary(sb_));
  if (!write_st.ok()) return write_st;
  std::error_code ec;
  const std::string manifest_path = RepoJoin(repo_path_, "manifest");
  std::filesystem::rename(temp, manifest_path, ec);
  if (ec) return Status::IoError("manifest rename failed: " + ec.message());
  const Status fs = FsyncPath(manifest_path);
  if (!fs.ok()) return fs;

  const Status crc_st = ReadManifestBodyCrc32FromFile(manifest_path, &last_manifest_crc32_);
  if (!crc_st.ok()) return crc_st;
  const Status merkle_st =
      audit::ComputeMerkleRoot(doc, last_merkle_root_, digest_algo_);
  if (!merkle_st.ok()) return merkle_st;
  EmitProgress(960);

  if (RepoUsesSnapshots(sb_)) {
    const Status arch = ArchiveSnapshot(
        repo_path_, doc.txn_id, manifest_path,
        static_cast<int64_t>(std::time(nullptr)), last_manifest_crc32_,
        last_merkle_root_, static_cast<uint32_t>(doc.files.size()));
    if (!arch.ok()) return arch;
  }
  (void)catalog::AppendManifestToPathIndex(repo_path_, doc);
  (void)catalog::AppendManifestBrowseIndex(repo_path_, doc.txn_id, doc.files);
  report::BackupReport br{};
  br.txn_id = doc.txn_id;
  br.backed_up = CountBackedUpManifestFiles(doc.files);
  br.chunks_written = stats_.chunks_written;
  br.chunks_reused = stats_.chunks_reused + stats_.chunks_reused_from_cfi;
  br.bytes_processed = stats_.bytes_processed;
  const uint64_t total_chunks = br.chunks_written + br.chunks_reused;
  if (total_chunks > 0) {
    br.reuse_pct =
        100.0 * static_cast<double>(br.chunks_reused) / static_cast<double>(total_chunks);
  }
  br.issues = scan_issues_;
  br.job_id = active_job_.job_id;
  br.retention_tag = active_job_.retention_tag;
  br.immutable_until_unix = active_job_.immutable_until_unix;
  br.plugins = pending_plugin_reports_;
  br.durability_downgraded = durability_downgraded_;
  br.window_truncated = window_truncated_;
  br.window_end_unix = window_end_unix_;
  br.vss_used = backup_vss_used_;
  if (backup_vss_used_) {
    br.vss_consistency = backup_vss_info_.consistency;
    br.vss_mode = backup_vss_info_.vss_mode;
    br.vss_snapshot_set_id = backup_vss_info_.snapshot_set_id;
    br.vss_volumes = backup_vss_info_.volumes;
    br.vss_cross_volume = backup_vss_info_.cross_volume;
    br.vss_shadow_storage_ok = backup_vss_info_.shadow_storage_ok;
    br.vss_writers.reserve(backup_vss_info_.writers.size());
    for (const auto& w : backup_vss_info_.writers) {
      br.vss_writers.push_back({w.id, w.name, w.state});
    }
  }
  for (const auto& f : doc.files) {
    if (f.sparse && f.has_sparse_meta()) ++br.sparse_file_count;
    if (f.efs_encrypted) ++br.efs_skipped_count;
  }
  if (backup_vss_used_ && !backup_vss_info_.shadow_storage_bytes_by_volume.empty()) {
    br.vss_shadow_storage_bytes = backup_vss_info_.shadow_storage_bytes_by_volume;
  }
  if (!last_recovery_key_.empty()) {
    br.recovery_key_issued = last_recovery_key_;
  }
  pending_plugin_reports_.clear();
  report::PopulateReportIssueCounts(&br);
  (void)report::WriteBackupReport(repo_path_, br);
  if (!active_job_.job_id.empty()) {
    (void)catalog::AppendJobReport(repo_path_, active_job_.job_id, br);
  }
  if (!active_job_.job_id.empty()) {
    catalog::SnapshotMetaRecord sm{};
    sm.txn_id = doc.txn_id;
    sm.job_id = active_job_.job_id;
    sm.retention_tag = active_job_.retention_tag;
    sm.immutable_until_unix = active_job_.immutable_until_unix;
    (void)catalog::AppendSnapshotMeta(repo_path_, sm);
  }
  return Status::Ok();
}

Status BackupEngine::AppendAuditEntry() {
  const std::string chain_path = RepoJoin(repo_path_, "audit/rar.chain");
  audit::RarChainEntry entry{};
  entry.sequence = audit::RarChainNextSequence(chain_path);
  entry.txn_id = sb_.critical.txn_id;
  entry.prev_rar_sha256 = audit::RarChainLastSha256(chain_path);
  entry.generated_at_unix = static_cast<int64_t>(std::time(nullptr));
  char crc_buf[16];
  snprintf(crc_buf, sizeof(crc_buf), "%08x",
           static_cast<unsigned>(last_manifest_crc32_));
  entry.manifest_crc32 = crc_buf;
  entry.merkle_root = BytesToHex(last_merkle_root_, 32);
  entry.body_json = audit::BuildRarBodyJson(sb_.critical.txn_id,
                                            last_manifest_crc32_,
                                            last_merkle_root_);
  entry.rar_sha256 = audit::ComputeRarSha256(entry.body_json, digest_algo_);
  const Status st = audit::AppendRarChainEntry(chain_path, entry);
  if (!st.ok()) return st;

  audit::CarlSignedTreeHead anchor{};
  const std::string anchor_dir = audit::DefaultCarlAnchorDir(repo_path_);
  (void)audit::PublishCarlAnchor(chain_path, anchor_dir, &anchor);

  EmitProgress(1000);
  return Status::Ok();
}

Status BackupEngine::VerifyManifestDocument(const ManifestDocument& doc,
                                            uint64_t snapshot_txn_id,
                                            bool verify_deep_content) {
  if (snapshot_txn_id == 0) {
    if (sb_.critical.txn_id != 0 && doc.txn_id != sb_.critical.txn_id) {
      return Status::Corrupt("manifest txn_id mismatch with superblock");
    }
  }

  for (const auto& file : doc.files) {
    if (file.file_type != FileType::kRegular) {
      if (!file.chunk_hashes_hex.empty()) {
        return Status::Corrupt("non-regular file has chunks: " + file.relative_path);
      }
      continue;
    }
    if (file.chunk_hashes_hex.empty() && file.size > 0) {
      if (file.efs_encrypted) continue;
      return Status::Corrupt("empty chunk list for non-empty file: " +
                             file.relative_path);
    }
    uint64_t total = 0;
    for (const auto& hex : file.chunk_hashes_hex) {
      uint8_t hash[32];
      if (!HexToBytes(hex, hash, 32)) {
        return Status::Corrupt("invalid chunk hash in manifest");
      }
      std::vector<uint8_t> payload;
      const Status st = chunk_store_->Get(hash, &payload);
      if (!st.ok()) return st;
      if (verify_deep_content) {
        uint8_t actual[32];
        ContentHash(digest_algo_, payload.data(), payload.size(), actual);
        if (std::memcmp(actual, hash, 32) != 0) {
          return Status::Corrupt("chunk content hash mismatch for " +
                                 file.relative_path);
        }
      }
      total += payload.size();
    }
    if (file.sparse && file.has_sparse_meta()) {
      uint64_t run_total = 0;
      for (const auto& run : file.sparse_runs) run_total += run.second;
      if (total != run_total) {
        return Status::Corrupt("manifest sparse data size mismatch for " +
                               file.relative_path);
      }
    } else if (total != file.size) {
      return Status::Corrupt("manifest file size mismatch for " +
                             file.relative_path);
    }
  }

  uint8_t merkle[32];
  const Status merkle_st = audit::ComputeMerkleRoot(doc, merkle, digest_algo_);
  if (!merkle_st.ok()) return merkle_st;

  const std::string chain_path = RepoJoin(repo_path_, "audit/rar.chain");
  if (snapshot_txn_id != 0) {
    if (!std::filesystem::exists(chain_path)) {
      return Status::Corrupt("missing rar chain for historical verify");
    }
    std::vector<audit::RarChainEntry> entries;
    const Status read_st = audit::ReadRarChainEntries(chain_path, &entries);
    if (!read_st.ok()) return read_st;
    const audit::RarChainEntry* match = nullptr;
    for (const auto& e : entries) {
      if (e.txn_id == snapshot_txn_id) {
        match = &e;
        break;
      }
    }
    if (!match) {
      return Status::NotFound("rar chain entry not found for snapshot txn");
    }
    if (match->merkle_root != BytesToHex(merkle, 32)) {
      return Status::Corrupt("historical merkle mismatch with rar chain");
    }
    uint32_t body_crc = 0;
    const Status crc_st = ReadManifestBodyCrc32FromFile(
        SnapshotManifestPath(repo_path_, snapshot_txn_id), &body_crc);
    if (!crc_st.ok()) return crc_st;
    char crc_buf[16];
    snprintf(crc_buf, sizeof(crc_buf), "%08x", static_cast<unsigned>(body_crc));
    if (match->manifest_crc32 != crc_buf) {
      return Status::Corrupt("historical manifest crc mismatch with rar chain");
    }
    return Status::Ok();
  }

  if (std::memcmp(merkle, sb_.ext.merkle_root, 32) != 0 &&
      sb_.ext.merkle_root[0] != 0) {
    return Status::Corrupt("merkle root mismatch with superblock");
  }

  if (std::filesystem::exists(chain_path)) {
    audit::RarChainVerifyReport report{};
    const Status chain_st = audit::VerifyRarChain(chain_path, &report, digest_algo_);
    if (!chain_st.ok()) return chain_st;
    if (!report.consistent) {
      return Status::Corrupt("rar chain inconsistent");
    }
    audit::RarChainEntry last{};
    bool found = false;
    const Status last_st =
        audit::ReadLastRarChainEntry(chain_path, &last, &found);
    if (!last_st.ok()) return last_st;
    if (found) {
      if (last.merkle_root != BytesToHex(merkle, 32)) {
        return Status::Corrupt("rar chain merkle mismatch");
      }
      if (last.txn_id != doc.txn_id) {
        return Status::Corrupt("rar chain txn_id mismatch");
      }
    }
  }
  return Status::Ok();
}

Status BackupEngine::RunJob(const std::string& job_id, BackupMode mode,
                            const BackupOptions& options) {
  if (job_id.empty()) return Status::InvalidArgument("job_id empty");
  job::BackupJob job{};
  const Status job_st = job::GetJob(repo_path_, job_id, &job);
  if (!job_st.ok()) return job_st;

  if (options.respect_job_windows && job::HasBackupWindow(job.window) &&
      !job::IsWithinBackupWindow(std::time(nullptr), job.window)) {
    return Status::Conflict("outside backup window");
  }

  BackupOptions opts = options;
  opts.job_id = job_id;
  opts.window = job.window;
  for (const auto& path : job.exclude_paths) {
    opts.filter.exclude_paths.push_back(path);
  }
  for (const auto& glob : job.exclude_globs) {
    opts.filter.exclude_globs.push_back(glob);
  }
  if (opts.plugins.empty()) {
    opts.plugins = job.plugins;
  }
  if (job.use_vss) {
    opts.use_vss = true;
    if (!job.vss_mode.empty()) {
      (void)winmeta::ParseVssConsistencyMode(job.vss_mode, &opts.vss_mode);
    }
    opts.vss_fallback_live = opts.vss_fallback_live || job.vss_fallback_live;
    opts.vss_include_junction_volumes = job.vss_include_junction_volumes;
  }
  opts.quiesce_profile = job.quiesce_profile;
  opts.post_backup_webhook_url = job.post_backup_webhook_url;
  if (!job.quiesce_profile.empty() && job.quiesce_profile != "none") {
    opts.use_vss = true;
    if (job.quiesce_profile == "sql" || job.quiesce_profile == "exchange" ||
        job.quiesce_profile == "system" || job.quiesce_profile == "custom") {
      opts.vss_mode = winmeta::VssConsistencyMode::kApp;
    }
  }
  if (job.vss_app_failure_policy == "fail_job") {
    opts.vss_app_failure_policy = BackupOptions::VssAppFailurePolicy::kFailJob;
  } else if (job.vss_app_failure_policy == "fallback_live") {
    opts.vss_app_failure_policy =
        BackupOptions::VssAppFailurePolicy::kFallbackLive;
  }

  active_job_.job_id = job.id;
  active_job_.retention_tag = job.retention_tag;
  active_job_.immutable_until_unix = 0;
  if (job.immutability_days > 0) {
    active_job_.immutable_until_unix =
        static_cast<int64_t>(std::time(nullptr)) +
        static_cast<int64_t>(job.immutability_days) * 86400;
  }

  const Status run_st = RunBackup(job.source_path, mode, opts);
  active_job_ = {};
  return run_st;
}

Status BackupEngine::RunBackup(const std::string& source_path, BackupMode mode,
                               const BackupOptions& options) {
  if (!std::filesystem::exists(PathFromUtf8(source_path))) {
    return Status::NotFound("source path not found");
  }
  if (mode == BackupMode::kIncremental) {
    ManifestDocument prior;
    const Status prior_st = LoadPriorManifest(&prior);
    if (!prior_st.ok()) return prior_st;
  }

  stats_ = BackupStats{};
  pending_files_.clear();
  pending_file_bytes_.clear();
  pending_chunks_.clear();
  pending_cfi_.clear();
  pending_manifest_.clear();
  pending_scan_entries_.clear();
  pending_meta_entries_.clear();
  scan_issues_.clear();
  pending_plugin_reports_.clear();
  source_root_ = source_path;
  backup_vss_used_ = false;
  backup_vss_info_ = {};
  ResetBackupWindowState();

  sb_.critical.txn_id = GetNextTxnId(sb_);
  chunk_store_->SetTxnId(sb_.critical.txn_id);
  const BackupOptions effective = ResolveBackupOptions(sb_, options);
  InitBackupWindow(effective.window);
  if (effective.respect_job_windows && job::HasBackupWindow(active_window_) &&
      !job::IsWithinBackupWindow(std::time(nullptr), active_window_)) {
    return Status::Conflict("outside backup window");
  }
  if (effective.compress_mode == CompressMode::kAuto) {
    sb_.ext.default_codec = kDefaultCodecAuto;
  } else if (effective.compress_mode == CompressMode::kZstd) {
    sb_.ext.default_codec = kDefaultCodecZstd;
  } else if (effective.use_lz4 ||
             effective.compress_mode == CompressMode::kLz4) {
    sb_.ext.default_codec = kDefaultCodecLz4;
  }
  const bool standard_digest = RepoUsesStandardDigest(sb_);
  const bool persistent_index = RepoUsesPersistentIndex(sb_);
  const bool manifest_binary = RepoUsesManifestBinary(sb_);
  const bool snapshots = RepoUsesSnapshots(sb_);
  const bool ebpack = RepoUsesEbPack(sb_);
  const bool coalesced_meta = RepoUsesCoalescedMeta(sb_);
  const bool repo_immutable = RepoUsesImmutable(sb_);
  sb_.ext.backup_features = 0;
  if (standard_digest) {
    sb_.ext.backup_features |= kBackupFeatureDigestStandard;
  }
  if (persistent_index) {
    sb_.ext.backup_features |= kBackupFeaturePersistentIndex;
  }
  if (manifest_binary) {
    sb_.ext.backup_features |= kBackupFeatureManifestBinary;
  }
  if (snapshots) {
    sb_.ext.backup_features |= kBackupFeatureSnapshots;
  }
  if (ebpack) {
    sb_.ext.backup_features |= kBackupFeatureEbPack;
    chunk_store_->SetUseEbPack(true);
  }
  if (coalesced_meta) {
    sb_.ext.backup_features |= kBackupFeatureCoalescedMeta;
  }
  if (repo_immutable) {
    sb_.ext.backup_features |= kBackupFeatureImmutable;
  }
  if (effective.durability == DurabilityMode::kBalanced) {
    sb_.ext.backup_features |= kBackupFeatureBalancedDurability;
    chunk_store_->SetDurabilityMode(DurabilityMode::kBalanced);
  } else {
    chunk_store_->SetDurabilityMode(DurabilityMode::kStrict);
  }
  chunk_store_->SetDeferPersistentIndexSave(persistent_index);
  zstd_dict_trainer_.Reset();
  if (effective.use_zstd_dict) {
    chunk_store_->SetZstdDictionary(zstd_dict_.empty() ? nullptr : &zstd_dict_);
  } else {
    chunk_store_->SetZstdDictionary(nullptr);
  }

  const bool profile = PipelineProfileEnabled();
  if (effective.use_pipeline || profile) {
    ResetPipelinePhaseStats(&pipeline_phase_stats_);
  }
  const auto backup_t0 = std::chrono::steady_clock::now();

  Status st = SetupEncryption(options);
  if (!st.ok()) return st;

  plugin::PluginSession plugin_session(source_path, effective.plugins);
  (void)plugin_session.QuiesceAll(&scan_issues_);
  std::vector<std::string> extra_scan_roots;
  std::vector<plugin::ScanHint> plugin_scan_hints;
  plugin_session.CollectExtraScanRoots(&extra_scan_roots);
  plugin_session.CollectScanHints(&plugin_scan_hints);

  if (!effective.pre_backup_cmd.empty()) {
    int hook_rc = 0;
    st = RunShellCommand(effective.pre_backup_cmd, &hook_rc);
    if (!st.ok()) {
      (void)PersistSuperBlock(BackupPhase::kAborted);
      return st;
    }
    if (hook_rc != 0) {
      scan_issues_.push_back({"", "hook_failed:pre:" + std::to_string(hook_rc)});
      (void)PersistSuperBlock(BackupPhase::kAborted);
      return Status::Internal("pre_backup_cmd failed with exit code " +
                              std::to_string(hook_rc));
    }
  }

  winmeta::VssSession vss;
  bool vss_active = false;
  VssReleaseGuard vss_guard(&vss, &vss_active, &scan_issues_, &backup_vss_info_);
  if (effective.use_vss) {
    std::vector<std::string> vss_roots;
    vss_roots.push_back(source_path);
    for (const std::string& root : extra_scan_roots) {
      if (!root.empty()) vss_roots.push_back(root);
    }
    winmeta::VssBeginOptions vss_opts{};
    vss_opts.mode = effective.vss_mode;
    vss_opts.include_junction_volumes = effective.vss_include_junction_volumes;
    st = vss.Begin(vss_roots, vss_opts);
    if (!st.ok()) {
      if (effective.vss_fallback_live) {
        const std::string& msg = st.message();
        if (msg.find("insufficient shadow") != std::string::npos ||
            msg.find("shadow storage") != std::string::npos) {
          scan_issues_.push_back({"", "vss_shadow_storage_low:" + msg});
        } else {
          scan_issues_.push_back({"", "vss_unavailable:" + msg});
        }
      } else {
        (void)PersistSuperBlock(BackupPhase::kAborted);
        return st;
      }
    } else {
      vss_active = true;
      backup_vss_used_ = true;
      backup_vss_info_ = vss.info();
    }
  }

  st = DispatchTransition(BackupEvent::kScanFile);
  if (!st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return st;
  }
  st = PersistSuperBlock(BackupPhase::kScanning);
  if (!st.ok()) return st;
  st = MaybeTestAbortAfter(BackupPhase::kScanning);
  if (!st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return st;
  }

  const auto scan_t0 = std::chrono::steady_clock::now();
  st = ScanFiles(source_path, effective, extra_scan_roots, plugin_scan_hints,
                 vss_active ? &vss : nullptr);
  if (profile) {
    const auto scan_t1 = std::chrono::steady_clock::now();
    pipeline_phase_stats_.scan_ns.fetch_add(
        static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(scan_t1 - scan_t0)
                .count()),
        std::memory_order_relaxed);
  }
  if (!st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return st;
  }

  if (options.use_encryption) {
    sb_.ext.backup_features |= kBackupFeatureEncrypted;
  }
  if (!pending_meta_entries_.empty()) {
    sb_.ext.backup_features |= kBackupFeatureSpecialFiles;
  }
  for (const auto& e : pending_scan_entries_) {
    if (e.mode != 0 || e.mtime_unix != 0 || e.atime_unix != 0) {
      sb_.ext.backup_features |= kBackupFeatureMeta;
      break;
    }
  }

  st = DispatchTransition(BackupEvent::kChunkFile);
  if (!st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return st;
  }
  st = PersistSuperBlock(BackupPhase::kChunking);
  if (!st.ok()) return st;
  st = MaybeTestAbortAfter(BackupPhase::kChunking);
  if (!st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return st;
  }

  chunk_store_->SetDurabilityMode(effective.durability);
  st = chunk_store_->BeginAppendSession();
  if (!st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return st;
  }

  if (effective.use_pipeline) {
    st = RunPipelineBackup(mode, effective);
  } else {
    st = ChunkPendingFiles(mode, effective);
  }
  if (!st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return st;
  }

  st = DispatchTransition(BackupEvent::kStoreChunk);
  if (!st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return st;
  }
  st = PersistSuperBlock(BackupPhase::kStoring);
  if (!st.ok()) return st;
  st = MaybeTestAbortAfter(BackupPhase::kStoring);
  if (!st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return st;
  }

  if (!effective.use_pipeline) {
    st = StorePendingChunks(effective);
    if (!st.ok()) {
      (void)PersistSuperBlock(BackupPhase::kAborted);
      return st;
    }
  }

  vss_guard.release();
  if (backup_vss_used_ &&
      effective.vss_mode == winmeta::VssConsistencyMode::kAuto &&
      backup_vss_info_.consistency == "crash" &&
      !backup_vss_info_.writers.empty()) {
    scan_issues_.push_back({"", "vss_writer_degraded"});
    if (effective.vss_app_failure_policy ==
        BackupOptions::VssAppFailurePolicy::kFailJob) {
      (void)PersistSuperBlock(BackupPhase::kAborted);
      return Status::Internal("vss app writer failure policy fail_job");
    }
    if (effective.vss_app_failure_policy ==
        BackupOptions::VssAppFailurePolicy::kFallbackLive) {
      scan_issues_.push_back({"", "vss_app_fallback_live"});
    }
  }

  const auto flush_t0 = std::chrono::steady_clock::now();
  std::future<Status> merge_future =
      std::async(std::launch::async, [this] { return MergeMetaManifestEntries(); });
  st = chunk_store_->Flush();
  if (profile) {
    const auto flush_t1 = std::chrono::steady_clock::now();
    pipeline_phase_stats_.flush_ns.fetch_add(
        static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(flush_t1 - flush_t0)
                .count()),
        std::memory_order_relaxed);
  }
  if (!st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return st;
  }
  st = chunk_store_->EndAppendSession();
  if (!st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return st;
  }

  st = merge_future.get();
  if (!st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return st;
  }

  st = DispatchTransition(BackupEvent::kCommitManifest);
  if (!st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return st;
  }
  st = PersistSuperBlock(BackupPhase::kCommittingMeta);
  if (!st.ok()) return st;
  st = MaybeTestAbortAfter(BackupPhase::kCommittingMeta);
  if (!st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return st;
  }

  const auto meta_t0 = std::chrono::steady_clock::now();
  pending_plugin_reports_ = plugin_session.PluginReportJsonFragments();
  st = CommitManifestFile();
  if (!st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return st;
  }

  plugin_session.EndQuiesce();

  if (!effective.post_backup_cmd.empty()) {
    int hook_rc = 0;
    const Status post_st = RunShellCommand(effective.post_backup_cmd, &hook_rc);
    const bool post_failed = !post_st.ok() || hook_rc != 0;
    if (!post_st.ok()) {
      scan_issues_.push_back({"", "hook_failed:post:" + post_st.message()});
    } else if (hook_rc != 0) {
      scan_issues_.push_back({"", "hook_failed:post:" + std::to_string(hook_rc)});
    }
    if (post_failed) {
      report::BackupReport br{};
      if (report::LoadBackupReport(repo_path_, sb_.critical.txn_id, &br).ok()) {
        br.issues = scan_issues_;
        report::PopulateReportIssueCounts(&br);
        (void)report::WriteBackupReport(repo_path_, br);
      }
    }
  }

  if (!effective.post_backup_webhook_url.empty()) {
    report::BackupReport br{};
    if (report::LoadBackupReport(repo_path_, sb_.critical.txn_id, &br).ok()) {
      const std::string json = report::BackupReportToJson(br);
      const std::string body_path = RepoJoin(repo_path_, "reports/webhook_body.tmp");
      std::ofstream body_out(PathFromUtf8(body_path), std::ios::trunc);
      if (body_out) {
        body_out << json;
        body_out.close();
        const std::string cmd =
            "powershell -NoProfile -Command \"Invoke-RestMethod -Uri '" +
            effective.post_backup_webhook_url +
            "' -Method Post -ContentType 'application/json' -InFile '" +
            body_path + "'\"";
        int hook_rc = 0;
        (void)RunShellCommand(cmd, &hook_rc);
      }
    }
  }

  st = DispatchTransition(BackupEvent::kAppendAudit);
  if (!st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return st;
  }
  st = PersistSuperBlock(BackupPhase::kAuditing);
  if (!st.ok()) return st;

  st = AppendAuditEntry();
  if (profile) {
    const auto meta_t1 = std::chrono::steady_clock::now();
    pipeline_phase_stats_.meta_ns.fetch_add(
        static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(meta_t1 - meta_t0)
                .count()),
        std::memory_order_relaxed);
  }
  if (!st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return st;
  }

  st = DispatchTransition(BackupEvent::kComplete);
  if (!st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return st;
  }
  chunk_store_->SetDeferPersistentIndexSave(false);
  if (persistent_index) {
    st = chunk_store_->SavePersistentIndex();
    if (!st.ok()) {
      (void)PersistSuperBlock(BackupPhase::kAborted);
      return st;
    }
  }
  SetNextTxnId(&sb_, sb_.critical.txn_id + 1);
  if (profile) {
    const auto backup_t1 = std::chrono::steady_clock::now();
    const double total_sec =
        std::chrono::duration<double>(backup_t1 - backup_t0).count();
    PrintPipelinePhaseStats(pipeline_phase_stats_, total_sec);
  }

  if (const Status dict_st = FinalizeCompressionArtifacts(effective); !dict_st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return dict_st;
  }

  return PersistSuperBlock(BackupPhase::kIdle);
}

Status BackupEngine::FinalizeCompressionArtifacts(const BackupOptions& options) {
  if (!options.use_zstd_dict) return Status::Ok();
  ZstdDictionary trained;
  const Status st = zstd_dict_trainer_.FinalizeAndSave(repo_path_, &trained);
  if (!st.ok()) return st;
  if (!trained.empty()) {
    zstd_dict_.ReplaceWith(std::move(trained));
    chunk_store_->SetZstdDictionary(&zstd_dict_);
  }
  return Status::Ok();
}

Status BackupEngine::Verify(const BackupOptions& options) {
  const Status key_st = EnsureRepoContentKey(options.encryption_password);
  if (!key_st.ok()) return key_st;

  ManifestDocument doc;
  Status rd;
  if (options.snapshot_txn_id != 0) {
    rd = LoadSnapshotManifest(repo_path_, options.snapshot_txn_id, &doc);
  } else {
    const std::string manifest_path = RepoJoin(repo_path_, "manifest");
    if (!std::filesystem::exists(manifest_path)) {
      if (sb_.critical.txn_id == 0) return Status::Ok();
      return Status::IoError("cannot open manifest: " + manifest_path);
    }
    rd = ReadManifestAuto(manifest_path, &doc);
  }
  if (!rd.ok()) return rd;

  bool verify_deep = options.verify_deep_content;
  if (const char* env = std::getenv("EBBACKUP_VERIFY_DEEP")) {
    if (env[0] == '1') verify_deep = true;
  }

  const Status deep =
      VerifyManifestDocument(doc, options.snapshot_txn_id, verify_deep);
  if (!deep.ok()) return deep;

  const std::string chain_path = RepoJoin(repo_path_, "audit/rar.chain");
  if (options.require_anchor || std::getenv("EBBACKUP_AUDIT_KEY") != nullptr ||
      !options.audit_key.empty()) {
    const Status anchor_st = audit::VerifyCarlAnchorRequired(
        chain_path, audit::DefaultCarlAnchorDir(repo_path_),
        options.require_anchor,
        options.audit_key.empty() ? nullptr : &options.audit_key);
    if (!anchor_st.ok()) return anchor_st;
  }

  BackupContext ctx{this, phase_};
  return sync_.Dispatch(BackupEvent::kVerify, &ctx);
}

Status BackupEngine::Restore(const std::string& dest_path,
                             const RestoreOptions& options) {
  const Status key_st = EnsureRepoContentKey(options.encryption_password);
  if (!key_st.ok()) return key_st;
  RestoreEngine restore(repo_path_, chunk_store_.get());
  RestoreOptions opts = options;
  opts.acceptance_out = &last_restore_acceptance_;
  const Status st = restore.RunRestore(dest_path, opts);
  has_restore_acceptance_ = st.ok();
  return st;
}

Status BackupEngine::PreviewRestore(uint64_t snapshot_txn_id,
                                    const RestoreOptions& options,
                                    RestorePreviewReport* out) const {
  if (!out) return Status::InvalidArgument("out is null");
  ManifestDocument doc;
  const Status rd = LoadManifest(snapshot_txn_id, &doc);
  if (!rd.ok()) return rd;
  std::vector<ManifestFileEntry> files = doc.files;
  if (options.filter.HasAnyFilter()) {
    std::vector<ManifestFileEntry> filtered;
    const Status filter_st = ApplyManifestFilter(options.filter, files, &filtered);
    if (!filter_st.ok()) return filter_st;
    files = std::move(filtered);
  }
  out->file_count = 0;
  out->dir_count = 0;
  out->total_bytes = 0;
  for (const auto& file : files) {
    if (file.file_type == FileType::kDirectory) {
      ++out->dir_count;
      continue;
    }
    if (file.file_type == FileType::kRegular) {
      ++out->file_count;
      out->total_bytes += file.size;
    }
  }
  return Status::Ok();
}

Status BackupEngine::PreviewInPlaceRestore(
    uint64_t snapshot_txn_id, const std::string& target_root,
    const RestoreOptions& options, const restore::InPlacePreviewOptions& preview_opts,
    restore::InPlacePreviewReport* out) const {
  if (!out) return Status::InvalidArgument("out is null");
  RestoreOptions opts = options;
  uint64_t txn = snapshot_txn_id;
  if (txn == 0) txn = opts.snapshot_txn_id;
  if (txn == 0) txn = sb_.critical.txn_id;
  opts.snapshot_txn_id = txn;
  return restore::PreviewInPlaceRestore(*this, txn, target_root, opts, preview_opts,
                                        out);
}

Status BackupEngine::ApplyInPlaceRestore(
    uint64_t snapshot_txn_id, const std::string& target_root,
    const RestoreOptions& options, const restore::InPlacePreviewOptions& preview_opts,
    const restore::InPlaceApplyOptions& apply_opts,
    restore::InPlaceApplyReport* out) {
  if (!out) return Status::InvalidArgument("out is null");
  const Status key_st = EnsureRepoContentKey(options.encryption_password);
  if (!key_st.ok()) return key_st;
  RestoreOptions opts = options;
  uint64_t txn = snapshot_txn_id;
  if (txn == 0) txn = opts.snapshot_txn_id;
  if (txn == 0) txn = sb_.critical.txn_id;
  opts.snapshot_txn_id = txn;
  return restore::ApplyInPlaceRestore(*this, txn, target_root, opts, preview_opts,
                                      apply_opts, out);
}

Status BackupEngine::GcOrphans(bool dry_run, OrphanGcReport* report,
                               bool latest_manifest_only) {
  OrphanGcReport local{};
  OrphanGcReport* out = report ? report : &local;
  BackupContext ctx{this, phase_};
  const Status disp = sync_.Dispatch(BackupEvent::kGcOrphans, &ctx);
  if (!disp.ok()) return disp;

  const bool use_retained =
      RepoUsesSnapshots(sb_) && !latest_manifest_only;
  std::unordered_set<std::string> referenced;
  Status coll;
  if (use_retained) {
    coll = CollectReferencedHashesRetained(repo_path_, &referenced);
  } else {
    ManifestDocument doc;
    coll = ReadManifestAuto(RepoJoin(repo_path_, "manifest"), &doc);
    if (!coll.ok()) return coll;
    coll = CollectReferencedHashes(doc, &referenced);
  }
  if (!coll.ok()) return coll;
  return ExecuteOrphanGcReferenced(chunk_store_.get(), referenced, dry_run, out);
}

Status BackupEngine::Compact(bool dry_run, CompactReport* report) {
  return CompactChunkStore(repo_path_, dry_run, report);
}

Status BackupEngine::GetRepoStats(RepoStats* out) const {
  return ComputeRepoStats(repo_path_, out);
}

Status BackupEngine::ListSnapshots(std::vector<SnapshotEntry>* out) const {
  return ebbackup::ListSnapshots(repo_path_, out);
}

Status BackupEngine::LoadManifest(uint64_t snapshot_txn_id,
                                  ManifestDocument* out) const {
  if (!out) return Status::InvalidArgument("out is null");
  out->files.clear();
  out->txn_id = 0;
  if (snapshot_txn_id != 0) {
    return LoadSnapshotManifest(repo_path_, snapshot_txn_id, out);
  }
  const std::string manifest_path = RepoJoin(repo_path_, "manifest");
  if (!std::filesystem::exists(PathFromUtf8(manifest_path))) {
    return Status::NotFound("no manifest published");
  }
  return ReadManifestAuto(manifest_path, out);
}

Status BackupEngine::PruneSnapshots(const RetentionPolicy& policy, bool dry_run,
                                    PruneReport* report,
                                    const std::string& audit_key) {
  PruneOptions opts{};
  const std::string key = !audit_key.empty() ? audit_key : audit_key_;
  opts.authorized = !key.empty();
  return ebbackup::PruneSnapshots(repo_path_, policy, dry_run, report, opts);
}

Status BackupEngine::BuildPathIndex(bool full_rebuild) {
  if (full_rebuild) {
    const Status path_st = catalog::BuildPathIndexFromSnapshots(
        repo_path_, [this](uint64_t txn_id, ManifestDocument* out) {
          return LoadManifest(txn_id, out);
        });
    if (!path_st.ok()) return path_st;
    return catalog::BuildManifestBrowseIndexFromSnapshots(
        repo_path_, [this](uint64_t txn_id, std::string* manifest_path) {
          if (!manifest_path) return Status::InvalidArgument("manifest_path is null");
          if (txn_id == 0) {
            *manifest_path = RepoJoin(repo_path_, "manifest");
            return Status::Ok();
          }
          *manifest_path = SnapshotManifestPath(repo_path_, txn_id);
          return Status::Ok();
        });
  }
  ManifestDocument doc;
  const Status st = LoadManifest(0, &doc);
  if (!st.ok()) return st;
  return catalog::AppendManifestToPathIndex(repo_path_, doc);
}

std::string BackupEngine::QueryPathHistoryJson(const std::string& path,
                                               uint64_t offset,
                                               uint64_t limit) const {
  catalog::PathHistoryPage page;
  const Status st = catalog::QueryPathHistoryPage(repo_path_, path, offset, limit, &page);
  if (!st.ok()) {
    return std::string("{\"ok\":false,\"error\":\"") + st.message() + "\"}";
  }
  return catalog::PathHistoryPageToJson(page);
}

std::string BackupEngine::ListManifestFilesPageJson(uint64_t txn_id,
                                                    const std::string& prefix,
                                                    uint64_t offset,
                                                    uint64_t limit) const {
  if (txn_id == 0) {
    ManifestDocument latest;
    const Status latest_st = LoadManifest(0, &latest);
    if (!latest_st.ok()) {
      return std::string("{\"ok\":false,\"error\":\"") + latest_st.message() + "\"}";
    }
    txn_id = latest.txn_id;
  }

  catalog::ManifestFilePage page;
  const std::string index_path = catalog::ManifestBrowseIndexPath(repo_path_, txn_id);
  std::error_code ec;
  if (std::filesystem::exists(PathFromUtf8(index_path), ec)) {
    const Status page_st = catalog::QueryManifestBrowsePage(
        repo_path_, txn_id, prefix, offset, limit, &page);
    if (!page_st.ok()) {
      return std::string("{\"ok\":false,\"error\":\"") + page_st.message() + "\"}";
    }
    return catalog::ManifestPageToJson(page, "sidecar");
  }

  ManifestDocument doc;
  const Status st = LoadManifest(txn_id, &doc);
  if (!st.ok()) {
    return std::string("{\"ok\":false,\"error\":\"") + st.message() + "\"}";
  }
  const Status page_st =
      catalog::ListManifestFilesPage(doc, prefix, offset, limit, &page);
  if (!page_st.ok()) {
    return std::string("{\"ok\":false,\"error\":\"") + page_st.message() + "\"}";
  }
  return catalog::ManifestPageToJson(page, "full_manifest");
}

Status BackupEngine::EnqueueJob(const std::string& job_id, bool incremental,
                                uint32_t flags) {
  if (job_id.empty()) return Status::InvalidArgument("job_id empty");
  queue::JobQueue q(repo_path_);
  const Status load_st = q.Load();
  if (!load_st.ok()) return load_st;
  queue::JobQueueEnqueueOptions opts;
  opts.incremental = incremental;
  opts.flags = flags;
  const Status enq_st = q.Enqueue(job_id, opts);
  if (!enq_st.ok()) return enq_st;
  return q.Save();
}

Status BackupEngine::RunJobQueue(bool drain, const BackupOptions& options) {
  queue::JobQueue q(repo_path_);
  const Status load_st = q.Load();
  if (!load_st.ok()) return load_st;
  for (;;) {
    queue::JobQueueRunReport report;
    const Status run_st = q.RunNext(this, options, &report);
    if (run_st == StatusCode::kNotFound) return Status::Ok();
    if (!run_st.ok()) return run_st;
    if (!drain) return Status::Ok();
  }
}

std::string BackupEngine::JobQueueStatusJson() const {
  queue::JobQueue q(repo_path_);
  const Status load_st = q.Load();
  if (!load_st.ok()) {
    return std::string("{\"ok\":false,\"error\":\"") + load_st.message() + "\"}";
  }
  return q.StatusJson();
}

std::string BackupEngine::DiffSnapshotsJson(uint64_t txn_a, uint64_t txn_b) const {
  ManifestDocument doc_a;
  ManifestDocument doc_b;
  const Status st_a = LoadManifest(txn_a, &doc_a);
  if (!st_a.ok()) {
    return std::string("{\"ok\":false,\"error\":\"") + st_a.message() + "\"}";
  }
  const Status st_b = LoadManifest(txn_b, &doc_b);
  if (!st_b.ok()) {
    return std::string("{\"ok\":false,\"error\":\"") + st_b.message() + "\"}";
  }
  catalog::SnapshotDiffResult diff;
  const Status diff_st =
      catalog::DiffManifestDocuments(doc_a, doc_b, digest_algo_, &diff);
  if (!diff_st.ok()) {
    return std::string("{\"ok\":false,\"error\":\"") + diff_st.message() + "\"}";
  }
  return catalog::SnapshotDiffToJson(diff);
}

std::string BackupEngine::ExportRestoreReportJson() const {
  if (!has_restore_acceptance_) {
    return "{\"ok\":false,\"error\":\"no restore acceptance report\"}";
  }
  return catalog::RestoreAcceptanceReportToJson(last_restore_acceptance_);
}

std::string BackupEngine::ExportBackupReportJson(uint64_t txn_id) const {
  if (txn_id == 0) {
    return "{\"ok\":false,\"error\":\"txn_id required\"}";
  }
  report::BackupReport br{};
  const Status st = report::LoadBackupReport(repo_path_, txn_id, &br);
  if (!st.ok()) {
    return std::string("{\"ok\":false,\"error\":\"") + st.message() + "\"}";
  }
  return report::BackupReportToJson(br);
}

std::string BackupEngine::SnapshotReachabilityJson(uint64_t txn_id) const {
  catalog::SnapshotReachabilityReport report{};
  const Status st = catalog::AnalyzeSnapshotReachability(*this, txn_id, &report);
  if (!st.ok()) {
    return std::string("{\"ok\":false,\"error\":\"") + st.message() + "\"}";
  }
  return catalog::SnapshotReachabilityReportToJson(report);
}

std::string BackupEngine::RpoSummaryJson() const {
  report::RpoSummaryReport report{};
  const Status st = report::BuildRpoSummary(*this, &report);
  if (!st.ok()) {
    return std::string("{\"ok\":false,\"error\":\"") + st.message() + "\"}";
  }
  return report::RpoSummaryReportToJson(report);
}

std::string BackupEngine::OrphanExplainJson(uint64_t sample_limit) const {
  store::OrphanExplainReport report{};
  const Status st = store::BuildOrphanExplainReport(*this, sample_limit, &report);
  if (!st.ok()) {
    return std::string("{\"ok\":false,\"error\":\"") + st.message() + "\"}";
  }
  return store::OrphanExplainReportToJson(report);
}

std::string BackupEngine::AppendOpsAuditJson(const std::string& op_json) {
  std::string op;
  size_t i = 0;
  const std::string json = op_json;
  while (i < json.size()) {
    while (i < json.size() && json[i] != '"') ++i;
    if (i >= json.size()) break;
    ++i;
    const size_t key_start = i;
    while (i < json.size() && json[i] != '"') ++i;
    const std::string key = json.substr(key_start, i - key_start);
    ++i;
    while (i < json.size() && json[i] != ':' && json[i] != '"') ++i;
    if (i < json.size() && json[i] == ':') ++i;
    if (key == "op") {
      while (i < json.size() && json[i] != '"') ++i;
      if (i < json.size()) ++i;
      const size_t val_start = i;
      while (i < json.size() && json[i] != '"') ++i;
      op = json.substr(val_start, i - val_start);
      break;
    }
  }
  if (op.empty()) {
    return "{\"ok\":false,\"error\":\"op required\"}";
  }
  const Status st = audit::AppendOpsAuditEntry(repo_path_, op, op_json,
                                               digest_algo_, audit_key_);
  if (!st.ok()) {
    return std::string("{\"ok\":false,\"error\":\"") + st.message() + "\"}";
  }
  return std::string("{\"ok\":true,\"op\":\"") + op + "\"}";
}

std::string BackupEngine::ListOpsAuditJson() const {
  std::vector<audit::RarChainEntry> entries;
  const Status st = audit::ListOpsAuditEntries(repo_path_, &entries);
  if (!st.ok()) {
    return std::string("{\"ok\":false,\"error\":\"") + st.message() + "\"}";
  }
  return audit::OpsAuditEntriesToJson(entries);
}

void RegisterBackupSyncRules(BackupSyncExecutor* exec, BackupEngine* engine) {
  exec->Register(BackupEvent::kOpen, [engine](BackupContext* ctx) -> Status {
    if (!engine) return Status::InvalidArgument("engine is null");
    ctx->phase = engine->phase();
    return Status::Ok();
  });

  exec->Register(BackupEvent::kScanFile, [](BackupContext* ctx) -> Status {
    ctx->phase = NextPhase(ctx->phase, BackupEvent::kScanFile);
    return Status::Ok();
  });

  exec->Register(BackupEvent::kChunkFile, [](BackupContext* ctx) -> Status {
    ctx->phase = NextPhase(ctx->phase, BackupEvent::kChunkFile);
    return Status::Ok();
  });

  exec->Register(BackupEvent::kStoreChunk, [](BackupContext* ctx) -> Status {
    ctx->phase = NextPhase(ctx->phase, BackupEvent::kStoreChunk);
    return Status::Ok();
  });

  exec->Register(BackupEvent::kCommitManifest,
                 [](BackupContext* ctx) -> Status {
                   ctx->phase = NextPhase(ctx->phase, BackupEvent::kCommitManifest);
                   return Status::Ok();
                 });

  exec->Register(BackupEvent::kAppendAudit, [](BackupContext* ctx) -> Status {
    ctx->phase = NextPhase(ctx->phase, BackupEvent::kAppendAudit);
    return Status::Ok();
  });

  exec->Register(BackupEvent::kComplete, [](BackupContext* ctx) -> Status {
    ctx->phase = NextPhase(ctx->phase, BackupEvent::kComplete);
    return Status::Ok();
  });

  exec->Register(BackupEvent::kVerify, [](BackupContext* ctx) -> Status {
    if (ctx->phase != BackupPhase::kIdle && ctx->phase != BackupPhase::kComplete &&
        ctx->phase != BackupPhase::kAborted) {
      return Status::Conflict("verify not allowed in current phase");
    }
    ctx->phase = BackupPhase::kAuditing;
    ctx->phase = BackupPhase::kIdle;
    return Status::Ok();
  });

  exec->Register(BackupEvent::kRecover, [](BackupContext* ctx) -> Status {
    ctx->phase = BackupPhase::kAborted;
    return Status::Ok();
  });

  exec->Register(BackupEvent::kGcOrphans, [](BackupContext* ctx) -> Status {
    if (ctx->phase != BackupPhase::kIdle && ctx->phase != BackupPhase::kAborted) {
      return Status::Conflict("gc not allowed during active backup");
    }
    return Status::Ok();
  });
}

}  // namespace ebbackup
