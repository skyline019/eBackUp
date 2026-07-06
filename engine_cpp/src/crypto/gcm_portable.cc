#include "ebbackup/crypto/gcm_portable.h"

#include <algorithm>
#include <cstring>
#include <random>

#include "ebbackup/crypto/aes_block.h"
#include "ebbackup/crypto/aes_gcm.h"

namespace ebbackup {
namespace crypto {

namespace {

void Gf128Mul(const uint8_t x[16], const uint8_t y[16], uint8_t out[16]) {
  uint8_t v[16];
  uint8_t z[16]{};
  std::memcpy(v, y, 16);
  for (int i = 0; i < 128; ++i) {
    if ((x[i / 8] >> (7 - (i % 8))) & 1) {
      for (int j = 0; j < 16; ++j) z[j] ^= v[j];
    }
    const uint8_t lsb = static_cast<uint8_t>(v[15] & 1);
    for (int j = 15; j > 0; --j) {
      v[j] = static_cast<uint8_t>((v[j] >> 1) | ((v[j - 1] & 1) << 7));
    }
    v[0] >>= 1;
    if (lsb) v[0] ^= 0xe1;
  }
  std::memcpy(out, z, 16);
}

void GhashUpdate(uint8_t y[16], const uint8_t h[16], const uint8_t* data,
                 size_t len) {
  size_t offset = 0;
  while (offset < len) {
    uint8_t block[16]{};
    const size_t chunk = std::min(len - offset, static_cast<size_t>(16));
    std::memcpy(block, data + offset, chunk);
    for (int i = 0; i < 16; ++i) y[i] ^= block[i];
    uint8_t tmp[16];
    Gf128Mul(y, h, tmp);
    std::memcpy(y, tmp, 16);
    offset += chunk;
  }
}

void Inc32(uint8_t counter[16]) {
  for (int i = 15; i >= 12; --i) {
    ++counter[i];
    if (counter[i] != 0) break;
  }
}

void Gctr(const Aes256Key& key, uint8_t counter[16], const uint8_t* in,
          size_t len, uint8_t* out) {
  uint8_t block[16];
  size_t offset = 0;
  while (offset < len) {
    Aes256EncryptBlock(key, counter, block);
    const size_t chunk = std::min(len - offset, static_cast<size_t>(16));
    for (size_t i = 0; i < chunk; ++i) {
      out[offset + i] = static_cast<uint8_t>(in[offset + i] ^ block[i]);
    }
    offset += chunk;
    Inc32(counter);
  }
}

void BuildLengthBlock(size_t a_len, size_t c_len, uint8_t out[16]) {
  const uint64_t a_bits = static_cast<uint64_t>(a_len) * 8;
  const uint64_t c_bits = static_cast<uint64_t>(c_len) * 8;
  for (int i = 0; i < 8; ++i) {
    out[7 - i] = static_cast<uint8_t>((a_bits >> (i * 8)) & 0xFF);
    out[15 - i] = static_cast<uint8_t>((c_bits >> (i * 8)) & 0xFF);
  }
}

void ComputeTag(const Aes256Key& aes, const uint8_t j0[16], const uint8_t h[16],
                const uint8_t* aad, size_t aad_len, const uint8_t* cipher,
                size_t cipher_len, uint8_t tag_out[16]) {
  uint8_t ghash[16]{};
  GhashUpdate(ghash, h, aad, aad_len);
  GhashUpdate(ghash, h, cipher, cipher_len);
  uint8_t len_block[16];
  BuildLengthBlock(aad_len, cipher_len, len_block);
  GhashUpdate(ghash, h, len_block, 16);
  uint8_t tag_mask[16];
  Aes256EncryptBlock(aes, j0, tag_mask);
  for (int i = 0; i < 16; ++i) {
    tag_out[i] = static_cast<uint8_t>(ghash[i] ^ tag_mask[i]);
  }
}

}  // namespace

Status Aes256GcmEncryptPortable(const uint8_t key[32], const uint8_t* plain,
                                size_t plain_len, std::vector<uint8_t>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  uint8_t nonce[kAesGcmNonceSize];
  std::random_device rd;
  for (size_t i = 0; i < kAesGcmNonceSize; ++i) {
    nonce[i] = static_cast<uint8_t>(rd());
  }
  return Aes256GcmEncryptPortableEx(key, nonce, nullptr, 0, plain, plain_len,
                                    out);
}

Status Aes256GcmEncryptPortableEx(const uint8_t key[32],
                                  const uint8_t nonce[12], const uint8_t* aad,
                                  size_t aad_len, const uint8_t* plain,
                                  size_t plain_len, std::vector<uint8_t>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  Aes256Key aes{};
  Aes256KeyExpand(key, &aes);

  uint8_t j0[16]{};
  std::memcpy(j0, nonce, kAesGcmNonceSize);
  j0[15] = 1;

  uint8_t h[16]{};
  Aes256EncryptBlock(aes, h, h);

  out->assign(kAesGcmNonceSize + plain_len + kAesGcmTagSize, 0);
  std::memcpy(out->data(), nonce, kAesGcmNonceSize);

  uint8_t counter[16];
  std::memcpy(counter, j0, 16);
  Inc32(counter);
  Gctr(aes, counter, plain, plain_len, out->data() + kAesGcmNonceSize);

  uint8_t tag[kAesGcmTagSize];
  ComputeTag(aes, j0, h, aad, aad_len, out->data() + kAesGcmNonceSize, plain_len,
             tag);
  std::memcpy(out->data() + kAesGcmNonceSize + plain_len, tag, kAesGcmTagSize);
  return Status::Ok();
}

Status Aes256GcmDecryptPortable(const uint8_t key[32], const uint8_t* blob,
                                size_t blob_len, std::vector<uint8_t>* out) {
  return Aes256GcmDecryptPortableEx(key, blob, blob_len, nullptr, 0, out);
}

Status Aes256GcmDecryptPortableEx(const uint8_t key[32], const uint8_t* blob,
                                  size_t blob_len, const uint8_t* aad,
                                  size_t aad_len, std::vector<uint8_t>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  if (blob_len < kAesGcmNonceSize + kAesGcmTagSize) {
    return Status::Corrupt("encrypted blob too short");
  }
  const size_t cipher_len = blob_len - kAesGcmNonceSize - kAesGcmTagSize;
  const uint8_t* nonce = blob;
  const uint8_t* cipher = blob + kAesGcmNonceSize;
  const uint8_t* tag = blob + kAesGcmNonceSize + cipher_len;

  Aes256Key aes{};
  Aes256KeyExpand(key, &aes);

  uint8_t j0[16]{};
  std::memcpy(j0, nonce, kAesGcmNonceSize);
  j0[15] = 1;

  uint8_t h[16]{};
  Aes256EncryptBlock(aes, h, h);

  uint8_t expected_tag[kAesGcmTagSize];
  ComputeTag(aes, j0, h, aad, aad_len, cipher, cipher_len, expected_tag);
  uint8_t diff = 0;
  for (size_t i = 0; i < kAesGcmTagSize; ++i) {
    diff |= static_cast<uint8_t>(expected_tag[i] ^ tag[i]);
  }
  if (diff != 0) return Status::Corrupt("gcm tag mismatch");

  out->assign(cipher_len, 0);
  uint8_t counter[16];
  std::memcpy(counter, j0, 16);
  Inc32(counter);
  Gctr(aes, counter, cipher, cipher_len, out->data());
  return Status::Ok();
}

}  // namespace crypto
}  // namespace ebbackup
