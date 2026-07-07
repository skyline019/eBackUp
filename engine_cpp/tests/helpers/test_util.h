#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "ebbackup/common/status.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/state/superblock.h"

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

}  // namespace test
}  // namespace ebbackup
