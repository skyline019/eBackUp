#include "ebbackup/codec/zstd_dict.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <zdict.h>
#include <zstd.h>

#include "ebbackup/codec/content_class.h"
#include "ebbackup/common/path_encoding.h"

namespace ebbackup {

namespace {

constexpr char kDictMagic[4] = {'E', 'B', 'Z', 'D'};
constexpr uint32_t kDictVersion = 1;

void FreeCDict(const ZSTD_CDict_s* ptr) {
  if (ptr) ZSTD_freeCDict(const_cast<ZSTD_CDict_s*>(ptr));
}

void FreeDDict(const ZSTD_DDict_s* ptr) {
  if (ptr) ZSTD_freeDDict(const_cast<ZSTD_DDict_s*>(ptr));
}

}  // namespace

ZstdDictionary::ZstdDictionary() = default;

ZstdDictionary::~ZstdDictionary() = default;

void ZstdDictionary::ReplaceWith(ZstdDictionary&& other) {
  ClearCaches();
  other.ClearCaches();
  dict_bytes_ = std::move(other.dict_bytes_);
  other.dict_bytes_.clear();
}

void ZstdDictionary::ClearCaches() {
  std::lock_guard<std::mutex> lock(cache_mu_);
  cdict_.reset();
  ddict_.reset();
  cdict_level_ = -1;
}

Status ZstdDictionary::RebuildCaches() const {
  if (dict_bytes_.empty()) return Status::Ok();
  std::lock_guard<std::mutex> lock(cache_mu_);
  if (!ddict_) {
    ZSTD_DDict_s* created =
        ZSTD_createDDict(dict_bytes_.data(), dict_bytes_.size());
    if (!created) return Status::Internal("zstd createDDict failed");
    ddict_.reset(created, FreeDDict);
  }
  return Status::Ok();
}

Status ZstdDictionary::LoadFromFile(const std::string& path) {
  std::ifstream in(PathFromUtf8(path), std::ios::binary);
  if (!in) return Status::IoError("cannot open zstd dict: " + path);

  char magic[4]{};
  in.read(magic, sizeof(magic));
  if (!in) return Status::Corrupt("zstd dict header short");
  if (std::memcmp(magic, kDictMagic, sizeof(kDictMagic)) != 0) {
    return Status::Corrupt("zstd dict bad magic");
  }

  uint32_t version = 0;
  in.read(reinterpret_cast<char*>(&version), sizeof(version));
  if (!in || version != kDictVersion) {
    return Status::Corrupt("zstd dict unsupported version");
  }

  uint32_t size = 0;
  in.read(reinterpret_cast<char*>(&size), sizeof(size));
  if (!in || size == 0 || size > (8u * 1024u * 1024u)) {
    return Status::Corrupt("zstd dict invalid size");
  }

  dict_bytes_.resize(size);
  in.read(reinterpret_cast<char*>(dict_bytes_.data()),
          static_cast<std::streamsize>(size));
  if (!in) return Status::Corrupt("zstd dict payload short");

  ClearCaches();
  return RebuildCaches();
}

Status ZstdDictionary::SaveToFile(const std::string& path) const {
  if (dict_bytes_.empty()) return Status::InvalidArgument("empty zstd dict");

  const std::filesystem::path fs_path = PathFromUtf8(path);
  std::error_code ec;
  std::filesystem::create_directories(fs_path.parent_path(), ec);

  std::ofstream out(fs_path, std::ios::binary | std::ios::trunc);
  if (!out) return Status::IoError("cannot write zstd dict: " + path);

  out.write(kDictMagic, sizeof(kDictMagic));
  const uint32_t version = kDictVersion;
  out.write(reinterpret_cast<const char*>(&version), sizeof(version));
  const uint32_t size = static_cast<uint32_t>(dict_bytes_.size());
  out.write(reinterpret_cast<const char*>(&size), sizeof(size));
  out.write(reinterpret_cast<const char*>(dict_bytes_.data()),
            static_cast<std::streamsize>(dict_bytes_.size()));
  if (!out) return Status::IoError("zstd dict write failed");
  return Status::Ok();
}

Status ZstdDictionary::TrainFromSamples(
    const std::vector<std::vector<uint8_t>>& samples, size_t max_dict_bytes) {
  if (samples.size() < 8) {
    return Status::InvalidArgument("need at least 8 samples to train dict");
  }

  std::vector<size_t> sizes;
  std::vector<const void*> buffers;
  sizes.reserve(samples.size());
  buffers.reserve(samples.size());
  for (const auto& sample : samples) {
    if (sample.empty()) continue;
    sizes.push_back(sample.size());
    buffers.push_back(sample.data());
  }
  if (buffers.size() < 8) {
    return Status::InvalidArgument("insufficient non-empty dict samples");
  }

  const size_t dict_capacity = std::max<size_t>(1024, max_dict_bytes);
  std::vector<uint8_t> trained(dict_capacity);
  const size_t trained_size = ZDICT_trainFromBuffer(
      trained.data(), dict_capacity, buffers.data(), sizes.data(),
      static_cast<unsigned>(buffers.size()));
  if (ZDICT_isError(trained_size)) {
    return Status::Internal(std::string("zdict train failed: ") +
                            ZDICT_getErrorName(trained_size));
  }
  trained.resize(trained_size);
  dict_bytes_ = std::move(trained);
  ClearCaches();
  return RebuildCaches();
}

std::shared_ptr<const ZSTD_CDict_s> ZstdDictionary::AcquireCDict(int level) const {
  if (dict_bytes_.empty()) return {};
  std::lock_guard<std::mutex> lock(cache_mu_);
  if (cdict_ && cdict_level_ == level) return cdict_;
  ZSTD_CDict_s* created =
      ZSTD_createCDict(dict_bytes_.data(), dict_bytes_.size(), level);
  if (!created) return {};
  cdict_.reset(created, FreeCDict);
  cdict_level_ = level;
  return cdict_;
}

std::shared_ptr<const ZSTD_DDict_s> ZstdDictionary::AcquireDDict() const {
  if (dict_bytes_.empty()) return {};
  std::lock_guard<std::mutex> lock(cache_mu_);
  if (ddict_) return ddict_;
  ZSTD_DDict_s* created =
      ZSTD_createDDict(dict_bytes_.data(), dict_bytes_.size());
  if (!created) return {};
  ddict_.reset(created, FreeDDict);
  return ddict_;
}

void ZstdDictTrainer::Reset() {
  std::lock_guard<std::mutex> lock(samples_mu_);
  samples_.clear();
}

void ZstdDictTrainer::MaybeAddSample(const uint8_t* data, size_t len,
                                     ContentDataClass cls) {
  if (!data || len == 0) return;
  if (cls == ContentDataClass::kIncompressible) return;
  std::lock_guard<std::mutex> lock(samples_mu_);
  if (samples_.size() >= max_samples_) return;

  const size_t sample_len = std::min(len, max_sample_bytes_);
  samples_.emplace_back(data, data + sample_len);
}

size_t ZstdDictTrainer::sample_count() const {
  std::lock_guard<std::mutex> lock(samples_mu_);
  return samples_.size();
}

Status ZstdDictTrainer::FinalizeAndSave(const std::string& repo_path,
                                        ZstdDictionary* out_dict) const {
  if (!out_dict) return Status::InvalidArgument("out_dict is null");
  std::vector<std::vector<uint8_t>> samples;
  {
    std::lock_guard<std::mutex> lock(samples_mu_);
    if (samples_.size() < 8) return Status::Ok();
    samples = samples_;
  }

  ZstdDictionary trained;
  const Status train_st = trained.TrainFromSamples(samples);
  if (!train_st.ok()) return train_st;

  const Status save_st = SaveRepoZstdDictionary(repo_path, trained);
  if (!save_st.ok()) return save_st;

  out_dict->ReplaceWith(std::move(trained));
  return Status::Ok();
}

std::string ZstdDictionaryPath(const std::string& repo_path) {
  return repo_path + "/meta/zstd_dict.bin";
}

Status LoadRepoZstdDictionary(const std::string& repo_path,
                              ZstdDictionary* out) {
  if (!out) return Status::InvalidArgument("out is null");
  const std::string path = ZstdDictionaryPath(repo_path);
  std::error_code ec;
  if (!std::filesystem::exists(PathFromUtf8(path), ec)) {
    return Status::Ok();
  }
  return out->LoadFromFile(path);
}

Status SaveRepoZstdDictionary(const std::string& repo_path,
                              const ZstdDictionary& dict) {
  if (dict.empty()) return Status::Ok();
  return dict.SaveToFile(ZstdDictionaryPath(repo_path));
}

}  // namespace ebbackup
