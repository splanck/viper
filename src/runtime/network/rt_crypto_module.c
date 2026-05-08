//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_crypto_module.h"

#include "rt_crypto.h"
#include "rt_string.h"
#include "rt_trap.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
extern void arc4random_buf(void *buf, size_t nbytes);
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <bcrypt.h>
#include <windows.h>
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif
#elif defined(__linux__)
#include <sys/random.h>
#include <fcntl.h>
#include <unistd.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

typedef struct rt_hmac_drbg_state {
    uint8_t k[32];
    uint8_t v[32];
    uint64_t reseed_counter;
    int ready;
} rt_hmac_drbg_state_t;

static rt_crypto_module_mode_t g_mode = RT_CRYPTO_MODULE_MODE_COMPAT;
static rt_crypto_module_state_t g_state = RT_CRYPTO_MODULE_STATE_UNINITIALIZED;
static const char *g_status = "uninitialized";
static rt_hmac_drbg_state_t g_drbg;

static void module_secure_zero(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len-- > 0)
        *p++ = 0;
}

static int module_os_entropy(uint8_t *buf, size_t len) {
    if (len == 0)
        return 0;
#ifdef _WIN32
    size_t off = 0;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > ULONG_MAX)
            chunk = ULONG_MAX;
        NTSTATUS status =
            BCryptGenRandom(NULL, buf + off, (ULONG)chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (!NT_SUCCESS(status))
            return -1;
        off += chunk;
    }
    return 0;
#else
#if defined(__APPLE__)
    arc4random_buf(buf, len);
    return 0;
#elif defined(__linux__)
    size_t got = 0;
    while (got < len) {
        ssize_t n = getrandom(buf + got, len - got, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == ENOSYS)
                break;
            return -1;
        }
        if (n == 0)
            return -1;
        got += (size_t)n;
    }
    if (got == len)
        return 0;
#endif
#ifdef O_CLOEXEC
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
#else
    int fd = open("/dev/urandom", O_RDONLY);
#endif
    if (fd < 0)
        return -1;
    size_t got = 0;
    while (got < len) {
        ssize_t n = read(fd, buf + got, len - got);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            return -1;
        }
        if (n == 0) {
            close(fd);
            return -1;
        }
        got += (size_t)n;
    }
    close(fd);
    return 0;
#endif
}

static void drbg_update(rt_hmac_drbg_state_t *st, const uint8_t *seed, size_t seed_len) {
    uint8_t material[32 + 1 + 64];
    if (seed_len > 64)
        rt_trap("Crypto.Module: DRBG seed material too large");

    memcpy(material, st->v, 32);
    material[32] = 0x00;
    if (seed_len > 0)
        memcpy(material + 33, seed, seed_len);
    rt_hmac_sha256(st->k, sizeof(st->k), material, 33 + seed_len, st->k);
    rt_hmac_sha256(st->k, sizeof(st->k), st->v, sizeof(st->v), st->v);

    if (seed_len > 0) {
        memcpy(material, st->v, 32);
        material[32] = 0x01;
        memcpy(material + 33, seed, seed_len);
        rt_hmac_sha256(st->k, sizeof(st->k), material, 33 + seed_len, st->k);
        rt_hmac_sha256(st->k, sizeof(st->k), st->v, sizeof(st->v), st->v);
    }

    module_secure_zero(material, sizeof(material));
}

static void drbg_instantiate(rt_hmac_drbg_state_t *st, const uint8_t *seed, size_t seed_len) {
    memset(st->k, 0, sizeof(st->k));
    memset(st->v, 1, sizeof(st->v));
    st->reseed_counter = 1;
    st->ready = 1;
    drbg_update(st, seed, seed_len);
}

static void drbg_generate(rt_hmac_drbg_state_t *st, uint8_t *out, size_t out_len) {
    if (!st->ready)
        rt_trap("Crypto.Module: DRBG is not instantiated");
    size_t pos = 0;
    while (pos < out_len) {
        rt_hmac_sha256(st->k, sizeof(st->k), st->v, sizeof(st->v), st->v);
        size_t copy = out_len - pos;
        if (copy > sizeof(st->v))
            copy = sizeof(st->v);
        memcpy(out + pos, st->v, copy);
        pos += copy;
    }
    drbg_update(st, NULL, 0);
    st->reseed_counter++;
}

static int self_test_sha2(void) {
    static const uint8_t sha256_exp[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
        0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
        0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    static const uint8_t sha384_exp[48] = {
        0xcb, 0x00, 0x75, 0x3f, 0x45, 0xa3, 0x5e, 0x8b, 0xb5, 0xa0, 0x3d, 0x69,
        0x9a, 0xc6, 0x50, 0x07, 0x27, 0x2c, 0x32, 0xab, 0x0e, 0xde, 0xd1, 0x63,
        0x1a, 0x8b, 0x60, 0x5a, 0x43, 0xff, 0x5b, 0xed, 0x80, 0x86, 0x07, 0x2b,
        0xa1, 0xe7, 0xcc, 0x23, 0x58, 0xba, 0xec, 0xa1, 0x34, 0xc8, 0x25, 0xa7};
    static const uint8_t sha512_exp[64] = {
        0xdd, 0xaf, 0x35, 0xa1, 0x93, 0x61, 0x7a, 0xba, 0xcc, 0x41, 0x73,
        0x49, 0xae, 0x20, 0x41, 0x31, 0x12, 0xe6, 0xfa, 0x4e, 0x89, 0xa9,
        0x7e, 0xa2, 0x0a, 0x9e, 0xee, 0xe6, 0x4b, 0x55, 0xd3, 0x9a, 0x21,
        0x92, 0x99, 0x2a, 0x27, 0x4f, 0xc1, 0xa8, 0x36, 0xba, 0x3c, 0x23,
        0xa3, 0xfe, 0xeb, 0xbd, 0x45, 0x4d, 0x44, 0x23, 0x64, 0x3c, 0xe8,
        0x0e, 0x2a, 0x9a, 0xc9, 0x4f, 0xa5, 0x4c, 0xa4, 0x9f};
    uint8_t out[64];
    rt_sha256("abc", 3, out);
    if (memcmp(out, sha256_exp, 32) != 0)
        return 0;
    rt_sha384("abc", 3, out);
    if (memcmp(out, sha384_exp, 48) != 0)
        return 0;
    rt_sha512("abc", 3, out);
    return memcmp(out, sha512_exp, 64) == 0;
}

static int self_test_hmac_hkdf(void) {
    uint8_t key[20];
    uint8_t mac[48];
    memset(key, 0x0b, sizeof(key));
    static const uint8_t hmac_exp[32] = {
        0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53, 0x5c, 0xa8, 0xaf,
        0xce, 0xaf, 0x0b, 0xf1, 0x2b, 0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83,
        0x3d, 0xa7, 0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7};
    rt_hmac_sha256(key, sizeof(key), "Hi There", 8, mac);
    if (memcmp(mac, hmac_exp, 32) != 0)
        return 0;

    static const uint8_t prk[32] = {
        0x07, 0x77, 0x09, 0x36, 0x2c, 0x2e, 0x32, 0xdf, 0x0d, 0xdc, 0x3f,
        0x0d, 0xc4, 0x7b, 0xba, 0x63, 0x90, 0xb6, 0xc7, 0x3b, 0xb5, 0x0f,
        0x9c, 0x31, 0x22, 0xec, 0x84, 0x4a, 0xd7, 0xc2, 0xb3, 0xe5};
    static const uint8_t info[10] = {
        0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9};
    static const uint8_t okm_exp[42] = {
        0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a, 0x90, 0x43, 0x4f,
        0x64, 0xd0, 0x36, 0x2f, 0x2a, 0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a,
        0x5a, 0x4c, 0x5d, 0xb0, 0x2d, 0x56, 0xec, 0xc4, 0xc5, 0xbf, 0x34,
        0x00, 0x72, 0x08, 0xd5, 0xb8, 0x87, 0x18, 0x58, 0x65};
    uint8_t okm[42];
    if (rt_hkdf_expand(prk, info, sizeof(info), okm, sizeof(okm)) != 0)
        return 0;
    return memcmp(okm, okm_exp, sizeof(okm)) == 0;
}

static int self_test_aes_gcm(void) {
    uint8_t nonce[12] = {0};
    uint8_t pt[16] = {0};
    uint8_t ct[32];
    uint8_t dec[16];
    uint8_t key128[16] = {0};
    uint8_t key256[32] = {0};
    static const uint8_t aes128_exp[32] = {
        0x03, 0x88, 0xda, 0xce, 0x60, 0xb6, 0xa3, 0x92, 0xf3, 0x28, 0xc2,
        0xb9, 0x71, 0xb2, 0xfe, 0x78, 0xab, 0x6e, 0x47, 0xd4, 0x2c, 0xec,
        0x13, 0xbd, 0xf5, 0x3a, 0x67, 0xb2, 0x12, 0x57, 0xbd, 0xdf};
    static const uint8_t aes256_exp[32] = {
        0xce, 0xa7, 0x40, 0x3d, 0x4d, 0x60, 0x6b, 0x6e, 0x07, 0x4e, 0xc5,
        0xd3, 0xba, 0xf3, 0x9d, 0x18, 0xd0, 0xd1, 0xc8, 0xa7, 0x99, 0x99,
        0x6b, 0xf0, 0x26, 0x5b, 0x98, 0xb5, 0xd4, 0x8a, 0xb9, 0x19};
    if (rt_aes128_gcm_encrypt(key128, nonce, NULL, 0, pt, sizeof(pt), ct) != sizeof(ct))
        return 0;
    if (memcmp(ct, aes128_exp, sizeof(ct)) != 0)
        return 0;
    if (rt_aes128_gcm_decrypt(key128, nonce, NULL, 0, ct, sizeof(ct), dec) != 16)
        return 0;
    if (memcmp(dec, pt, sizeof(pt)) != 0)
        return 0;
    if (rt_aes256_gcm_encrypt(key256, nonce, NULL, 0, pt, sizeof(pt), ct) != sizeof(ct))
        return 0;
    if (memcmp(ct, aes256_exp, sizeof(ct)) != 0)
        return 0;
    return rt_aes256_gcm_decrypt(key256, nonce, NULL, 0, ct, sizeof(ct), dec) == 16 &&
           memcmp(dec, pt, sizeof(pt)) == 0;
}

static int self_test_drbg(void) {
    uint8_t seed[48];
    uint8_t out1[64];
    uint8_t out2[64];
    for (size_t i = 0; i < sizeof(seed); i++)
        seed[i] = (uint8_t)i;
    rt_hmac_drbg_state_t a;
    rt_hmac_drbg_state_t b;
    drbg_instantiate(&a, seed, sizeof(seed));
    drbg_instantiate(&b, seed, sizeof(seed));
    drbg_generate(&a, out1, sizeof(out1));
    drbg_generate(&b, out2, sizeof(out2));
    module_secure_zero(&a, sizeof(a));
    module_secure_zero(&b, sizeof(b));
    return memcmp(out1, out2, sizeof(out1)) == 0;
}

int rt_crypto_module_self_test(void) {
    if (!self_test_sha2()) {
        g_status = "self-test failed: SHA-2";
        return 0;
    }
    if (!self_test_hmac_hkdf()) {
        g_status = "self-test failed: HMAC/HKDF";
        return 0;
    }
    if (!self_test_aes_gcm()) {
        g_status = "self-test failed: AES-GCM";
        return 0;
    }
    if (!self_test_drbg()) {
        g_status = "self-test failed: DRBG";
        return 0;
    }
    return 1;
}

int rt_crypto_module_init(void) {
    if (g_state == RT_CRYPTO_MODULE_STATE_READY)
        return 1;
    if (g_state == RT_CRYPTO_MODULE_STATE_ERROR)
        return 0;

    g_state = RT_CRYPTO_MODULE_STATE_SELF_TESTING;
    g_status = "self-testing";
    if (!rt_crypto_module_self_test()) {
        g_state = RT_CRYPTO_MODULE_STATE_ERROR;
        return 0;
    }

    uint8_t seed[48];
    if (module_os_entropy(seed, sizeof(seed)) != 0) {
        g_state = RT_CRYPTO_MODULE_STATE_ERROR;
        g_status = "entropy unavailable";
        return 0;
    }
    drbg_instantiate(&g_drbg, seed, sizeof(seed));
    module_secure_zero(seed, sizeof(seed));
    g_state = RT_CRYPTO_MODULE_STATE_READY;
    g_status = "ready";
    return 1;
}

int rt_crypto_module_set_mode(rt_crypto_module_mode_t mode) {
    if (mode == RT_CRYPTO_MODULE_MODE_APPROVED) {
        if (!rt_crypto_module_init())
            return 0;
    }
    g_mode = mode;
    return 1;
}

rt_crypto_module_mode_t rt_crypto_module_get_mode(void) {
    return g_mode;
}

rt_crypto_module_state_t rt_crypto_module_get_state(void) {
    return g_state;
}

int rt_crypto_module_is_approved_mode(void) {
    return g_mode == RT_CRYPTO_MODULE_MODE_APPROVED;
}

int rt_crypto_module_service_allowed(rt_crypto_module_service_t service) {
    if (!rt_crypto_module_is_approved_mode())
        return 1;
    switch (service) {
    case RT_CRYPTO_SERVICE_AES_GCM:
    case RT_CRYPTO_SERVICE_SHA2:
    case RT_CRYPTO_SERVICE_HMAC_SHA2:
    case RT_CRYPTO_SERVICE_HKDF_SHA2:
    case RT_CRYPTO_SERVICE_PBKDF2_SHA256:
    case RT_CRYPTO_SERVICE_DRBG:
    case RT_CRYPTO_SERVICE_ECDSA_P256:
    case RT_CRYPTO_SERVICE_RSA_PSS:
        return 1;
    default:
        return 0;
    }
}

const char *rt_crypto_module_status_cstr(void) {
    if (g_state == RT_CRYPTO_MODULE_STATE_UNINITIALIZED)
        return "uninitialized";
    return g_status ? g_status : "unknown";
}

void rt_crypto_module_random_bytes(uint8_t *buf, size_t len) {
    if (!buf && len > 0)
        rt_trap("Crypto.Module: random output buffer is null");
    if (!rt_crypto_module_init())
        rt_trap(rt_crypto_module_status_cstr());
    drbg_generate(&g_drbg, buf, len);
}

int8_t rt_crypto_module_enable_approved_mode(void) {
    return rt_crypto_module_set_mode(RT_CRYPTO_MODULE_MODE_APPROVED) ? 1 : 0;
}

void rt_crypto_module_disable_approved_mode(void) {
    (void)rt_crypto_module_set_mode(RT_CRYPTO_MODULE_MODE_COMPAT);
}

int8_t rt_crypto_module_is_approved_mode_viper(void) {
    return rt_crypto_module_is_approved_mode() ? 1 : 0;
}

rt_string rt_crypto_module_status_text(void) {
    return rt_const_cstr(rt_crypto_module_status_cstr());
}
