#include "ebbackup/crypto/aes_gcm.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>

#include "ebbackup/common/digest.h"
#include "ebbackup/common/fsync.h"
#include "ebbackup/crypto/gcm_portable.h"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace ebbackup {
namespace crypto {

namespace {

#ifdef _WIN32
Status Aes256GcmEncryptBCrypt(const uint8_t key[32], const uint8_t* plain,
                              size_t plain_len, std::vector<uint8_t>* out) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) {
    return Status::Internal("BCryptOpenAlgorithmProvider failed");
  }
  (void)BCryptSetProperty(
      alg, BCRYPT_CHAINING_MODE,
      reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
      sizeof(BCRYPT_CHAIN_MODE_GCM), 0);

  BCRYPT_KEY_HANDLE hkey = nullptr;
  if (BCryptGenerateSymmetricKey(alg, &hkey, nullptr, 0,
                                const_cast<PUCHAR>(key), 32, 0) != 0) {
    BCryptCloseAlgorithmProvider(alg, 0);
    return Status::Internal("BCryptGenerateSymmetricKey failed");
  }

  uint8_t nonce[kAesGcmNonceSize];
  std::random_device rd;
  for (size_t i = 0; i < kAesGcmNonceSize; ++i) {
    nonce[i] = static_cast<uint8_t>(rd());
  }
  uint8_t tag[kAesGcmTagSize]{};
  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
  BCRYPT_INIT_AUTH_MODE_INFO(info);
  info.pbNonce = nonce;
  info.cbNonce = kAesGcmNonceSize;
  info.pbTag = tag;
  info.cbTag = kAesGcmTagSize;

  out->assign(kAesGcmNonceSize + plain_len + kAesGcmTagSize, 0);
  std::memcpy(out->data(), nonce, kAesGcmNonceSize);
  ULONG result_len = 0;
  const NTSTATUS st = BCryptEncrypt(
      hkey, const_cast<PUCHAR>(plain), static_cast<ULONG>(plain_len), &info,
      nullptr, 0, out->data() + kAesGcmNonceSize, static_cast<ULONG>(plain_len),
      &result_len, 0);
  BCryptDestroyKey(hkey);
  BCryptCloseAlgorithmProvider(alg, 0);
  if (st != 0) return Status::Internal("BCryptEncrypt failed");
  std::memcpy(out->data() + kAesGcmNonceSize + plain_len, tag, kAesGcmTagSize);
  return Status::Ok();
}

Status Aes256GcmDecryptBCrypt(const uint8_t key[32], const uint8_t* blob,
                              size_t blob_len, std::vector<uint8_t>* out) {
  if (blob_len < kAesGcmNonceSize + kAesGcmTagSize) {
    return Status::Corrupt("encrypted blob too short");
  }
  BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) {
    return Status::Internal("BCryptOpenAlgorithmProvider failed");
  }
  (void)BCryptSetProperty(
      alg, BCRYPT_CHAINING_MODE,
      reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
      sizeof(BCRYPT_CHAIN_MODE_GCM), 0);

  BCRYPT_KEY_HANDLE hkey = nullptr;
  if (BCryptGenerateSymmetricKey(alg, &hkey, nullptr, 0,
                                const_cast<PUCHAR>(key), 32, 0) != 0) {
    BCryptCloseAlgorithmProvider(alg, 0);
    return Status::Internal("BCryptGenerateSymmetricKey failed");
  }

  const size_t cipher_len = blob_len - kAesGcmNonceSize - kAesGcmTagSize;
  uint8_t tag[kAesGcmTagSize];
  std::memcpy(tag, blob + blob_len - kAesGcmTagSize, kAesGcmTagSize);
  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
  BCRYPT_INIT_AUTH_MODE_INFO(info);
  info.pbNonce = const_cast<PUCHAR>(blob);
  info.cbNonce = kAesGcmNonceSize;
  info.pbTag = tag;
  info.cbTag = kAesGcmTagSize;

  out->assign(cipher_len, 0);
  ULONG result_len = 0;
  const NTSTATUS st = BCryptDecrypt(
      hkey, const_cast<PUCHAR>(blob + kAesGcmNonceSize),
      static_cast<ULONG>(cipher_len), &info, nullptr, 0, out->data(),
      static_cast<ULONG>(out->size()), &result_len, 0);
  BCryptDestroyKey(hkey);
  BCryptCloseAlgorithmProvider(alg, 0);
  if (st != 0) return Status::Corrupt("BCryptDecrypt failed");
  out->resize(result_len);
  return Status::Ok();
}
#endif

}  // namespace

Status Aes256GcmEncrypt(const uint8_t key[32], const uint8_t* plain,
                        size_t plain_len, std::vector<uint8_t>* out) {
#ifdef _WIN32
  const Status bcrypt_st = Aes256GcmEncryptBCrypt(key, plain, plain_len, out);
  if (bcrypt_st.ok()) return bcrypt_st;
#endif
  return Aes256GcmEncryptPortable(key, plain, plain_len, out);
}

Status Aes256GcmDecrypt(const uint8_t key[32], const uint8_t* blob,
                        size_t blob_len, std::vector<uint8_t>* out) {
#ifdef _WIN32
  const Status bcrypt_st = Aes256GcmDecryptBCrypt(key, blob, blob_len, out);
  if (bcrypt_st.ok()) return bcrypt_st;
#endif
  return Aes256GcmDecryptPortable(key, blob, blob_len, out);
}

Status LoadOrCreateRepoSalt(const std::string& repo_path, uint8_t salt[16]) {
  const std::string path =
      (std::filesystem::path(repo_path) / "crypto.salt").string();
  if (std::filesystem::exists(path)) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return Status::IoError("cannot open salt");
    in.read(reinterpret_cast<char*>(salt), 16);
    if (!in) return Status::Corrupt("salt read short");
    return Status::Ok();
  }
  std::random_device rd;
  for (size_t i = 0; i < 16; ++i) salt[i] = static_cast<uint8_t>(rd());
  std::ofstream out(path, std::ios::binary);
  if (!out) return Status::IoError("cannot create salt");
  out.write(reinterpret_cast<const char*>(salt), 16);
  out.flush();
  if (!out) return Status::IoError("salt write failed");
  out.close();
  return FsyncPath(path);
}

Status DeriveContentKey(const std::string& password, const uint8_t salt[16],
                        uint8_t cek[32]) {
  if (password.empty()) return Status::InvalidArgument("empty password");
  Pbkdf2Sha256(reinterpret_cast<const uint8_t*>(password.data()),
               password.size(), salt, 16, 100000, cek);
  return Status::Ok();
}

}  // namespace crypto
}  // namespace ebbackup
