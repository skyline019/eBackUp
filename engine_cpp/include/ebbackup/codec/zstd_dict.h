#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"

struct ZSTD_CDict_s;
struct ZSTD_DDict_s;

namespace ebbackup {

enum class ContentDataClass;

class ZstdDictionary {
 public:
  ZstdDictionary();
  ~ZstdDictionary();

  ZstdDictionary(const ZstdDictionary&) = delete;
  ZstdDictionary& operator=(const ZstdDictionary&) = delete;
  ZstdDictionary(ZstdDictionary&&) = delete;
  ZstdDictionary& operator=(ZstdDictionary&&) = delete;

  void ReplaceWith(ZstdDictionary&& other);

  bool empty() const { return dict_bytes_.empty(); }
  const uint8_t* data() const { return dict_bytes_.data(); }
  size_t size() const { return dict_bytes_.size(); }

  Status LoadFromFile(const std::string& path);
  Status SaveToFile(const std::string& path) const;
  Status TrainFromSamples(const std::vector<std::vector<uint8_t>>& samples,
                          size_t max_dict_bytes = 112u * 1024u);

  std::shared_ptr<const ZSTD_CDict_s> AcquireCDict(int level) const;
  std::shared_ptr<const ZSTD_DDict_s> AcquireDDict() const;

 private:
  void ClearCaches();
  Status RebuildCaches() const;

  std::vector<uint8_t> dict_bytes_;
  mutable std::mutex cache_mu_;
  mutable std::shared_ptr<const ZSTD_CDict_s> cdict_;
  mutable int cdict_level_{-1};
  mutable std::shared_ptr<const ZSTD_DDict_s> ddict_;
};

class ZstdDictTrainer {
 public:
  void MaybeAddSample(const uint8_t* data, size_t len, ContentDataClass cls);
  void Reset();
  Status FinalizeAndSave(const std::string& repo_path,
                         ZstdDictionary* out_dict) const;

  size_t sample_count() const;

 private:
  mutable std::mutex samples_mu_;
  std::vector<std::vector<uint8_t>> samples_;
  size_t max_samples_{100};
  size_t max_sample_bytes_{16u * 1024u};
};

std::string ZstdDictionaryPath(const std::string& repo_path);

Status LoadRepoZstdDictionary(const std::string& repo_path,
                              ZstdDictionary* out);

Status SaveRepoZstdDictionary(const std::string& repo_path,
                              const ZstdDictionary& dict);

}  // namespace ebbackup
