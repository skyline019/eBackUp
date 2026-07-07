#include "ebbackup/common/digest_sha_ni.h"

#include "ebbackup/common/digest_standard.h"

#include <atomic>
#include <cstring>

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#endif

#if (defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))) || \
    ((defined(__SHA__) || defined(__SHA256__)) && \
     (defined(__x86_64__) || defined(__i386__)))
#include <immintrin.h>
#include <tmmintrin.h>
#define EBBACKUP_SHA_NI_COMPILED 1
#endif

namespace ebbackup {

namespace {

std::atomic<int> g_sha_ni_cached{-1};

bool ProbeShaNiCpu() {
#if defined(_MSC_VER)
  int info[4]{};
  __cpuid(info, 0);
  if (info[0] < 7) return false;
  __cpuidex(info, 7, 0);
  return (info[1] & (1 << 29)) != 0;
#elif defined(__GNUC__) || defined(__clang__)
  unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
  if (__get_cpuid_max(0, nullptr) < 7) return false;
  __cpuid_count(7, 0, eax, ebx, ecx, edx);
  return (ebx & (1u << 29)) != 0;
#else
  return false;
#endif
}

#if defined(EBBACKUP_SHA_NI_COMPILED)

alignas(64) static const uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define ADD_EPI32(dest, src) dest = _mm_add_epi32(dest, src)
#define SHA256_MSG1(dest, src) dest = _mm_sha256msg1_epu32(dest, src)
#define SHA256_MSG2(dest, src) dest = _mm_sha256msg2_epu32(dest, src)

#define LOAD_SHUFFLE(m, k)                                           \
  m = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + (k)*16)); \
  m = _mm_shuffle_epi8(m, mask)

#define NNN(m0, m1, m2, m3)

#define SM1(m1, m2, m3, m0) SHA256_MSG1(m0, m1)
#define SM2(m2, m3, m0, m1)                                          \
  ADD_EPI32(m0, _mm_alignr_epi8(m3, m2, 4));                         \
  SHA256_MSG2(m0, m3)

#define RND2(t0, t1) t0 = _mm_sha256rnds2_epu32(t0, t1, msg);

#define R4(k, m0, m1, m2, m3, OP0, OP1)                              \
  do {                                                                 \
    msg = _mm_add_epi32(                                               \
        m0, _mm_loadu_si128(reinterpret_cast<const __m128i*>(          \
                        &kSha256K[(k)*4])));                           \
    RND2(state0, state1);                                              \
    msg = _mm_shuffle_epi32(msg, 0x0E);                                \
    OP0(m0, m1, m2, m3);                                               \
    RND2(state1, state0);                                              \
    OP1(m0, m1, m2, m3);                                               \
  } while (0)

#define R16(k, OP0, OP1, OP2, OP3, OP4, OP5, OP6, OP7)                 \
  do {                                                                 \
    R4((k)*4 + 0, m0, m1, m2, m3, OP0, OP1);                           \
    R4((k)*4 + 1, m1, m2, m3, m0, OP2, OP3);                           \
    R4((k)*4 + 2, m2, m3, m0, m1, OP4, OP5);                           \
    R4((k)*4 + 3, m3, m0, m1, m2, OP6, OP7);                           \
  } while (0)

#define PREPARE_STATE                                                    \
  tmp = _mm_shuffle_epi32(state0, 0x1B);                               \
  state0 = _mm_shuffle_epi32(state1, 0x1B);                              \
  state1 = state0;                                                       \
  state0 = _mm_unpacklo_epi64(state0, tmp);                              \
  state1 = _mm_unpackhi_epi64(state1, tmp)

void Sha256UpdateBlocksHw(uint32_t state[8], const uint8_t* data,
                          size_t num_blocks) {
  const __m128i mask =
      _mm_set_epi32(0x0c0d0e0f, 0x08090a0b, 0x04050607, 0x00010203);
  __m128i tmp, state0, state1;

  if (num_blocks == 0) return;

  state0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&state[0]));
  state1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&state[4]));
  PREPARE_STATE;

  do {
    __m128i state0_save = state0;
    __m128i state1_save = state1;
    __m128i m0, m1, m2, m3;
    __m128i msg;

    LOAD_SHUFFLE(m0, 0);
    LOAD_SHUFFLE(m1, 1);
    LOAD_SHUFFLE(m2, 2);
    LOAD_SHUFFLE(m3, 3);

    R16(0, NNN, NNN, SM1, NNN, SM1, SM2, SM1, SM2);
    R16(1, SM1, SM2, SM1, SM2, SM1, SM2, SM1, SM2);
    R16(2, SM1, SM2, SM1, SM2, SM1, SM2, SM1, SM2);
    R16(3, SM1, SM2, NNN, SM2, NNN, NNN, NNN, NNN);

    ADD_EPI32(state0, state0_save);
    ADD_EPI32(state1, state1_save);

    data += 64;
  } while (--num_blocks);

  PREPARE_STATE;
  _mm_storeu_si128(reinterpret_cast<__m128i*>(&state[0]), state0);
  _mm_storeu_si128(reinterpret_cast<__m128i*>(&state[4]), state1);
}

#undef ADD_EPI32
#undef SHA256_MSG1
#undef SHA256_MSG2
#undef LOAD_SHUFFLE
#undef SM1
#undef SM2
#undef RND2
#undef R4
#undef R16
#undef PREPARE_STATE

void Sha256NiCore(const uint8_t* data, size_t len, uint8_t hash_out[32]) {
  uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  const size_t blocks = len / 64;
  if (blocks > 0) {
    Sha256UpdateBlocksHw(state, data, blocks);
  }
  uint8_t block[64]{};
  const size_t rem = len % 64;
  if (rem > 0) std::memcpy(block, data + blocks * 64, rem);
  block[rem] = 0x80;
  if (rem >= 56) {
    Sha256UpdateBlocksHw(state, block, 1);
    std::memset(block, 0, 64);
  }
  const uint64_t bitlen = static_cast<uint64_t>(len) * 8;
  for (int i = 0; i < 8; ++i) {
    block[63 - i] = static_cast<uint8_t>((bitlen >> (i * 8)) & 0xFF);
  }
  Sha256UpdateBlocksHw(state, block, 1);
  for (int i = 0; i < 8; ++i) {
    hash_out[i * 4] = static_cast<uint8_t>((state[i] >> 24) & 0xFF);
    hash_out[i * 4 + 1] = static_cast<uint8_t>((state[i] >> 16) & 0xFF);
    hash_out[i * 4 + 2] = static_cast<uint8_t>((state[i] >> 8) & 0xFF);
    hash_out[i * 4 + 3] = static_cast<uint8_t>(state[i] & 0xFF);
  }
}

bool SelfTestShaNi() {
  const char* msg = "abc";
  uint8_t ni[32];
  uint8_t ref[32];
  Sha256NiCore(reinterpret_cast<const uint8_t*>(msg), 3, ni);
  Sha256Standard(reinterpret_cast<const uint8_t*>(msg), 3, ref);
  return std::memcmp(ni, ref, 32) == 0;
}

#endif  // EBBACKUP_SHA_NI_COMPILED

}  // namespace

bool DigestShaNiAvailable() {
  int cached = g_sha_ni_cached.load(std::memory_order_acquire);
  if (cached >= 0) return cached != 0;
#if defined(EBBACKUP_SHA_NI_COMPILED)
  bool enabled = ProbeShaNiCpu() && SelfTestShaNi();
#else
  bool enabled = false;
#endif
  g_sha_ni_cached.store(enabled ? 1 : 0, std::memory_order_release);
  return enabled;
}

void Sha256ShaNi(const uint8_t* data, size_t len, uint8_t hash_out[32]) {
#if defined(EBBACKUP_SHA_NI_COMPILED)
  if (DigestShaNiAvailable()) {
    Sha256NiCore(data, len, hash_out);
    return;
  }
#endif
  Sha256Standard(data, len, hash_out);
}

}  // namespace ebbackup
