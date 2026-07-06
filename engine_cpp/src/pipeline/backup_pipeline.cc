#include "ebbackup/pipeline/backup_pipeline.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include "ebbackup/chunk/eb_hcrbo.h"
#include "ebbackup/chunk/rolling_checksum.h"
#include "ebbackup/codec/lz4_codec.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/path_util.h"
#include "ebbackup/crypto/aes_gcm.h"
#include "ebbackup/io/mmap_reader.h"

namespace ebbackup {

namespace {

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

template <typename T>
class BoundedQueue {
 public:
  explicit BoundedQueue(size_t capacity) : capacity_(capacity) {}

  void Push(T value) {
    std::unique_lock<std::mutex> lock(mu_);
    not_full_.wait(lock, [this] { return closed_ || queue_.size() < capacity_; });
    if (closed_) return;
    queue_.push_back(std::move(value));
    not_empty_.notify_one();
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

struct FileInput {
  size_t index{0};
  std::string path;
  std::string relative_path;
};

struct FileData {
  size_t index{0};
  std::string relative_path;
  std::vector<uint8_t> bytes;
};

struct ChunkedFile {
  size_t index{0};
  std::string relative_path;
  std::vector<uint8_t> bytes;
  std::vector<ChunkDescriptor> chunks;
  CfiIndex cfi;
};

struct EncodedChunk {
  ChunkDescriptor descriptor;
  std::vector<uint8_t> payload;
  ChunkCodec codec{ChunkCodec::kRaw};
  uint32_t uncompressed_len{0};
};

struct EncodedFile {
  size_t index{0};
  std::string relative_path;
  uint64_t file_size{0};
  CfiIndex cfi;
  std::vector<ChunkDescriptor> chunks;
  std::vector<EncodedChunk> encoded;
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
  std::atomic<StatusCode> error{StatusCode::kOk};
  std::string error_message;
  mutable std::mutex error_mu;
  size_t total_files{0};
  std::atomic<size_t> files_read{0};
  std::atomic<size_t> files_chunked{0};
  std::atomic<size_t> files_encoded{0};
  std::atomic<size_t> files_stored{0};

  void SetError(const Status& st) {
    if (st.ok()) return;
    std::lock_guard<std::mutex> lock(error_mu);
    if (error.load() == StatusCode::kOk) {
      error.store(st.code());
      error_message = st.message();
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
};

Status EncodeChunk(const uint8_t* data, size_t len, bool use_lz4,
                   EncodedChunk* out) {
  if (!out) return Status::InvalidArgument("out is null");
  if (use_lz4) {
    Lz4EncodeResult encoded{};
    const Status enc = Lz4Compress(data, len, &encoded);
    if (!enc.ok()) return enc;
    if (encoded.compressed) {
      out->payload = std::move(encoded.payload);
      out->codec = ChunkCodec::kLz4;
      out->uncompressed_len = encoded.uncompressed_size;
      return Status::Ok();
    }
  }
  out->payload.assign(data, data + len);
  out->codec = ChunkCodec::kRaw;
  out->uncompressed_len = static_cast<uint32_t>(len);
  return Status::Ok();
}

void ReaderStage(BoundedQueue<FileInput>* in, BoundedQueue<FileData>* out,
                 PipelineShared* shared) {
  FileInput item{};
  while (in->Pop(&item)) {
    if (!shared->CurrentError().ok()) break;
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
      data.bytes.assign(reader.data(), reader.data() + reader.size());
    } else {
      std::ifstream file(item.path, std::ios::binary);
      if (!file) {
        shared->SetError(Status::IoError("read failed: " + item.path));
        break;
      }
      data.bytes.assign(std::istreambuf_iterator<char>(file),
                         std::istreambuf_iterator<char>());
    }
    out->Push(std::move(data));
    const size_t done = ++shared->files_read;
    if (shared->total_files > 0) {
      const uint64_t pct = 50 + static_cast<uint64_t>(done * 100 / shared->total_files);
      shared->EmitProgress(pct);
    }
  }
  out->Close();
}

void ChunkerStage(BoundedQueue<FileData>* in, BoundedQueue<ChunkedFile>* out,
                  PipelineShared* shared) {
  EbHcrboChunker chunker;
  FileData item{};
  while (in->Pop(&item)) {
    if (!shared->CurrentError().ok()) break;

    ChunkedFile chunked{};
    chunked.index = item.index;
    chunked.relative_path = item.relative_path;
    chunked.bytes = std::move(item.bytes);
    EbHcrboStats hstats{};
    Status ch_st;
    if (shared->mode == BackupMode::kIncremental && shared->prior) {
      const CfiIndex history =
          FindPriorCfi(*shared->prior, chunked.relative_path);
      ch_st = chunker.ChunkIncremental(chunked.bytes.data(), chunked.bytes.size(),
                                       history, &chunked.chunks, &chunked.cfi,
                                       &hstats);
    } else {
      ch_st = chunker.ChunkFull(chunked.bytes.data(), chunked.bytes.size(),
                                &chunked.chunks, &chunked.cfi, &hstats);
    }
    if (!ch_st.ok()) {
      shared->SetError(ch_st);
      break;
    }
    PopulateAnchorChecksums(chunked.bytes.data(), chunked.bytes.size(),
                            &chunked.cfi);
    if (shared->stats) {
      shared->stats->chunks_reused_from_cfi += hstats.chunks_reused_from_cfi;
    }
    out->Push(std::move(chunked));
    const size_t done = ++shared->files_chunked;
    if (shared->total_files > 0) {
      const uint64_t pct = 150 + static_cast<uint64_t>(done * 250 / shared->total_files);
      shared->EmitProgress(pct);
    }
  }
  out->Close();
}

void CompressorStage(BoundedQueue<ChunkedFile>* in, BoundedQueue<EncodedFile>* out,
                     PipelineShared* shared) {
  ChunkedFile item{};
  while (in->Pop(&item)) {
    if (!shared->CurrentError().ok()) break;

    EncodedFile encoded{};
    encoded.index = item.index;
    encoded.relative_path = item.relative_path;
    encoded.file_size = item.bytes.size();
    encoded.cfi = item.cfi;
    encoded.chunks = item.chunks;
    encoded.encoded.reserve(item.chunks.size());
    for (const auto& chunk : item.chunks) {
      EncodedChunk enc{};
      enc.descriptor = chunk;
      const Status st = EncodeChunk(item.bytes.data() + chunk.offset, chunk.length,
                                    shared->options.use_lz4, &enc);
      if (!st.ok()) {
        shared->SetError(st);
        break;
      }
      encoded.encoded.push_back(std::move(enc));
    }
    if (!shared->CurrentError().ok()) break;
    out->Push(std::move(encoded));
    const size_t done = ++shared->files_encoded;
    if (shared->total_files > 0) {
      const uint64_t pct = 400 + static_cast<uint64_t>(done * 200 / shared->total_files);
      shared->EmitProgress(pct);
    }
  }
  out->Close();
}

void StoreStage(BoundedQueue<EncodedFile>* in, PipelineShared* shared) {
  EncodedFile item{};
  while (in->Pop(&item)) {
    if (!shared->CurrentError().ok()) break;

    ManifestFileEntry entry;
    entry.relative_path = item.relative_path;
    entry.size = item.file_size;
    entry.cfi = item.cfi;
    entry.chunk_hashes_hex.reserve(item.chunks.size());

    for (size_t ci = 0; ci < item.chunks.size(); ++ci) {
      const auto& chunk = item.chunks[ci];
      const auto& enc = item.encoded[ci];
      entry.chunk_hashes_hex.push_back(BytesToHex(chunk.hash, 32));

      if (chunk.reused_from_cfi && shared->store->Exists(chunk.hash)) {
        if (shared->stats) ++shared->stats->chunks_reused;
        continue;
      }

      bool newly_written = false;
      Status put_st;
      ChunkStorePutOptions put_opts{};
      put_opts.use_encryption = shared->options.use_encryption;
      put_opts.content_key = shared->options.content_key;
      if (enc.codec == ChunkCodec::kLz4) {
        ChunkCodec store_codec = ChunkCodec::kLz4;
        const uint8_t* payload_ptr = enc.payload.data();
        size_t payload_len = enc.payload.size();
        std::vector<uint8_t> encrypted_payload;
        if (put_opts.use_encryption) {
          if (!put_opts.content_key) {
            shared->SetError(
                Status::InvalidArgument("encryption requires content key"));
            break;
          }
          const Status enc_st = crypto::Aes256GcmEncrypt(
              put_opts.content_key, enc.payload.data(), enc.payload.size(),
              &encrypted_payload);
          if (!enc_st.ok()) {
            shared->SetError(enc_st);
            break;
          }
          payload_ptr = encrypted_payload.data();
          payload_len = encrypted_payload.size();
          store_codec = ChunkCodec::kEncryptedLz4;
        }
        put_st = shared->store->PutPrecompressed(
            chunk.hash, payload_ptr, payload_len, enc.uncompressed_len,
            store_codec, &newly_written);
      } else {
        put_st = shared->store->PutKnownHash(
            enc.payload.data(), enc.payload.size(), chunk.hash, &newly_written,
            put_opts.use_encryption ? &put_opts : nullptr);
      }
      if (!put_st.ok()) {
        shared->SetError(put_st);
        break;
      }
      if (shared->stats) {
        if (newly_written) {
          ++shared->stats->chunks_written;
        } else {
          ++shared->stats->chunks_reused;
        }
      }
    }
    if (!shared->CurrentError().ok()) break;

    if (shared->stats) {
      shared->stats->bytes_processed += item.file_size;
      ++shared->stats->files_processed;
    }
    if (shared->result) {
      if (shared->result->manifest_files.size() <= item.index) {
        shared->result->manifest_files.resize(item.index + 1);
      }
      shared->result->manifest_files[item.index] = std::move(entry);
    }
    const size_t done = ++shared->files_stored;
    if (shared->total_files > 0) {
      const uint64_t pct = 600 + static_cast<uint64_t>(done * 350 / shared->total_files);
      shared->EmitProgress(pct);
    }
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
  shared.total_files = file_paths.size();
  result->manifest_files.clear();
  result->manifest_files.resize(file_paths.size());

  BoundedQueue<FileInput> file_q(options.queue_depth);
  BoundedQueue<FileData> read_q(options.queue_depth);
  BoundedQueue<ChunkedFile> chunk_q(options.queue_depth);
  BoundedQueue<EncodedFile> encode_q(options.queue_depth);

  std::thread reader(ReaderStage, &file_q, &read_q, &shared);
  std::thread chunker(ChunkerStage, &read_q, &chunk_q, &shared);
  std::thread compressor(CompressorStage, &chunk_q, &encode_q, &shared);
  std::thread store(StoreStage, &encode_q, &shared);

  for (size_t i = 0; i < file_paths.size(); ++i) {
    FileInput input{};
    input.index = i;
    input.path = file_paths[i];
    const Status rel_st =
        RelativePathFromRoot(source_root, file_paths[i], &input.relative_path);
    if (!rel_st.ok()) {
      shared.SetError(rel_st);
      break;
    }
    file_q.Push(std::move(input));
  }
  file_q.Close();

  reader.join();
  chunker.join();
  compressor.join();
  store.join();

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
