#include "ebbackup/pipeline/backup_pipeline.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

#include "ebbackup/chunk/chunk_profile.h"
#include "ebbackup/chunk/eb_hcrbo.h"
#include "ebbackup/chunk/fast_cdc.h"
#include "ebbackup/chunk/fast_cdc_streaming.h"
#include "ebbackup/chunk/rolling_checksum.h"
#include "ebbackup/codec/content_class.h"
#include "ebbackup/codec/lz4_codec.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/digest_pool.h"
#include "ebbackup/common/path_encoding.h"
#include "ebbackup/common/path_util.h"
#include "ebbackup/crypto/aes_gcm.h"
#include "ebbackup/io/mmap_reader.h"
#include "ebbackup/pipeline/file_scheduler.h"
#include "ebbackup/pipeline/pipeline_phase_stats.h"
#include "ebbackup/store/eb_pack.h"

namespace ebbackup {

namespace {

constexpr size_t kStreamFeedBytes = 32u * 1024u * 1024u;

size_t StreamFeedBytesForFile(size_t byte_len) {
  if (byte_len <= kStreamFeedBytes) {
    return byte_len;
  }
  return kStreamFeedBytes;
}

bool PipelineWorkersExplicitlyRequested(size_t worker_count) {
  if (worker_count > 0) return true;
  const char* env = std::getenv("EBBACKUP_PIPELINE_WORKERS");
  if (!env || env[0] == '\0') return false;
  const long parsed = std::strtol(env, nullptr, 10);
  return parsed > 0;
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

void AppendWeakCfiAnchors(const std::vector<ChunkDescriptor>& chunks,
                          const uint8_t* bytes, size_t byte_len,
                          CfiIndex* cfi) {
  if (!cfi) return;
  for (const auto& d : chunks) {
    ChunkAnchor a{};
    a.offset = d.offset;
    a.length = d.length;
    std::memcpy(a.hash, d.hash, 32);
    a.strength = AnchorStrength::kWeak;
    if (bytes && a.offset + a.length <= byte_len) {
      a.rolling_checksum = RollingChecksum(bytes + a.offset, a.length);
    }
    cfi->anchors.push_back(a);
  }
}

template <typename T>
class BoundedQueue {
 public:
  explicit BoundedQueue(size_t capacity) : capacity_(capacity) {}

  bool Push(T value) {
    std::unique_lock<std::mutex> lock(mu_);
    not_full_.wait(lock, [this] { return closed_ || queue_.size() < capacity_; });
    if (closed_) return false;
    queue_.push_back(std::move(value));
    not_empty_.notify_one();
    return true;
  }

  bool Pop(T* out) {
    std::unique_lock<std::mutex> lock(mu_);
    not_empty_.wait(lock, [this] { return closed_ || !queue_.empty(); });
    if (queue_.empty()) return false;
    *out = std::move(queue_.front());
    queue_.pop_front();
    not_full_.notify_one();
    return true;
  }

  void Close() {
    std::lock_guard<std::mutex> lock(mu_);
    closed_ = true;
    not_full_.notify_all();
    not_empty_.notify_all();
  }

 private:
  std::mutex mu_;
  std::condition_variable not_full_;
  std::condition_variable not_empty_;
  std::deque<T> queue_;
  size_t capacity_{8};
  bool closed_{false};
};

struct FileView {
  MmapReader mmap{};
  std::vector<uint8_t> owned{};
  bool use_mmap{false};

  FileView() = default;
  FileView(FileView&&) = default;
  FileView& operator=(FileView&&) = default;
  FileView(const FileView&) = delete;
  FileView& operator=(const FileView&) = delete;

  const uint8_t* data() const {
    return use_mmap ? mmap.data() : owned.data();
  }
  size_t size() const { return use_mmap ? mmap.size() : owned.size(); }
};

struct FilePipelineState {
  size_t index{0};
  std::string relative_path;
  std::shared_ptr<FileView> view;
  CfiIndex cfi;
  uint64_t file_size{0};
  size_t total_chunks{0};
  std::vector<std::string> chunk_hashes_hex;
  std::atomic<uint32_t> chunks_done{0};
  std::atomic<bool> chunking_complete{false};
  std::mutex finalize_mu;
  std::atomic<bool> finalized{false};
};

struct ChunkTask {
  std::shared_ptr<FilePipelineState> file;
  size_t chunk_index{0};
  ChunkDescriptor descriptor{};
  bool store_exists{false};
};

struct EncodedChunkTask {
  std::shared_ptr<FilePipelineState> file;
  size_t chunk_index{0};
  ChunkDescriptor descriptor{};
  std::vector<uint8_t> payload;
  ChunkCodec codec{ChunkCodec::kRaw};
  uint32_t uncompressed_len{0};
  bool skip_store{false};
};

struct FileInput {
  size_t index{0};
  std::string path;
  std::string relative_path;
};

struct FileData {
  size_t index{0};
  std::string relative_path;
  FileView view;
};

struct PipelineShared {
  BackupMode mode{BackupMode::kFull};
  const ManifestDocument* prior{nullptr};
  ChunkStore* store{nullptr};
  BackupStats* stats{nullptr};
  BackupPipelineResult* result{nullptr};
  BackupPipelineOptions options{};
  ProgressCallback progress_cb{nullptr};
  void* progress_user{nullptr};
  PipelinePhaseStats* phase_stats{nullptr};
  std::atomic<StatusCode> error{StatusCode::kOk};
  std::string error_message;
  mutable std::mutex error_mu;
  size_t total_files{0};
  std::atomic<size_t> files_read{0};
  std::atomic<size_t> files_chunked{0};
  std::atomic<size_t> chunks_encoded{0};
  std::atomic<size_t> files_stored{0};
  std::atomic<size_t> compressors_remaining{0};
  std::atomic<size_t> chunkers_remaining{0};
  BoundedQueue<ChunkTask>* chunk_q{nullptr};
  BoundedQueue<EncodedChunkTask>* encoded_q{nullptr};
  mutable std::mutex stats_mu;

  void MergeContentClassStats(const ContentClassStats& delta) {
    if (!options.content_stats) return;
    std::lock_guard<std::mutex> lock(stats_mu);
    options.content_stats->incompressible_skips += delta.incompressible_skips;
    options.content_stats->lz4_only += delta.lz4_only;
    options.content_stats->zstd_attempts += delta.zstd_attempts;
    options.content_stats->zstd_wins += delta.zstd_wins;
    options.content_stats->cpu_budget_spent_permille +=
        delta.cpu_budget_spent_permille;
  }

  void RecordFileProcessed(uint64_t file_size) {
    if (!stats) return;
    std::lock_guard<std::mutex> lock(stats_mu);
    stats->bytes_processed += file_size;
    ++stats->files_processed;
  }

  void RecordChunkStored(bool newly_written) {
    if (!stats) return;
    std::lock_guard<std::mutex> lock(stats_mu);
    if (newly_written) {
      ++stats->chunks_written;
    } else {
      ++stats->chunks_reused;
    }
  }

  void RecordCfiReuse(uint64_t count) {
    if (!stats || count == 0) return;
    std::lock_guard<std::mutex> lock(stats_mu);
    stats->chunks_reused_from_cfi += count;
  }

  void SetError(const Status& st) {
    if (st.ok()) return;
    std::lock_guard<std::mutex> lock(error_mu);
    if (error.load() == StatusCode::kOk) {
      error.store(st.code());
      error_message = st.message();
      if (chunk_q) chunk_q->Close();
      if (encoded_q) encoded_q->Close();
    }
  }

  Status CurrentError() const {
    const StatusCode code = error.load();
    if (code == StatusCode::kOk) return Status::Ok();
    std::lock_guard<std::mutex> lock(error_mu);
    return Status(code, error_message);
  }

  void EmitProgress(uint64_t permille) {
    if (progress_cb) progress_cb(permille, progress_user);
  }

  void AddPhaseNs(std::atomic<uint64_t>* field, uint64_t ns) {
    if (field) field->fetch_add(ns, std::memory_order_relaxed);
  }
};

void MergeStreamProfileToPhaseStats(const FastCdcStreamProfile& profile,
                                   PipelineShared* shared) {
  if (!shared || !shared->phase_stats) return;
  shared->phase_stats->stream_cdc_ns.fetch_add(
      profile.cdc_scan_ns, std::memory_order_relaxed);
  shared->phase_stats->stream_digest_ns.fetch_add(
      profile.digest_ns, std::memory_order_relaxed);
  shared->phase_stats->stream_carry_ns.fetch_add(
      profile.carry_copy_ns, std::memory_order_relaxed);
}

void ChunkFileStreaming(FileData item, PipelineShared* shared,
                        BoundedQueue<ChunkTask>* chunk_q);

void CompressorWorker(BoundedQueue<ChunkTask>* chunk_q,
                      BoundedQueue<EncodedChunkTask>* encoded_q,
                      PipelineShared* shared);

void StoreWorker(BoundedQueue<EncodedChunkTask>* encoded_q,
                 PipelineShared* shared);

Status EncodeChunkPayload(const uint8_t* data, size_t len,
                          PipelineShared* shared, const char* path_hint,
                          EncodedChunkTask* out) {
  if (!out || !shared) return Status::InvalidArgument("out is null");
  const BackupPipelineOptions& options = shared->options;
  ContentEncodeRequest req{};
  req.data = data;
  req.len = len;
  req.path_hint = path_hint;
  req.cpu_budget_permille = options.cpu_budget_permille;
  if (options.compress_mode != CompressMode::kOff) {
    req.mode = options.compress_mode;
  } else if (options.use_lz4) {
    req.mode = CompressMode::kLz4;
  }
  ContentClassStats delta{};
  ContentEncodeResult encoded{};
  const Status enc = ContentClassEncode(
      req, &encoded, options.content_stats ? &delta : nullptr);
  if (!enc.ok()) return enc;
  shared->MergeContentClassStats(delta);
  out->payload = std::move(encoded.payload);
  out->codec = encoded.codec;
  out->uncompressed_len = encoded.uncompressed_len;
  return Status::Ok();
}

void BatchExists(ChunkStore* store, const std::vector<ChunkDescriptor>& chunks,
                 std::vector<bool>* out) {
  out->resize(chunks.size());
  if (chunks.empty()) return;
  std::vector<const uint8_t*> hashes(chunks.size());
  for (size_t i = 0; i < chunks.size(); ++i) {
    hashes[i] = chunks[i].hash;
  }
  std::unique_ptr<bool[]> exists(new bool[chunks.size()]);
  store->ExistsMany(hashes.data(), chunks.size(), exists.get());
  for (size_t i = 0; i < chunks.size(); ++i) {
    (*out)[i] = exists[i];
  }
}

bool NeedsStoreExistsCheck(PipelineShared* shared) {
  return shared->mode == BackupMode::kIncremental && shared->prior != nullptr;
}

void FillStoreExists(ChunkStore* store, PipelineShared* shared,
                     const std::vector<ChunkDescriptor>& chunks,
                     std::vector<bool>* out) {
  out->assign(chunks.size(), false);
  if (!NeedsStoreExistsCheck(shared)) return;
  BatchExists(store, chunks, out);
}

void MaybeFinalizeFileLocked(const std::shared_ptr<FilePipelineState>& file,
                             PipelineShared* shared) {
  if (!file || file->finalized.load(std::memory_order_acquire)) return;
  if (!file->chunking_complete.load(std::memory_order_acquire)) return;
  if (file->total_chunks > 0 &&
      file->chunks_done.load(std::memory_order_acquire) < file->total_chunks) {
    return;
  }
  file->finalized.store(true, std::memory_order_release);

  if (file->chunk_hashes_hex.size() > file->total_chunks) {
    file->chunk_hashes_hex.resize(file->total_chunks);
  }

  ManifestFileEntry entry;
  entry.relative_path = file->relative_path;
  entry.size = file->file_size;
  entry.cfi = file->cfi;
  entry.chunk_hashes_hex = std::move(file->chunk_hashes_hex);

  shared->RecordFileProcessed(file->file_size);
  if (shared->result) {
    if (shared->result->manifest_files.size() <= file->index) {
      shared->result->manifest_files.resize(file->index + 1);
    }
    shared->result->manifest_files[file->index] = std::move(entry);
  }
  const size_t done = ++shared->files_stored;
  if (shared->total_files > 0) {
    const uint64_t pct =
        600 + static_cast<uint64_t>(done * 350 / shared->total_files);
    shared->EmitProgress(pct);
  }
}

void TryFinalizeFile(const std::shared_ptr<FilePipelineState>& file,
                     PipelineShared* shared) {
  if (!file || file->finalized.load(std::memory_order_acquire)) return;
  if (!file->chunking_complete.load(std::memory_order_acquire)) return;
  const uint32_t total = static_cast<uint32_t>(file->total_chunks);
  if (total > 0 &&
      file->chunks_done.load(std::memory_order_acquire) < total) {
    return;
  }
  std::lock_guard<std::mutex> lock(file->finalize_mu);
  if (file->finalized.load(std::memory_order_acquire)) return;
  MaybeFinalizeFileLocked(file, shared);
}

void RecordStoredChunkHash(const std::shared_ptr<FilePipelineState>& file,
                           size_t chunk_index, const uint8_t hash[32],
                           PipelineShared* shared) {
  std::lock_guard<std::mutex> lock(file->finalize_mu);
  if (file->finalized.load(std::memory_order_acquire)) return;
  if (chunk_index >= file->chunk_hashes_hex.size()) {
    shared->SetError(Status::Internal("pipeline chunk hash index overflow"));
    return;
  }
  file->chunk_hashes_hex[chunk_index] = BytesToHex(hash, 32);
  file->chunks_done.fetch_add(1, std::memory_order_release);
  MaybeFinalizeFileLocked(file, shared);
}

void PushChunkTasks(const std::shared_ptr<FilePipelineState>& file_state,
                    const std::vector<ChunkDescriptor>& chunks,
                    const std::vector<bool>& store_exists,
                    BoundedQueue<ChunkTask>* chunk_q, PipelineShared* shared) {
  {
    std::lock_guard<std::mutex> lock(file_state->finalize_mu);
    file_state->total_chunks = chunks.size();
    file_state->chunk_hashes_hex.resize(chunks.size());
  }
  for (size_t ci = 0; ci < chunks.size(); ++ci) {
    ChunkTask task{};
    task.file = file_state;
    task.chunk_index = ci;
    task.descriptor = chunks[ci];
    task.store_exists = store_exists[ci];
    if (!chunk_q->Push(std::move(task))) {
      shared->SetError(Status::Internal("pipeline chunk queue closed early"));
      return;
    }
  }
}

void ChunkFileFull(FileData item, PipelineShared* shared,
                   BoundedQueue<ChunkTask>* chunk_q) {
  const auto t0 = std::chrono::steady_clock::now();
  auto file_state = std::make_shared<FilePipelineState>();
  file_state->index = item.index;
  file_state->relative_path = item.relative_path;
  file_state->view = std::make_shared<FileView>(std::move(item.view));
  file_state->file_size = file_state->view->size();
  const uint8_t* bytes = file_state->view->data();
  const size_t byte_len = file_state->view->size();

  EbHcrboConfig hcrbo_cfg =
      EbHcrboConfigForFileSize(byte_len, shared->options.chunk_profile,
                               shared->options.digest_algo);
  EbHcrboChunker chunker(hcrbo_cfg);
  EbHcrboStats hstats{};
  std::vector<ChunkDescriptor> chunks;
  Status ch_st;
  if (shared->mode == BackupMode::kIncremental && shared->prior) {
    const CfiIndex history = FindPriorCfi(*shared->prior, item.relative_path);
    ch_st = chunker.ChunkIncremental(bytes, byte_len, history, &chunks,
                                     &file_state->cfi, &hstats);
  } else {
    ch_st = chunker.ChunkFull(bytes, byte_len, &chunks, &file_state->cfi, &hstats);
  }
  if (!ch_st.ok()) {
    shared->SetError(ch_st);
    return;
  }
  PopulateAnchorChecksums(bytes, byte_len, &file_state->cfi);
  std::vector<bool> store_exists;
  FillStoreExists(shared->store, shared, chunks, &store_exists);
  shared->RecordCfiReuse(hstats.chunks_reused_from_cfi);
  PushChunkTasks(file_state, chunks, store_exists, chunk_q, shared);
  file_state->chunking_complete.store(true, std::memory_order_release);
  TryFinalizeFile(file_state, shared);
  const auto t1 = std::chrono::steady_clock::now();
  shared->AddPhaseNs(
      shared->phase_stats ? &shared->phase_stats->chunk_ns : nullptr,
      static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
}

bool ForceStreamCdc() {
  const char* env = std::getenv("EBBACKUP_FORCE_STREAM_CDC");
  return env && env[0] == '1';
}

bool CdcFastSliceEnabled() {
  const char* env = std::getenv("EBBACKUP_CDC_FAST_SLICE");
  return env && env[0] == '1';
}

bool UseCdcFastPath(const PipelineShared* shared, const uint8_t* bytes,
                    size_t byte_len) {
  if (!CdcFastSliceEnabled()) return false;
  if (!shared || !bytes || byte_len == 0) return false;
  if (ForceStreamCdc()) return false;
  if (shared->mode != BackupMode::kFull) return false;
  if (byte_len <= kStreamFeedBytes) return false;
  return true;
}

void ChunkFileStreamingFastSlice(FileData item, PipelineShared* shared,
                                 BoundedQueue<ChunkTask>* chunk_q) {
  const auto t0 = std::chrono::steady_clock::now();
  auto file_state = std::make_shared<FilePipelineState>();
  file_state->index = item.index;
  file_state->relative_path = item.relative_path;
  file_state->view = std::make_shared<FileView>(std::move(item.view));
  file_state->file_size = file_state->view->size();
  const uint8_t* bytes = file_state->view->data();
  const size_t byte_len = file_state->view->size();

  EbHcrboConfig hcrbo_cfg =
      EbHcrboConfigForFileSize(byte_len, shared->options.chunk_profile,
                               shared->options.digest_algo);
  const FastCdcConfig& cfg = hcrbo_cfg.fast;
  FastCdcSlice chunker(cfg);

  const size_t hash_slot_capacity =
      byte_len == 0 ? 0
                    : std::max<size_t>(1, byte_len / cfg.min_size + 2);
  file_state->chunk_hashes_hex.assign(hash_slot_capacity, std::string());
  size_t chunk_index = 0;

  auto push_batch = [&](const std::vector<ChunkDescriptor>& batch) -> bool {
    if (batch.empty()) return true;
    AppendWeakCfiAnchors(batch, bytes, byte_len, &file_state->cfi);
    {
      std::lock_guard<std::mutex> lock(file_state->finalize_mu);
      file_state->total_chunks = chunk_index + batch.size();
      if (file_state->total_chunks > file_state->chunk_hashes_hex.size()) {
        shared->SetError(Status::Internal("fast slice chunk index overflow"));
        return false;
      }
    }
    std::vector<bool> store_exists;
    FillStoreExists(shared->store, shared, batch, &store_exists);
    for (size_t bi = 0; bi < batch.size(); ++bi) {
      ChunkTask task{};
      task.file = file_state;
      task.chunk_index = chunk_index + bi;
      task.descriptor = batch[bi];
      task.store_exists = store_exists[bi];
      if (!chunk_q->Push(std::move(task))) {
        shared->SetError(Status::Internal("pipeline chunk queue closed early"));
        return false;
      }
    }
    chunk_index += batch.size();
    return true;
  };

  auto hash_batch = [&](const std::vector<size_t>& offsets,
                        const std::vector<uint32_t>& lengths,
                        std::vector<ChunkDescriptor>* out) {
    out->clear();
    const size_t count = offsets.size();
    if (count == 0) return;
    out->resize(count);
    constexpr size_t kParallelHashMinBytes = 1024 * 1024;
    constexpr size_t kParallelHashMinChunks = 2;
    const size_t total_bytes =
        std::accumulate(lengths.begin(), lengths.end(), size_t{0});
    const bool use_pool = count >= kParallelHashMinChunks &&
                          total_bytes >= kParallelHashMinBytes;
    if (use_pool) {
      std::vector<DigestSpan> spans(count);
      for (size_t i = 0; i < count; ++i) {
        spans[i].offset = offsets[i];
        spans[i].length = lengths[i];
      }
      std::vector<uint8_t> hashes(count * 32);
      DigestPool::Shared().HashRegions(cfg.digest_algo, bytes, spans.data(), count,
                                       hashes.data());
      for (size_t i = 0; i < count; ++i) {
        (*out)[i].offset = offsets[i];
        (*out)[i].length = lengths[i];
        std::memcpy((*out)[i].hash, hashes.data() + i * 32, 32);
      }
      return;
    }
    for (size_t i = 0; i < count; ++i) {
      (*out)[i].offset = offsets[i];
      (*out)[i].length = lengths[i];
      ContentHash(cfg.digest_algo, bytes + offsets[i], lengths[i], (*out)[i].hash);
    }
  };

  if (byte_len == 0) {
    file_state->chunking_complete.store(true, std::memory_order_release);
    TryFinalizeFile(file_state, shared);
  } else if (byte_len <= cfg.min_size) {
    ChunkDescriptor desc{};
    desc.offset = 0;
    desc.length = static_cast<uint32_t>(byte_len);
    ContentHash(cfg.digest_algo, bytes, byte_len, desc.hash);
    if (!push_batch({desc})) return;
    PopulateAnchorChecksums(bytes, byte_len, &file_state->cfi);
    file_state->chunking_complete.store(true, std::memory_order_release);
    TryFinalizeFile(file_state, shared);
  } else {
    std::vector<size_t> all_offsets;
    std::vector<uint32_t> all_lengths;
    const Status cuts_st =
        chunker.ChunkCuts(bytes, byte_len, &all_offsets, &all_lengths);
    if (!cuts_st.ok()) {
      shared->SetError(cuts_st);
      return;
    }

    size_t batch_start = 0;
    size_t batch_bytes = 0;
    for (size_t i = 0; i < all_offsets.size(); ++i) {
      batch_bytes += all_lengths[i];
      const bool last_chunk = (i + 1 == all_offsets.size());
      if (batch_bytes < kStreamFeedBytes && !last_chunk) continue;

      std::vector<size_t> offsets(all_offsets.begin() + batch_start,
                                  all_offsets.begin() + i + 1);
      std::vector<uint32_t> lengths(all_lengths.begin() + batch_start,
                                    all_lengths.begin() + i + 1);
      std::vector<ChunkDescriptor> batch;
      hash_batch(offsets, lengths, &batch);
      if (!push_batch(batch)) return;
      batch_start = i + 1;
      batch_bytes = 0;
    }
    PopulateAnchorChecksums(bytes, byte_len, &file_state->cfi);
    file_state->chunking_complete.store(true, std::memory_order_release);
    TryFinalizeFile(file_state, shared);
  }

  const auto t1 = std::chrono::steady_clock::now();
  shared->AddPhaseNs(
      shared->phase_stats ? &shared->phase_stats->chunk_ns : nullptr,
      static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
}

void ChunkFileStreaming(FileData item, PipelineShared* shared,
                        BoundedQueue<ChunkTask>* chunk_q) {
  const uint8_t* bytes_preview = item.view.data();
  const size_t byte_len_preview = item.view.size();
  if (UseCdcFastPath(shared, bytes_preview, byte_len_preview)) {
    ChunkFileStreamingFastSlice(std::move(item), shared, chunk_q);
    return;
  }

  const auto t0 = std::chrono::steady_clock::now();
  auto file_state = std::make_shared<FilePipelineState>();
  file_state->index = item.index;
  file_state->relative_path = item.relative_path;
  file_state->view = std::make_shared<FileView>(std::move(item.view));
  file_state->file_size = file_state->view->size();
  const uint8_t* bytes = file_state->view->data();
  const size_t byte_len = file_state->view->size();

  EbHcrboConfig hcrbo_cfg =
      EbHcrboConfigForFileSize(byte_len, shared->options.chunk_profile,
                               shared->options.digest_algo);
  FastCdcStreamState stream_state{};
  FastCdcStreamInit(&stream_state, hcrbo_cfg.fast);
  stream_state.digest_base = bytes;

  const size_t hash_slot_capacity =
      byte_len == 0 ? 0
                    : std::max<size_t>(1, byte_len / hcrbo_cfg.fast.min_size + 2);
  file_state->chunk_hashes_hex.assign(hash_slot_capacity, std::string());

  size_t off = 0;
  size_t chunk_index = 0;
  const size_t feed_bytes = StreamFeedBytesForFile(byte_len);
  while (off < byte_len) {
    const size_t n = std::min(feed_bytes, byte_len - off);
    const bool last = (off + n >= byte_len);
    std::vector<ChunkDescriptor> batch;
    const Status st =
        FastCdcStreamFeed(&stream_state, bytes + off, n, last, &batch);
    if (!st.ok()) {
      shared->SetError(st);
      return;
    }
    off += n;
    if (batch.empty()) continue;

    AppendWeakCfiAnchors(batch, bytes, byte_len, &file_state->cfi);
    {
      std::lock_guard<std::mutex> lock(file_state->finalize_mu);
      file_state->total_chunks = chunk_index + batch.size();
      if (file_state->total_chunks > file_state->chunk_hashes_hex.size()) {
        shared->SetError(Status::Internal("streaming chunk index overflow"));
        return;
      }
    }
    std::vector<bool> store_exists;
    FillStoreExists(shared->store, shared, batch, &store_exists);
    for (size_t bi = 0; bi < batch.size(); ++bi) {
      ChunkTask task{};
      task.file = file_state;
      task.chunk_index = chunk_index + bi;
      task.descriptor = batch[bi];
      task.store_exists = store_exists[bi];
      if (!chunk_q->Push(std::move(task))) {
        shared->SetError(Status::Internal("pipeline chunk queue closed early"));
        return;
      }
    }
    chunk_index += batch.size();
  }

  file_state->chunking_complete.store(true, std::memory_order_release);
  TryFinalizeFile(file_state, shared);

  MergeStreamProfileToPhaseStats(stream_state.profile, shared);

  const auto t1 = std::chrono::steady_clock::now();
  shared->AddPhaseNs(
      shared->phase_stats ? &shared->phase_stats->chunk_ns : nullptr,
      static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
}

Status StoreChunkInline(const ChunkTask& task, PipelineShared* shared) {
  if (!shared->CurrentError().ok()) return shared->CurrentError();
  const auto t0 = std::chrono::steady_clock::now();
  EncodedChunkTask enc{};
  enc.file = task.file;
  enc.chunk_index = task.chunk_index;
  enc.descriptor = task.descriptor;
  if (task.store_exists) {
    enc.skip_store = true;
    enc.codec = ChunkCodec::kRaw;
    enc.uncompressed_len = task.descriptor.length;
  } else {
    const uint8_t* bytes = task.file->view->data();
    const Status st = EncodeChunkPayload(
        bytes + task.descriptor.offset, task.descriptor.length, shared,
        task.file->relative_path.c_str(), &enc);
    if (!st.ok()) {
      shared->SetError(st);
      return st;
    }
  }
  const auto t1 = std::chrono::steady_clock::now();
  shared->AddPhaseNs(
      shared->phase_stats ? &shared->phase_stats->encode_ns : nullptr,
      static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));

  const auto t2 = std::chrono::steady_clock::now();
  if (!enc.skip_store) {
    bool newly_written = false;
    ChunkCodec store_codec = enc.codec;
    const uint8_t* payload_ptr = enc.payload.data();
    size_t payload_len = enc.payload.size();
    std::vector<uint8_t> encrypted_payload;
    if (shared->options.use_encryption) {
      if (!shared->options.content_key) {
        shared->SetError(
            Status::InvalidArgument("encryption requires content key"));
        return shared->CurrentError();
      }
      const Status enc_st = crypto::Aes256GcmEncrypt(
          shared->options.content_key, enc.payload.data(), enc.payload.size(),
          &encrypted_payload);
      if (!enc_st.ok()) {
        shared->SetError(enc_st);
        return enc_st;
      }
      payload_ptr = encrypted_payload.data();
      payload_len = encrypted_payload.size();
      if (enc.codec == ChunkCodec::kLz4) {
        store_codec = ChunkCodec::kEncryptedLz4;
      } else if (enc.codec == ChunkCodec::kZstd) {
        store_codec = ChunkCodec::kEncryptedZstd;
      } else {
        store_codec = ChunkCodec::kEncrypted;
      }
    }
    const Status put_st = shared->store->PutPrecompressed(
        enc.descriptor.hash, payload_ptr, payload_len, enc.uncompressed_len,
        store_codec, &newly_written, true);
    if (!put_st.ok()) {
      shared->SetError(put_st);
      return put_st;
    }
    shared->RecordChunkStored(newly_written);
  } else {
    shared->RecordChunkStored(false);
  }
  RecordStoredChunkHash(enc.file, enc.chunk_index, enc.descriptor.hash, shared);
  const auto t3 = std::chrono::steady_clock::now();
  shared->AddPhaseNs(
      shared->phase_stats ? &shared->phase_stats->store_ns : nullptr,
      static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(t3 - t2).count()));
  return shared->CurrentError();
}

Status ReadOneFileData(const std::string& path, const BackupPipelineOptions& options,
                       FileData* out) {
  if (!out) return Status::InvalidArgument("out is null");
  if (options.use_mmap) {
    MmapReader reader;
    const Status st = reader.Open(path);
    if (!st.ok()) return st;
    out->view.use_mmap = true;
    out->view.mmap = std::move(reader);
  } else {
    std::ifstream file(PathFromUtf8(path), std::ios::binary);
    if (!file) return Status::IoError("read failed: " + path);
    out->view.owned.assign(std::istreambuf_iterator<char>(file),
                           std::istreambuf_iterator<char>());
  }
  return Status::Ok();
}

Status RunSingleFileStreamingChunkPipeline(const std::string& path,
                                           const std::string& relative_path,
                                           size_t index, PipelineShared* shared) {
  const auto read_t0 = std::chrono::steady_clock::now();
  FileData item{};
  item.index = index;
  item.relative_path = relative_path;
  const Status read_st = ReadOneFileData(path, shared->options, &item);
  if (!read_st.ok()) return read_st;
  const auto read_t1 = std::chrono::steady_clock::now();
  shared->AddPhaseNs(
      shared->phase_stats ? &shared->phase_stats->read_ns : nullptr,
      static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(read_t1 - read_t0)
              .count()));

  constexpr size_t kQueueDepth = 1024;
  constexpr size_t kEncodeWorkers = 2;
  constexpr size_t kStoreWorkers = 1;
  BoundedQueue<ChunkTask> chunk_q(kQueueDepth);
  BoundedQueue<EncodedChunkTask> encoded_q(kQueueDepth);
  shared->chunk_q = &chunk_q;
  shared->encoded_q = &encoded_q;
  shared->compressors_remaining.store(kEncodeWorkers, std::memory_order_release);
  shared->chunkers_remaining.store(1, std::memory_order_release);

  std::vector<std::thread> workers;
  workers.reserve(kEncodeWorkers + kStoreWorkers);
  for (size_t i = 0; i < kEncodeWorkers; ++i) {
    workers.emplace_back(CompressorWorker, &chunk_q, &encoded_q, shared);
  }
  for (size_t i = 0; i < kStoreWorkers; ++i) {
    workers.emplace_back(StoreWorker, &encoded_q, shared);
  }

  ChunkFileStreaming(std::move(item), shared, &chunk_q);

  if (shared->chunkers_remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    chunk_q.Close();
  }
  for (auto& worker : workers) {
    worker.join();
  }
  shared->chunk_q = nullptr;
  shared->encoded_q = nullptr;

  const Status err = shared->CurrentError();
  if (!err.ok()) return err;

  ++shared->files_read;
  ++shared->files_chunked;
  return Status::Ok();
}

Status RunSingleFileInlinePipeline(const std::string& path,
                                   const std::string& relative_path,
                                   size_t index, PipelineShared* shared) {
  const auto read_t0 = std::chrono::steady_clock::now();
  FileData item{};
  item.index = index;
  item.relative_path = relative_path;
  const Status read_st = ReadOneFileData(path, shared->options, &item);
  if (!read_st.ok()) return read_st;
  const auto read_t1 = std::chrono::steady_clock::now();
  shared->AddPhaseNs(
      shared->phase_stats ? &shared->phase_stats->read_ns : nullptr,
      static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(read_t1 - read_t0)
              .count()));

  const auto chunk_t0 = std::chrono::steady_clock::now();
  FileView view = std::move(item.view);
  const uint8_t* bytes = view.data();
  const size_t byte_len = view.size();

  CfiIndex cfi;
  EbHcrboConfig hcrbo_cfg =
      EbHcrboConfigForFileSize(byte_len, shared->options.chunk_profile,
                               shared->options.digest_algo);
  EbHcrboChunker chunker(hcrbo_cfg);
  EbHcrboStats hstats{};
  std::vector<ChunkDescriptor> chunks;
  Status ch_st;
  if (shared->mode == BackupMode::kIncremental && shared->prior) {
    const CfiIndex history = FindPriorCfi(*shared->prior, relative_path);
    ch_st =
        chunker.ChunkIncremental(bytes, byte_len, history, &chunks, &cfi, &hstats);
  } else {
    ch_st = chunker.ChunkFull(bytes, byte_len, &chunks, &cfi, &hstats);
  }
  if (!ch_st.ok()) return ch_st;
  PopulateAnchorChecksums(bytes, byte_len, &cfi);
  std::vector<bool> store_exists;
  FillStoreExists(shared->store, shared, chunks, &store_exists);

  const auto chunk_t1 = std::chrono::steady_clock::now();
  shared->AddPhaseNs(
      shared->phase_stats ? &shared->phase_stats->chunk_ns : nullptr,
      static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(chunk_t1 - chunk_t0)
              .count()));

  std::vector<std::string> chunk_hashes_hex(chunks.size());
  ContentClassStats content_delta{};
  size_t chunks_written = 0;
  size_t chunks_reused = 0;
  uint64_t encode_ns = 0;
  uint64_t store_ns = 0;

  const BackupPipelineOptions& options = shared->options;
  for (size_t ci = 0; ci < chunks.size(); ++ci) {
    const ChunkDescriptor& desc = chunks[ci];
    chunk_hashes_hex[ci] = BytesToHex(desc.hash, 32);

    if (store_exists[ci]) {
      ++chunks_reused;
      continue;
    }

    const auto enc_t0 = std::chrono::steady_clock::now();
    ContentEncodeRequest req{};
    req.data = bytes + desc.offset;
    req.len = desc.length;
    req.path_hint = relative_path.c_str();
    req.cpu_budget_permille = options.cpu_budget_permille;
    if (options.compress_mode != CompressMode::kOff) {
      req.mode = options.compress_mode;
    } else if (options.use_lz4) {
      req.mode = CompressMode::kLz4;
    }
    ContentClassStats enc_delta{};
    ContentEncodeResult encoded{};
    const Status enc_st = ContentClassEncode(
        req, &encoded, options.content_stats ? &enc_delta : nullptr);
    if (!enc_st.ok()) return enc_st;
    content_delta.incompressible_skips += enc_delta.incompressible_skips;
    content_delta.lz4_only += enc_delta.lz4_only;
    content_delta.zstd_attempts += enc_delta.zstd_attempts;
    content_delta.zstd_wins += enc_delta.zstd_wins;
    content_delta.cpu_budget_spent_permille +=
        enc_delta.cpu_budget_spent_permille;
    const auto enc_t1 = std::chrono::steady_clock::now();
    encode_ns += static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(enc_t1 - enc_t0)
            .count());

    const auto store_t0 = std::chrono::steady_clock::now();
    ChunkCodec store_codec = encoded.codec;
    const uint8_t* payload_ptr = encoded.payload.data();
    size_t payload_len = encoded.payload.size();
    std::vector<uint8_t> encrypted_payload;
    if (options.use_encryption) {
      if (!options.content_key) {
        return Status::InvalidArgument("encryption requires content key");
      }
      const Status encr_st = crypto::Aes256GcmEncrypt(
          options.content_key, encoded.payload.data(), encoded.payload.size(),
          &encrypted_payload);
      if (!encr_st.ok()) return encr_st;
      payload_ptr = encrypted_payload.data();
      payload_len = encrypted_payload.size();
      if (encoded.codec == ChunkCodec::kLz4) {
        store_codec = ChunkCodec::kEncryptedLz4;
      } else if (encoded.codec == ChunkCodec::kZstd) {
        store_codec = ChunkCodec::kEncryptedZstd;
      } else {
        store_codec = ChunkCodec::kEncrypted;
      }
    }
    bool newly_written = false;
    const Status put_st = shared->store->PutPrecompressed(
        desc.hash, payload_ptr, payload_len, encoded.uncompressed_len, store_codec,
        &newly_written, true);
    if (!put_st.ok()) return put_st;
    if (newly_written) {
      ++chunks_written;
    } else {
      ++chunks_reused;
    }
    const auto store_t1 = std::chrono::steady_clock::now();
    store_ns += static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(store_t1 - store_t0)
            .count());
  }

  shared->AddPhaseNs(
      shared->phase_stats ? &shared->phase_stats->encode_ns : nullptr, encode_ns);
  shared->AddPhaseNs(
      shared->phase_stats ? &shared->phase_stats->store_ns : nullptr, store_ns);

  if (shared->stats) {
    shared->stats->chunks_written += chunks_written;
    shared->stats->chunks_reused += chunks_reused;
    shared->stats->chunks_reused_from_cfi += hstats.chunks_reused_from_cfi;
    shared->stats->bytes_processed += byte_len;
    ++shared->stats->files_processed;
  }
  if (options.content_stats) {
    options.content_stats->incompressible_skips += content_delta.incompressible_skips;
    options.content_stats->lz4_only += content_delta.lz4_only;
    options.content_stats->zstd_attempts += content_delta.zstd_attempts;
    options.content_stats->zstd_wins += content_delta.zstd_wins;
    options.content_stats->cpu_budget_spent_permille +=
        content_delta.cpu_budget_spent_permille;
  }

  if (shared->result) {
    if (shared->result->manifest_files.size() <= index) {
      shared->result->manifest_files.resize(index + 1);
    }
    ManifestFileEntry entry;
    entry.relative_path = relative_path;
    entry.size = byte_len;
    entry.cfi = std::move(cfi);
    entry.chunk_hashes_hex = std::move(chunk_hashes_hex);
    shared->result->manifest_files[index] = std::move(entry);
  }
  if (shared->total_files > 0) {
    shared->EmitProgress(950);
  }
  ++shared->files_read;
  ++shared->files_chunked;
  ++shared->files_stored;
  return Status::Ok();
}

void ReaderStage(BoundedQueue<FileInput>* in, BoundedQueue<FileData>* out,
                 PipelineShared* shared) {
  FileInput item{};
  while (in->Pop(&item)) {
    if (!shared->CurrentError().ok()) break;
    const auto t0 = std::chrono::steady_clock::now();
    FileData data{};
    data.index = item.index;
    data.relative_path = item.relative_path;
    if (shared->options.use_mmap) {
      MmapReader reader;
      const Status st = reader.Open(item.path);
      if (!st.ok()) {
        shared->SetError(st);
        break;
      }
      data.view.use_mmap = true;
      data.view.mmap = std::move(reader);
    } else {
      std::ifstream file(PathFromUtf8(item.path), std::ios::binary);
      if (!file) {
        shared->SetError(Status::IoError("read failed: " + item.path));
        break;
      }
      data.view.owned.assign(std::istreambuf_iterator<char>(file),
                             std::istreambuf_iterator<char>());
    }
    out->Push(std::move(data));
    const auto t1 = std::chrono::steady_clock::now();
    shared->AddPhaseNs(
        shared->phase_stats ? &shared->phase_stats->read_ns : nullptr,
        static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
    const size_t done = ++shared->files_read;
    if (shared->total_files > 0) {
      const uint64_t pct = 50 + static_cast<uint64_t>(done * 100 / shared->total_files);
      shared->EmitProgress(pct);
    }
  }
  out->Close();
}

void ChunkerStage(BoundedQueue<FileData>* in, BoundedQueue<ChunkTask>* chunk_q,
                  PipelineShared* shared) {
  FileData item{};
  while (in->Pop(&item)) {
    if (!shared->CurrentError().ok()) break;
    const bool stream = shared->mode == BackupMode::kFull &&
                        item.view.size() > kStreamFeedBytes;
    if (stream) {
      ChunkFileStreaming(std::move(item), shared, chunk_q);
    } else {
      ChunkFileFull(std::move(item), shared, chunk_q);
    }
    if (!shared->CurrentError().ok()) break;
    const size_t done = ++shared->files_chunked;
    if (shared->total_files > 0) {
      const uint64_t pct = 150 + static_cast<uint64_t>(done * 250 / shared->total_files);
      shared->EmitProgress(pct);
    }
  }
  if (shared->chunkers_remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    chunk_q->Close();
  }
}

void CompressorWorker(BoundedQueue<ChunkTask>* chunk_q,
                      BoundedQueue<EncodedChunkTask>* encoded_q,
                      PipelineShared* shared) {
  ChunkTask item{};
  while (chunk_q->Pop(&item)) {
    if (!shared->CurrentError().ok()) break;
    const auto t0 = std::chrono::steady_clock::now();
    EncodedChunkTask enc{};
    enc.file = item.file;
    enc.chunk_index = item.chunk_index;
    enc.descriptor = item.descriptor;
    if (item.store_exists) {
      enc.skip_store = true;
      enc.codec = ChunkCodec::kRaw;
      enc.uncompressed_len = item.descriptor.length;
    } else {
      const uint8_t* bytes = item.file->view->data();
      const Status st = EncodeChunkPayload(
          bytes + item.descriptor.offset, item.descriptor.length, shared,
          item.file->relative_path.c_str(), &enc);
      if (!st.ok()) {
        shared->SetError(st);
        break;
      }
    }
    if (!encoded_q->Push(std::move(enc))) {
      shared->SetError(Status::Internal("pipeline encoded queue closed early"));
      break;
    }
    ++shared->chunks_encoded;
    const auto t1 = std::chrono::steady_clock::now();
    shared->AddPhaseNs(
        shared->phase_stats ? &shared->phase_stats->encode_ns : nullptr,
        static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
  }
  if (shared->compressors_remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    encoded_q->Close();
  }
}

void StoreWorker(BoundedQueue<EncodedChunkTask>* encoded_q,
                 PipelineShared* shared) {
  EncodedChunkTask item{};
  while (encoded_q->Pop(&item)) {
    if (!shared->CurrentError().ok()) break;
    const auto t0 = std::chrono::steady_clock::now();
    auto& file = item.file;
    if (!item.skip_store) {
      bool newly_written = false;
      ChunkCodec store_codec = item.codec;
      const uint8_t* payload_ptr = item.payload.data();
      size_t payload_len = item.payload.size();
      std::vector<uint8_t> encrypted_payload;
      if (shared->options.use_encryption) {
        if (!shared->options.content_key) {
          shared->SetError(
              Status::InvalidArgument("encryption requires content key"));
          break;
        }
        const Status enc_st = crypto::Aes256GcmEncrypt(
            shared->options.content_key, item.payload.data(), item.payload.size(),
            &encrypted_payload);
        if (!enc_st.ok()) {
          shared->SetError(enc_st);
          break;
        }
        payload_ptr = encrypted_payload.data();
        payload_len = encrypted_payload.size();
        if (item.codec == ChunkCodec::kLz4) {
          store_codec = ChunkCodec::kEncryptedLz4;
        } else if (item.codec == ChunkCodec::kZstd) {
          store_codec = ChunkCodec::kEncryptedZstd;
        } else {
          store_codec = ChunkCodec::kEncrypted;
        }
      }
      const Status put_st = shared->store->PutPrecompressed(
          item.descriptor.hash, payload_ptr, payload_len, item.uncompressed_len,
          store_codec, &newly_written, true);
      if (!put_st.ok()) {
        shared->SetError(put_st);
        break;
      }
      if (shared->stats) {
        shared->RecordChunkStored(newly_written);
      }
    } else if (shared->stats) {
      shared->RecordChunkStored(false);
    }

    RecordStoredChunkHash(file, item.chunk_index, item.descriptor.hash, shared);
    if (!shared->CurrentError().ok()) break;
    const auto t1 = std::chrono::steady_clock::now();
    shared->AddPhaseNs(
        shared->phase_stats ? &shared->phase_stats->store_ns : nullptr,
        static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
  }
}

}  // namespace

Status RunBackupPipeline(const std::vector<std::string>& file_paths,
                           const std::string& source_root, BackupMode mode,
                           const ManifestDocument* prior_manifest,
                           ChunkStore* chunk_store, BackupStats* stats,
                           const BackupPipelineOptions& options,
                           BackupPipelineResult* result,
                           ProgressCallback progress_cb, void* progress_user) {
  if (!chunk_store || !stats || !result) {
    return Status::InvalidArgument("null pipeline argument");
  }

  PipelineShared shared{};
  shared.mode = mode;
  shared.prior = prior_manifest;
  shared.store = chunk_store;
  shared.stats = stats;
  shared.result = result;
  shared.options = options;
  shared.progress_cb = progress_cb;
  shared.progress_user = progress_user;
  shared.phase_stats = options.phase_stats;
  shared.total_files = file_paths.size();
  result->manifest_files.clear();
  result->manifest_files.resize(file_paths.size());

  chunk_store->SetPipelineDedupTrust(true);

  uint64_t total_bytes = 0;
  std::vector<ScheduledFileInput> schedule_in;
  schedule_in.reserve(file_paths.size());
  for (size_t i = 0; i < file_paths.size(); ++i) {
    ScheduledFileInput sf{};
    sf.index = i;
    sf.path = file_paths[i];
    const Status rel_st =
        RelativePathFromRoot(source_root, file_paths[i], &sf.relative_path);
    if (!rel_st.ok()) return rel_st;
    std::error_code ec;
    sf.file_size = std::filesystem::file_size(PathFromUtf8(file_paths[i]), ec);
    total_bytes += sf.file_size;
    schedule_in.push_back(std::move(sf));
  }

  if (file_paths.size() == 1 && options.worker_count == 0 &&
      total_bytes <= kStreamFeedBytes) {
    const Status inline_st = RunSingleFileInlinePipeline(
        file_paths[0], schedule_in[0].relative_path, 0, &shared);
    chunk_store->SetPipelineDedupTrust(false);
    if (!inline_st.ok()) return inline_st;
    result->manifest_files.erase(
        std::remove_if(result->manifest_files.begin(),
                       result->manifest_files.end(),
                       [](const ManifestFileEntry& e) {
                         return e.relative_path.empty();
                       }),
        result->manifest_files.end());
    return Status::Ok();
  }

  if (file_paths.size() == 1 && total_bytes > kStreamFeedBytes &&
      mode == BackupMode::kFull && options.worker_count == 0 &&
      !PipelineWorkersExplicitlyRequested(options.worker_count)) {
    const Status stream_st = RunSingleFileStreamingChunkPipeline(
        file_paths[0], schedule_in[0].relative_path, 0, &shared);
    chunk_store->SetPipelineDedupTrust(false);
    if (!stream_st.ok()) return stream_st;
    result->manifest_files.erase(
        std::remove_if(result->manifest_files.begin(),
                       result->manifest_files.end(),
                       [](const ManifestFileEntry& e) {
                         return e.relative_path.empty();
                       }),
        result->manifest_files.end());
    return Status::Ok();
  }

  size_t pipeline_workers = ResolvePipelineWorkerCount(
      options.worker_count, file_paths.size(), total_bytes);
  if (options.worker_count == 0 && file_paths.size() <= 1 &&
      total_bytes < 64u * 1024u * 1024u) {
    pipeline_workers = 1;
  }
  const size_t file_workers = file_paths.size() <= 1 ? 1 : pipeline_workers;
  const size_t compressor_workers = pipeline_workers;
  const size_t queue_depth = options.queue_depth;
  const size_t inter_stage_depth =
      std::max(queue_depth * 64, static_cast<size_t>(1024));
  shared.compressors_remaining.store(compressor_workers, std::memory_order_release);
  shared.chunkers_remaining.store(file_workers, std::memory_order_release);

  const auto worker_queues = ScheduleFilesByColor(schedule_in, file_workers);

  BoundedQueue<ChunkTask> chunk_q(inter_stage_depth);
  BoundedQueue<EncodedChunkTask> encoded_q(inter_stage_depth);
  shared.chunk_q = &chunk_q;
  shared.encoded_q = &encoded_q;
  std::vector<std::thread> workers;
  std::vector<std::shared_ptr<BoundedQueue<FileInput>>> file_queues;
  std::vector<std::shared_ptr<BoundedQueue<FileData>>> read_queues;
  workers.reserve(file_workers * 2 + compressor_workers + 8);

  for (size_t w = 0; w < file_workers; ++w) {
    auto file_q = std::make_shared<BoundedQueue<FileInput>>(queue_depth);
    auto read_q = std::make_shared<BoundedQueue<FileData>>(queue_depth);
    file_queues.push_back(file_q);
    read_queues.push_back(read_q);

    for (const auto& sf : worker_queues[w]) {
      FileInput input{};
      input.index = sf.index;
      input.path = sf.path;
      input.relative_path = sf.relative_path;
      file_q->Push(std::move(input));
    }
    file_q->Close();

    workers.emplace_back(ReaderStage, file_q.get(), read_q.get(), &shared);
    workers.emplace_back(ChunkerStage, read_q.get(), &chunk_q, &shared);
  }

  for (size_t c = 0; c < compressor_workers; ++c) {
    workers.emplace_back(CompressorWorker, &chunk_q, &encoded_q, &shared);
  }

  const size_t store_shard_count =
      options.store_shard_count > 0 ? options.store_shard_count : 16;
  size_t store_workers = std::max<size_t>(2, store_shard_count / 2);
  if (options.worker_count == 0 && file_paths.size() <= 1 &&
      total_bytes < 64u * 1024u * 1024u) {
    store_workers = 1;
  }
  for (size_t s = 0; s < store_workers; ++s) {
    workers.emplace_back(StoreWorker, &encoded_q, &shared);
  }

  for (auto& t : workers) {
    if (t.joinable()) t.join();
  }

  chunk_store->SetPipelineDedupTrust(false);

  const Status err = shared.CurrentError();
  if (!err.ok()) return err;

  result->manifest_files.erase(
      std::remove_if(result->manifest_files.begin(), result->manifest_files.end(),
                     [](const ManifestFileEntry& e) {
                       return e.relative_path.empty();
                     }),
      result->manifest_files.end());
  return Status::Ok();
}

}  // namespace ebbackup
