#include "ebbackup/engine/backup_engine.h"
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ebbackup/audit/carl_anchor.h"
#include "ebbackup/audit/merkle.h"
#include "ebbackup/audit/rar_chain.h"
#include "ebbackup/chunk/rolling_checksum.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/fsync.h"
#include "ebbackup/common/path_util.h"
#include "ebbackup/crypto/aes_gcm.h"
#include "ebbackup/engine/restore_engine.h"
#include "ebbackup/io/mmap_reader.h"
#include "ebbackup/pipeline/backup_pipeline.h"
#include "ebbackup/scan/backup_filter.h"
#include "ebbackup/scan/scan_entry.h"

namespace ebbackup {

namespace {

std::string RepoJoin(const std::string& repo, const std::string& name) {
  return (std::filesystem::path(repo) / name).string();
}

std::vector<uint8_t> ReadFileBytes(const std::string& path) {
  MmapReader reader;
  if (reader.Open(path).ok()) {
    return std::vector<uint8_t>(reader.data(), reader.data() + reader.size());
  }
  std::ifstream in(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
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
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::IoError("cannot open manifest: " + path);
  std::string header;
  if (!std::getline(in, header)) return Status::Corrupt("empty manifest");
  if (header != "EBMANIFEST2" && header != "EBMANIFEST3") {
    return Status::Corrupt("manifest v2/v3 required for crc");
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

Status BackupEngine::InitRepo(const std::string& repo_path) {
  std::error_code ec;
  std::filesystem::create_directories(RepoJoin(repo_path, "data"), ec);
  if (ec) return Status::IoError("cannot create repo data dir");
  std::filesystem::create_directories(RepoJoin(repo_path, "audit"), ec);
  if (ec) return Status::IoError("cannot create repo audit dir");
  BackupSuperBlockStore sb_store(RepoJoin(repo_path, "superblock.bin"));
  BackupSuperBlock sb{};
  SetPhase(&sb, BackupPhase::kIdle);
  sb.format_version = kBackupSuperBlockFormatV2;
  sb.ext.next_txn_id = 1;
  return sb_store.Commit(sb);
}

Status BackupEngine::StartupSelfCheck() {
  const std::string temp = RepoJoin(repo_path_, "manifest.new");
  std::error_code ec;
  if (std::filesystem::exists(temp)) {
    std::filesystem::remove(temp, ec);
  }
  if (phase_ == BackupPhase::kScanning || phase_ == BackupPhase::kChunking ||
      phase_ == BackupPhase::kStoring || phase_ == BackupPhase::kCommittingMeta ||
      phase_ == BackupPhase::kAuditing) {
    stats_.orphan_chunks_hint = sb_.critical.chunks_written;
    SetPhase(&sb_, BackupPhase::kAborted);
    return superblock_store_->Commit(sb_);
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
  const Status cs = chunk_store_->Open();
  if (!cs.ok()) return cs;
  phase_ = GetPhase(sb_);
  if (phase_ != BackupPhase::kIdle && phase_ != BackupPhase::kComplete &&
      phase_ != BackupPhase::kAborted) {
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
  uint8_t salt[16];
  const Status salt_st = crypto::LoadOrCreateRepoSalt(repo_path_, salt);
  if (!salt_st.ok()) return salt_st;
  const Status key_st = crypto::DeriveContentKey(options.encryption_password, salt,
                                                 content_key_);
  if (!key_st.ok()) return key_st;
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
  if (!std::filesystem::exists(salt_path)) return Status::Ok();
  if (password.empty()) {
    return Status::InvalidArgument("encrypted repo requires password");
  }
  if (has_content_key_) {
    chunk_store_->ClearContentKey();
    has_content_key_ = false;
  }
  uint8_t salt[16];
  const Status salt_st = crypto::LoadOrCreateRepoSalt(repo_path_, salt);
  if (!salt_st.ok()) return salt_st;
  const Status key_st =
      crypto::DeriveContentKey(password, salt, content_key_);
  if (!key_st.ok()) return key_st;
  has_content_key_ = true;
  chunk_store_->SetContentKey(content_key_);
  return Status::Ok();
}

Status BackupEngine::ScanFiles(const std::string& source_path,
                               const BackupOptions& options) {
  pending_scan_entries_.clear();
  pending_files_.clear();
  pending_meta_entries_.clear();
  const Status scan_st = ScanDirectory(source_path, &pending_scan_entries_);
  if (!scan_st.ok()) return scan_st;
  const Status filter_st =
      ApplyBackupFilter(options.filter, &pending_scan_entries_);
  if (!filter_st.ok()) return filter_st;

  for (const auto& entry : pending_scan_entries_) {
    if (entry.needs_chunking()) {
      pending_files_.push_back(entry.absolute_path);
    } else {
      pending_meta_entries_.push_back(entry.ToManifestSkeleton());
    }
  }
  EmitProgress(50);
  return Status::Ok();
}

Status BackupEngine::MergeMetaManifestEntries() {
  for (auto& mf : pending_manifest_) {
    for (const auto& se : pending_scan_entries_) {
      if (se.relative_path == mf.relative_path) {
        mf.file_type = se.type;
        mf.mode = se.mode;
        mf.uid = se.uid;
        mf.gid = se.gid;
        mf.mtime_unix = se.mtime_unix;
        mf.atime_unix = se.atime_unix;
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
  return Status::Ok();
}

Status BackupEngine::LoadPriorManifest(ManifestDocument* out) const {
  const std::string path = RepoJoin(repo_path_, "manifest");
  if (!std::filesystem::exists(path)) {
    return Status::NotFound("prior manifest not found");
  }
  return ReadManifestAuto(path, out);
}

Status BackupEngine::ChunkPendingFiles(BackupMode mode) {
  pending_chunks_.clear();
  pending_cfi_.clear();
  pending_manifest_.clear();
  pending_file_bytes_.clear();
  pending_chunks_.resize(pending_files_.size());
  pending_cfi_.resize(pending_files_.size());
  pending_file_bytes_.resize(pending_files_.size());

  ManifestDocument prior;
  if (mode == BackupMode::kIncremental) {
    const Status st = LoadPriorManifest(&prior);
    if (!st.ok()) return st;
  }

  EbHcrboChunker chunker;
  const size_t file_count = pending_files_.size();
  for (size_t i = 0; i < file_count; ++i) {
    const auto& file_path = pending_files_[i];
    std::string rel_final;
    const Status rel_st =
        RelativePathFromRoot(source_root_, file_path, &rel_final);
    if (!rel_st.ok()) return rel_st;

    pending_file_bytes_[i] = ReadFileBytes(file_path);
    const auto& bytes = pending_file_bytes_[i];
    EbHcrboStats hstats{};
    Status ch_st;
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
    stats_.chunks_reused_from_cfi += hstats.chunks_reused_from_cfi;

    ManifestFileEntry entry;
    entry.relative_path = rel_final;
    entry.size = bytes.size();
    entry.cfi = pending_cfi_[i];
    entry.file_type = FileType::kRegular;
    for (const auto& se : pending_scan_entries_) {
      if (se.absolute_path == file_path) {
        entry.mode = se.mode;
        entry.uid = se.uid;
        entry.gid = se.gid;
        entry.mtime_unix = se.mtime_unix;
        entry.atime_unix = se.atime_unix;
        break;
      }
    }
    pending_manifest_.push_back(std::move(entry));

    if (file_count > 0) {
      const uint64_t chunk_pct = 50 + static_cast<uint64_t>((i + 1) * 350 / file_count);
      EmitProgress(chunk_pct);
    }
  }
  return Status::Ok();
}

Status BackupEngine::StorePendingChunks(const BackupOptions& options) {
  ChunkStorePutOptions put_opts{};
  put_opts.use_lz4 = options.use_lz4;
  put_opts.use_encryption = options.use_encryption;
  if (has_content_key_) put_opts.content_key = content_key_;
  const size_t file_count = pending_chunks_.size();
  for (size_t fi = 0; fi < file_count; ++fi) {
    const auto& bytes = pending_file_bytes_[fi];
    auto& chunks = pending_chunks_[fi];
    auto& entry = pending_manifest_[fi];
    entry.chunk_hashes_hex.clear();
    for (const auto& chunk : chunks) {
      entry.chunk_hashes_hex.push_back(BytesToHex(chunk.hash, 32));
      if (chunk.reused_from_cfi && chunk_store_->Exists(chunk.hash)) {
        ++stats_.chunks_reused;
        continue;
      }
      const uint8_t* ptr = bytes.data() + chunk.offset;
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
  ManifestDocument prior;
  const ManifestDocument* prior_ptr = nullptr;
  if (mode == BackupMode::kIncremental) {
    const Status st = LoadPriorManifest(&prior);
    if (!st.ok()) return st;
    prior_ptr = &prior;
  }

  BackupPipelineOptions pipe_opts{};
  pipe_opts.use_lz4 = options.use_lz4;
  pipe_opts.use_encryption = options.use_encryption;
  pipe_opts.content_key = has_content_key_ ? content_key_ : nullptr;
  pipe_opts.use_mmap = true;
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
  const Status write_st = WriteManifestAuto(temp, doc);
  if (!write_st.ok()) return write_st;
  std::error_code ec;
  const std::string manifest_path = RepoJoin(repo_path_, "manifest");
  std::filesystem::rename(temp, manifest_path, ec);
  if (ec) return Status::IoError("manifest rename failed: " + ec.message());
  const Status fs = FsyncPath(manifest_path);
  if (!fs.ok()) return fs;

  const Status crc_st = ReadManifestBodyCrc32FromFile(manifest_path, &last_manifest_crc32_);
  if (!crc_st.ok()) return crc_st;
  const Status merkle_st = audit::ComputeMerkleRoot(doc, last_merkle_root_);
  if (!merkle_st.ok()) return merkle_st;
  EmitProgress(960);
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
  entry.rar_sha256 = audit::ComputeRarSha256(entry.body_json);
  const Status st = audit::AppendRarChainEntry(chain_path, entry);
  if (!st.ok()) return st;

  audit::CarlSignedTreeHead anchor{};
  const std::string anchor_dir = audit::DefaultCarlAnchorDir(repo_path_);
  (void)audit::PublishCarlAnchor(chain_path, anchor_dir, &anchor);

  EmitProgress(1000);
  return Status::Ok();
}

Status BackupEngine::VerifyManifestDocument(const ManifestDocument& doc) {
  if (sb_.critical.txn_id != 0 && doc.txn_id != sb_.critical.txn_id) {
    return Status::Corrupt("manifest txn_id mismatch with superblock");
  }

  for (const auto& file : doc.files) {
    if (file.file_type != FileType::kRegular) {
      if (!file.chunk_hashes_hex.empty()) {
        return Status::Corrupt("non-regular file has chunks: " + file.relative_path);
      }
      continue;
    }
    if (file.chunk_hashes_hex.empty() && file.size > 0) {
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
      total += payload.size();
    }
    if (total != file.size) {
      return Status::Corrupt("manifest file size mismatch for " +
                             file.relative_path);
    }
  }

  uint8_t merkle[32];
  const Status merkle_st = audit::ComputeMerkleRoot(doc, merkle);
  if (!merkle_st.ok()) return merkle_st;
  if (std::memcmp(merkle, sb_.ext.merkle_root, 32) != 0 &&
      sb_.ext.merkle_root[0] != 0) {
    return Status::Corrupt("merkle root mismatch with superblock");
  }

  const std::string chain_path = RepoJoin(repo_path_, "audit/rar.chain");
  if (std::filesystem::exists(chain_path)) {
    audit::RarChainVerifyReport report{};
    const Status chain_st = audit::VerifyRarChain(chain_path, &report);
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

Status BackupEngine::RunBackup(const std::string& source_path, BackupMode mode,
                               const BackupOptions& options) {
  if (!std::filesystem::exists(source_path)) {
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
  source_root_ = source_path;

  sb_.critical.txn_id = GetNextTxnId(sb_);
  if (options.use_lz4) {
    sb_.ext.default_codec = 1;
  }
  sb_.ext.backup_features = 0;

  Status st = SetupEncryption(options);
  if (!st.ok()) return st;

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

  st = ScanFiles(source_path, options);
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

  if (options.use_pipeline) {
    st = RunPipelineBackup(mode, options);
  } else {
    st = ChunkPendingFiles(mode);
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

  if (!options.use_pipeline) {
    st = StorePendingChunks(options);
    if (!st.ok()) {
      (void)PersistSuperBlock(BackupPhase::kAborted);
      return st;
    }
  }

  st = MergeMetaManifestEntries();
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

  st = CommitManifestFile();
  if (!st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return st;
  }

  st = DispatchTransition(BackupEvent::kAppendAudit);
  if (!st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return st;
  }
  st = PersistSuperBlock(BackupPhase::kAuditing);
  if (!st.ok()) return st;

  st = AppendAuditEntry();
  if (!st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return st;
  }

  st = DispatchTransition(BackupEvent::kComplete);
  if (!st.ok()) {
    (void)PersistSuperBlock(BackupPhase::kAborted);
    return st;
  }
  SetNextTxnId(&sb_, sb_.critical.txn_id + 1);
  return PersistSuperBlock(BackupPhase::kIdle);
}

Status BackupEngine::Verify(const BackupOptions& options) {
  const Status key_st = EnsureRepoContentKey(options.encryption_password);
  if (!key_st.ok()) return key_st;

  ManifestDocument doc;
  const Status rd = ReadManifestAuto(RepoJoin(repo_path_, "manifest"), &doc);
  if (!rd.ok()) return rd;

  const Status deep = VerifyManifestDocument(doc);
  if (!deep.ok()) return deep;

  const std::string chain_path = RepoJoin(repo_path_, "audit/rar.chain");
  if (options.require_anchor ||
      std::getenv("EBBACKUP_AUDIT_KEY") != nullptr) {
    const Status anchor_st = audit::VerifyCarlAnchorRequired(
        chain_path, audit::DefaultCarlAnchorDir(repo_path_),
        options.require_anchor);
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
  return restore.RunRestore(dest_path, options);
}

Status BackupEngine::GcOrphans(bool dry_run, OrphanGcReport* report) {
  ManifestDocument doc;
  const Status rd = ReadManifestAuto(RepoJoin(repo_path_, "manifest"), &doc);
  if (!rd.ok()) return rd;
  OrphanGcReport local{};
  OrphanGcReport* out = report ? report : &local;
  BackupContext ctx{this, phase_};
  const Status disp = sync_.Dispatch(BackupEvent::kGcOrphans, &ctx);
  if (!disp.ok()) return disp;
  return ExecuteOrphanGc(chunk_store_.get(), doc, dry_run, out);
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
