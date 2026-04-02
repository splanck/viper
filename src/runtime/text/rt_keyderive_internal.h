#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RT_PBKDF2_MIN_ITERATIONS 1000U
#define RT_PBKDF2_MAX_KEY_LEN 1024U

void rt_keyderive_pbkdf2_sha256_raw(const uint8_t *password,
                                    size_t password_len,
                                    const uint8_t *salt,
                                    size_t salt_len,
                                    uint32_t iterations,
                                    uint8_t *out,
                                    size_t out_len);

#ifdef __cplusplus
}
#endif
