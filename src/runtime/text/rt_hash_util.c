//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_hash_util.c
// Purpose: SipHash-2-4 per-process seed initialization. Provides a single
//          shared seed across all translation units via extern linkage.
//
// Key invariants:
//   - The seed is initialized exactly once per process via pthread_once /
//     InitOnceExecuteOnce for thread safety.
//   - The seed is sourced from the OS CSPRNG (/dev/urandom or BCryptGenRandom).
//
// Ownership/Lifetime:
//   - Global state lives for the process lifetime; no cleanup needed.
//
// Links: src/runtime/text/rt_hash_util.h (public API)
//
//===----------------------------------------------------------------------===//

#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif
#else
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#endif

/// Shared SipHash-2-4 key (128 bits).
uint64_t rt_siphash_k0_ = 0;
uint64_t rt_siphash_k1_ = 0;
int rt_siphash_seeded_ = 0;

/// @brief Fill buffer with random bytes from the OS CSPRNG.
static int hash_random_fill(uint8_t *buf, size_t len) {
    if (len == 0)
        return 0;
#ifdef _WIN32
    NTSTATUS status = BCryptGenRandom(NULL, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return NT_SUCCESS(status) ? 0 : -1;
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return -1;
    size_t done = 0;
    while (done < len) {
        ssize_t r = read(fd, buf + done, len - done);
        if (r <= 0) {
            close(fd);
            return -1;
        }
        done += (size_t)r;
    }
    close(fd);
    return 0;
#endif
}

/// @brief Actual seed initialization (called exactly once).
static void hash_seed_init(void) {
    uint8_t buf[16];
    if (hash_random_fill(buf, 16) == 0) {
        memcpy(&rt_siphash_k0_, buf, 8);
        memcpy(&rt_siphash_k1_, buf + 8, 8);
    } else {
        /* Fallback: use address-space entropy if CSPRNG unavailable. */
        rt_siphash_k0_ = (uint64_t)(uintptr_t)&rt_siphash_k0_ ^ 0x736f6d6570736575ULL;
        rt_siphash_k1_ = (uint64_t)(uintptr_t)&rt_siphash_k1_ ^ 0x646f72616e646f6dULL;
    }
#if defined(_MSC_VER) && !defined(__clang__)
    rt_siphash_seeded_ = 1;
#else
    __atomic_store_n(&rt_siphash_seeded_, 1, __ATOMIC_RELEASE);
#endif
}

#ifdef _WIN32
static INIT_ONCE g_hash_seed_once_ = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK hash_seed_once_cb(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *Context) {
    (void)InitOnce;
    (void)Parameter;
    (void)Context;
    hash_seed_init();
    return TRUE;
}

void rt_hash_ensure_seeded_(void) {
    InitOnceExecuteOnce(&g_hash_seed_once_, hash_seed_once_cb, NULL, NULL);
}
#else
static pthread_once_t g_hash_seed_once_ = PTHREAD_ONCE_INIT;

void rt_hash_ensure_seeded_(void) {
    pthread_once(&g_hash_seed_once_, hash_seed_init);
}
#endif
