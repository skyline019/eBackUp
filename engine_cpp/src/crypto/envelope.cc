#include "ebbackup/crypto/envelope.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include "ebbackup/common/fsync.h"
#include "ebbackup/common/path_encoding.h"
#include "ebbackup/crypto/aes_gcm.h"
#include "ebbackup/engine/restore_options_json.h"

namespace ebbackup {
namespace crypto {
namespace {

constexpr char kRecoveryAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

std::string BytesToHexLocal(const uint8_t* data, size_t len) {
  static const char* hex = "0123456789abcdef";
  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out.push_back(hex[(data[i] >> 4) & 0xF]);
    out.push_back(hex[data[i] & 0xF]);
  }
  return out;
}

bool HexToBytesLocal(const std::string& hex, uint8_t* out, size_t out_len) {
  if (hex.size() != out_len * 2) return false;
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < out_len; ++i) {
    const int hi = nibble(hex[i * 2]);
    const int lo = nibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

std::string Base64Encode(const uint8_t* data, size_t len) {
  static const char* tbl =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3) {
    const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                       ((i + 1 < len) ? (static_cast<uint32_t>(data[i + 1]) << 8) : 0) |
                       ((i + 2 < len) ? static_cast<uint32_t>(data[i + 2]) : 0);
    out.push_back(tbl[(n >> 18) & 63]);
    out.push_back(tbl[(n >> 12) & 63]);
    out.push_back((i + 1 < len) ? tbl[(n >> 6) & 63] : '=');
    out.push_back((i + 2 < len) ? tbl[n & 63] : '=');
  }
  return out;
}

std::vector<uint8_t> Base64Decode(const std::string& in) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::vector<uint8_t> out;
  int buf = 0;
  int bits = 0;
  for (char c : in) {
    if (c == '=') break;
    const int v = val(c);
    if (v < 0) continue;
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<uint8_t>((buf >> bits) & 0xFF));
    }
  }
  return out;
}

void RandomBytes(uint8_t* out, size_t len) {
  std::random_device rd;
  for (size_t i = 0; i < len; ++i) out[i] = static_cast<uint8_t>(rd());
}

Status WrapMasterKey(const uint8_t master_key[32], const std::string& secret,
                     const uint8_t salt[16], DigestAlgo algo,
                     std::vector<uint8_t>* out) {
  uint8_t wrap_key[32];
  const Status dk = DeriveWrapKey(secret, salt, wrap_key, algo);
  if (!dk.ok()) return dk;
  return Aes256GcmEncrypt(wrap_key, master_key, kMasterKeySize, out);
}

Status UnwrapMasterKeyBlob(const std::vector<uint8_t>& blob,
                           const std::string& secret, const uint8_t salt[16],
                           DigestAlgo algo, uint8_t master_key[32]) {
  uint8_t wrap_key[32];
  const Status dk = DeriveWrapKey(secret, salt, wrap_key, algo);
  if (!dk.ok()) return dk;
  std::vector<uint8_t> plain;
  const Status dec =
      Aes256GcmDecrypt(wrap_key, blob.data(), blob.size(), &plain);
  if (!dec.ok()) return dec;
  if (plain.size() != kMasterKeySize) {
    return Status::Corrupt("invalid master key length");
  }
  std::memcpy(master_key, plain.data(), kMasterKeySize);
  return Status::Ok();
}

}  // namespace

std::string EnvelopePath(const std::string& repo_path) {
  return PathToUtf8(PathFromUtf8(repo_path) / "crypto.envelope.json");
}

bool EnvelopeExists(const std::string& repo_path) {
  return std::filesystem::exists(PathFromUtf8(EnvelopePath(repo_path)));
}

std::string GenerateRecoveryKey() {
  std::string key;
  key.reserve(26);
  std::random_device rd;
  for (int i = 0; i < 26; ++i) {
    key.push_back(kRecoveryAlphabet[rd() % (sizeof(kRecoveryAlphabet) - 1)]);
  }
  return key;
}

Status DeriveWrapKey(const std::string& secret, const uint8_t salt[16],
                     uint8_t wrap_key[32], DigestAlgo algo) {
  if (secret.empty()) return Status::InvalidArgument("empty wrap secret");
  Pbkdf2Sha256(algo, reinterpret_cast<const uint8_t*>(secret.data()),
               secret.size(), salt, 16, 100000, wrap_key);
  return Status::Ok();
}

Status LoadEnvelope(const std::string& repo_path, CryptoEnvelope* out) {
  if (!out) return Status::InvalidArgument("out is null");
  const std::string path = EnvelopePath(repo_path);
  std::ifstream in(PathFromUtf8(path));
  if (!in) return Status::NotFound("crypto.envelope.json missing");
  std::ostringstream ss;
  ss << in.rdbuf();
  const std::string json = ss.str();

  CryptoEnvelope env{};
  uint64_t ver = 0;
  if (!ReadJsonU64Field(json, "version", &ver).ok()) {
    return Status::Corrupt("envelope version missing");
  }
  env.version = static_cast<uint32_t>(ver);
  std::string salt_hex;
  if (!ReadJsonStringField(json, "salt", &salt_hex).ok() ||
      !HexToBytesLocal(salt_hex, env.salt, 16)) {
    return Status::Corrupt("envelope salt invalid");
  }
  std::string wrap_pw;
  std::string wrap_rk;
  if (!ReadJsonStringField(json, "wrap_password", &wrap_pw).ok() ||
      !ReadJsonStringField(json, "wrap_recovery", &wrap_rk).ok()) {
    return Status::Corrupt("envelope wraps missing");
  }
  env.wrap_password = Base64Decode(wrap_pw);
  env.wrap_recovery = Base64Decode(wrap_rk);
  if (env.wrap_password.empty() || env.wrap_recovery.empty()) {
    return Status::Corrupt("envelope wraps invalid");
  }
  *out = std::move(env);
  return Status::Ok();
}

Status SaveEnvelope(const std::string& repo_path, const CryptoEnvelope& env) {
  std::ostringstream out;
  out << "{\n"
      << "  \"version\": " << env.version << ",\n"
      << "  \"salt\": \"" << BytesToHexLocal(env.salt, 16) << "\",\n"
      << "  \"kdf\": \"pbkdf2-sha256-100000\",\n"
      << "  \"wrap_password\": \"" << Base64Encode(env.wrap_password.data(),
                                                   env.wrap_password.size())
      << "\",\n"
      << "  \"wrap_recovery\": \"" << Base64Encode(env.wrap_recovery.data(),
                                                    env.wrap_recovery.size())
      << "\"\n"
      << "}\n";
  const std::string path = EnvelopePath(repo_path);
  std::ofstream file(PathFromUtf8(path), std::ios::trunc);
  if (!file) return Status::IoError("cannot write crypto.envelope.json");
  file << out.str();
  file.flush();
  if (!file) return Status::IoError("envelope write failed");
  file.close();
  return FsyncPath(path);
}

Status CreateEnvelope(const std::string& repo_path, const std::string& password,
                      std::string* recovery_key_out, uint8_t master_key[32]) {
  if (password.empty()) return Status::InvalidArgument("password required");
  if (EnvelopeExists(repo_path)) {
    return Status::Conflict("crypto.envelope.json already exists");
  }
  uint8_t salt[16];
  const Status salt_st = LoadOrCreateRepoSalt(repo_path, salt);
  if (!salt_st.ok()) return salt_st;

  RandomBytes(master_key, kMasterKeySize);
  const std::string recovery_key = GenerateRecoveryKey();

  CryptoEnvelope env{};
  env.version = 1;
  std::memcpy(env.salt, salt, 16);
  const Status wp =
      WrapMasterKey(master_key, password, env.salt, DigestAlgo::kStandard,
                    &env.wrap_password);
  if (!wp.ok()) return wp;
  const Status wr =
      WrapMasterKey(master_key, recovery_key, env.salt, DigestAlgo::kStandard,
                    &env.wrap_recovery);
  if (!wr.ok()) return wr;
  const Status save = SaveEnvelope(repo_path, env);
  if (!save.ok()) return save;
  if (recovery_key_out) *recovery_key_out = recovery_key;
  return Status::Ok();
}

Status UpgradeLegacyToEnvelope(const std::string& repo_path,
                               const std::string& password,
                               std::string* recovery_key_out) {
  if (EnvelopeExists(repo_path)) return Status::Ok();
  uint8_t salt[16];
  const Status salt_st = LoadOrCreateRepoSalt(repo_path, salt);
  if (!salt_st.ok()) return salt_st;
  uint8_t master_key[32];
  const Status dk =
      DeriveContentKey(password, salt, master_key, DigestAlgo::kLegacy);
  if (!dk.ok()) return dk;

  const std::string recovery_key = GenerateRecoveryKey();
  CryptoEnvelope env{};
  env.version = 1;
  std::memcpy(env.salt, salt, 16);
  const Status wp =
      WrapMasterKey(master_key, password, env.salt, DigestAlgo::kLegacy,
                    &env.wrap_password);
  if (!wp.ok()) return wp;
  const Status wr =
      WrapMasterKey(master_key, recovery_key, env.salt, DigestAlgo::kLegacy,
                    &env.wrap_recovery);
  if (!wr.ok()) return wr;
  const Status save = SaveEnvelope(repo_path, env);
  if (!save.ok()) return save;
  if (recovery_key_out) *recovery_key_out = recovery_key;
  return Status::Ok();
}

Status UnwrapMasterKeyWithPassword(const std::string& repo_path,
                                   const std::string& password,
                                   uint8_t master_key[32], DigestAlgo algo) {
  CryptoEnvelope env{};
  const Status load = LoadEnvelope(repo_path, &env);
  if (!load.ok()) return load;
  return UnwrapMasterKeyBlob(env.wrap_password, password, env.salt, algo,
                             master_key);
}

Status UnwrapMasterKeyWithRecoveryKey(const std::string& repo_path,
                                      const std::string& recovery_key,
                                      uint8_t master_key[32], DigestAlgo algo) {
  CryptoEnvelope env{};
  const Status load = LoadEnvelope(repo_path, &env);
  if (!load.ok()) return load;
  return UnwrapMasterKeyBlob(env.wrap_recovery, recovery_key, env.salt, algo,
                             master_key);
}

Status RotateEnvelopePassword(const std::string& repo_path,
                              const std::string& old_password,
                              const std::string& new_password,
                              DigestAlgo algo) {
  if (new_password.empty()) return Status::InvalidArgument("new password empty");
  CryptoEnvelope env{};
  const Status load = LoadEnvelope(repo_path, &env);
  if (!load.ok()) return load;
  uint8_t master_key[32];
  const Status unwrap =
      UnwrapMasterKeyBlob(env.wrap_password, old_password, env.salt, algo,
                          master_key);
  if (!unwrap.ok()) return unwrap;
  env.wrap_password.clear();
  const Status wp =
      WrapMasterKey(master_key, new_password, env.salt, algo, &env.wrap_password);
  if (!wp.ok()) return wp;
  return SaveEnvelope(repo_path, env);
}

}  // namespace crypto
}  // namespace ebbackup
