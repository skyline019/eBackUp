#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "ebbackup/common/status.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/state/superblock.h"

#if defined(_WIN32)
#include <stdlib.h>
#endif

namespace ebbackup {
namespace test {

inline Status InitLegacyRepo(const std::string& repo) {
  return BackupEngine::InitRepo(repo, false);
}

inline Status InitStandardRepo(const std::string& repo) {
  return BackupEngine::InitRepo(repo, true);
}

inline Status InitV03Repo(const std::string& repo) {
  RepoInitOptions opts{};
  opts.standard_digest = true;
  opts.persistent_index = true;
  opts.manifest_binary = true;
  opts.snapshots = true;
  return BackupEngine::InitRepoEx(repo, opts);
}

inline Status InitV05Repo(const std::string& repo) {
  RepoInitOptions opts{};
  opts.standard_digest = true;
  opts.persistent_index = true;
  opts.manifest_binary = true;
  opts.snapshots = true;
  opts.ebpack = true;
  opts.coalesced_meta = true;
  return BackupEngine::InitRepoEx(repo, opts);
}

inline Status InitLegacyStorageRepo(const std::string& repo) {
  return InitV03Repo(repo);
}

inline Status InitDefaultRepo(const std::string& repo) {
  return InitV05Repo(repo);
}

#if defined(_WIN32)
inline void SetEnvVar(const char* key, const char* value) {
  _putenv_s(key, value);
}
inline void ClearEnvVar(const char* key) {
  _putenv_s(key, "");
}
#else
inline void SetEnvVar(const char* key, const char* value) {
  setenv(key, value, 1);
}
inline void ClearEnvVar(const char* key) {
  unsetenv(key);
}
#endif

inline Status InitGtCdcV4Repo(const std::string& repo) {
  const Status st = InitV05Repo(repo);
  if (!st.ok()) return st;
  BackupSuperBlockStore store(repo + "/superblock.bin");
  BackupSuperBlock sb{};
  const Status load = store.Load(&sb);
  if (!load.ok()) return load;
  sb.ext.backup_features |= kBackupFeatureGtCdc;
  sb.ext.backup_features |= kBackupFeatureGtCdcNative;
  sb.ext.backup_features &= ~kBackupFeatureGtCdcAnGear;
  if (sb.ext.gtcdc_table_seed == 0) {
    sb.ext.gtcdc_table_seed = 0xA4B4C4D4u;
  }
  if (sb.ext.gtcdc_nc_level == 0) {
    sb.ext.gtcdc_nc_level = 2;
  }
  return store.Commit(sb);
}

inline Status InitGtCdcV5Repo(const std::string& repo) {
  const char* prev = std::getenv("EBBACKUP_CDC_ALGO");
  const std::string saved = prev ? prev : "";
  SetEnvVar("EBBACKUP_CDC_ALGO", "gtcdc");
  const Status st = InitV05Repo(repo);
  if (saved.empty()) {
    ClearEnvVar("EBBACKUP_CDC_ALGO");
  } else {
    SetEnvVar("EBBACKUP_CDC_ALGO", saved.c_str());
  }
  if (!st.ok()) return st;
  BackupSuperBlockStore store(repo + "/superblock.bin");
  BackupSuperBlock sb{};
  const Status load = store.Load(&sb);
  if (!load.ok()) return load;
  sb.ext.backup_features |= kBackupFeatureGtCdc;
  sb.ext.backup_features |= kBackupFeatureGtCdcAnGear;
  sb.ext.backup_features &= ~kBackupFeatureGtCdcTwoFGear;
  if (sb.ext.gtcdc_table_seed == 0) {
    sb.ext.gtcdc_table_seed = 0xA4B4C4D4u;
  }
  if (sb.ext.gtcdc_nc_level == 0) {
    sb.ext.gtcdc_nc_level = 2;
  }
  return store.Commit(sb);
}

inline Status InitGtCdcV6Repo(const std::string& repo) {
  const char* prev = std::getenv("EBBACKUP_CDC_ALGO");
  const std::string saved = prev ? prev : "";
  SetEnvVar("EBBACKUP_CDC_ALGO", "gtcdc");
  const Status st = InitV05Repo(repo);
  if (saved.empty()) {
    ClearEnvVar("EBBACKUP_CDC_ALGO");
  } else {
    SetEnvVar("EBBACKUP_CDC_ALGO", saved.c_str());
  }
  if (!st.ok()) return st;
  BackupSuperBlockStore store(repo + "/superblock.bin");
  BackupSuperBlock sb{};
  const Status load = store.Load(&sb);
  if (!load.ok()) return load;
  sb.ext.backup_features |= kBackupFeatureGtCdc;
  sb.ext.backup_features |= kBackupFeatureGtCdcTwoFGear;
  sb.ext.backup_features &= ~kBackupFeatureGtCdcAnGear;
  if (sb.ext.gtcdc_table_seed == 0) {
    sb.ext.gtcdc_table_seed = 0xA4B4C4D4u;
  }
  if (sb.ext.gtcdc_nc_level == 0) {
    sb.ext.gtcdc_nc_level = 2;
  }
  return store.Commit(sb);
}

inline Status InitTopoRepo(const std::string& repo) {
  const char* prev = std::getenv("EBBACKUP_CDC_ALGO");
  const std::string saved = prev ? prev : "";
  SetEnvVar("EBBACKUP_CDC_ALGO", "topocdc");
  const Status st = InitV05Repo(repo);
  if (saved.empty()) {
    ClearEnvVar("EBBACKUP_CDC_ALGO");
  } else {
    SetEnvVar("EBBACKUP_CDC_ALGO", saved.c_str());
  }
  return st;
}

inline Status InitTopoChainRepo(const std::string& repo) {
  const char* prev = std::getenv("EBBACKUP_CDC_ALGO");
  const std::string saved = prev ? prev : "";
  SetEnvVar("EBBACKUP_CDC_ALGO", "topochain");
  const Status st = InitV05Repo(repo);
  if (saved.empty()) {
    ClearEnvVar("EBBACKUP_CDC_ALGO");
  } else {
    SetEnvVar("EBBACKUP_CDC_ALGO", saved.c_str());
  }
  return st;
}

inline Status InitTopoPhRepo(const std::string& repo) {
  const char* prev = std::getenv("EBBACKUP_CDC_ALGO");
  const std::string saved = prev ? prev : "";
  SetEnvVar("EBBACKUP_CDC_ALGO", "topoph");
  const Status st = InitV05Repo(repo);
  if (saved.empty()) {
    ClearEnvVar("EBBACKUP_CDC_ALGO");
  } else {
    SetEnvVar("EBBACKUP_CDC_ALGO", saved.c_str());
  }
  return st;
}

inline Status InitTopoPhnRepo(const std::string& repo) {
  const char* prev = std::getenv("EBBACKUP_CDC_ALGO");
  const std::string saved = prev ? prev : "";
  SetEnvVar("EBBACKUP_CDC_ALGO", "topophn");
  const Status st = InitV05Repo(repo);
  if (saved.empty()) {
    ClearEnvVar("EBBACKUP_CDC_ALGO");
  } else {
    SetEnvVar("EBBACKUP_CDC_ALGO", saved.c_str());
  }
  return st;
}

inline bool RepoHasGtCdcNative(const std::string& repo_path) {
  BackupSuperBlockStore store(repo_path + "/superblock.bin");
  BackupSuperBlock sb{};
  if (!store.Load(&sb).ok()) return false;
  return RepoUsesGtCdcNative(sb);
}

inline bool RepoHasGtCdcAnGear(const std::string& repo_path) {
  BackupSuperBlockStore store(repo_path + "/superblock.bin");
  BackupSuperBlock sb{};
  if (!store.Load(&sb).ok()) return false;
  return RepoUsesGtCdcAnGear(sb);
}

inline bool RepoHasGtCdcTwoFGear(const std::string& repo_path) {
  BackupSuperBlockStore store(repo_path + "/superblock.bin");
  BackupSuperBlock sb{};
  if (!store.Load(&sb).ok()) return false;
  return RepoUsesGtCdcTwoFGear(sb);
}

inline std::filesystem::path TestOutputRoot() {
  if (const char* env = std::getenv("EBTEST_TMPDIR")) {
    if (env[0] != '\0') return std::filesystem::path(env);
  }
#ifdef EBTEST_OUTPUT_DIR
  return std::filesystem::path(EBTEST_OUTPUT_DIR);
#else
  return std::filesystem::path("test_output");
#endif
}

inline std::string TempDir(const std::string& prefix) {
  const auto base = TestOutputRoot();
  std::filesystem::create_directories(base);
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<uint64_t> dist;
  const auto path = base / (prefix + "_" + std::to_string(dist(gen)));
  std::filesystem::create_directories(path);
  return path.string();
}

inline void WriteFile(const std::string& path, const std::string& content) {
  std::filesystem::create_directories(
      std::filesystem::path(path).parent_path());
  std::ofstream out(path, std::ios::binary);
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

inline std::string MakeSyntheticData(size_t size, uint8_t seed = 0) {
  std::string data(size, '\0');
  for (size_t i = 0; i < size; ++i) {
    data[i] = static_cast<char>((seed + i * 17 + (i >> 8)) & 0xFF);
  }
  return data;
}

inline std::string MakeRandomData(size_t size, uint32_t seed = 1) {
  std::mt19937 gen(seed);
  std::uniform_int_distribution<int> dist(0, 255);
  std::string data(size, '\0');
  for (size_t i = 0; i < size; ++i) {
    data[i] = static_cast<char>(dist(gen));
  }
  return data;
}

inline const ManifestFileEntry* FindManifestFile(const ManifestDocument& doc,
                                                 const std::string& rel_path) {
  const ManifestFileEntry* best = nullptr;
  for (const auto& file : doc.files) {
    if (file.relative_path != rel_path) continue;
    if (!best || (!file.chunk_hashes_hex.empty() && best->chunk_hashes_hex.empty())) {
      best = &file;
    }
  }
  return best;
}

}  // namespace test
}  // namespace ebbackup
