# Viper Network Runtime — Audit Fixes + New Classes Implementation Plan

**Date:** 2026-03-20
**Scope:** 44 audit findings (27 confirmed, 17 false positive) + AES-128-GCM implementation + 10 new network classes
**Source audit:** `/misc/bugs/runtime_network_20260320.md`

---

# Part 1: Audit Finding Verification & Fix Plans

## Legend

- **CONFIRMED** — Real issue with fix plan below
- **FALSE POSITIVE** — Not a real issue; explanation of why
- Priority: P0-CRITICAL, P1-HIGH, P2-MEDIUM, P3-LOW

---

## rt_network.c (3 confirmed / 10 original)

### NET-001 — FALSE POSITIVE: select() fd_set overflow
**Why false positive:** `wait_socket()` adds exactly one socket to the fd_set. With a single fd, `FD_SET` is safe even for high fd values on most platforms. The risk would only apply if iterating over many fds, which this code does not do.

### NET-002 — CONFIRMED (P3-LOW): rt_tcp_send_str missing INT_MAX clamp
**Root cause:** `rt_tcp_send()` clamps at line 650 but `rt_tcp_send_str()` at line 675 does not.
**Fix (rt_network.c:675):**
```c
// BEFORE:
int sent = send(tcp->sock, text_ptr, (int)len, SEND_FLAGS);

// AFTER:
int to_send = (len > INT_MAX) ? INT_MAX : (int)len;
int sent = send(tcp->sock, text_ptr, to_send, SEND_FLAGS);
```

### NET-003 — FALSE POSITIVE: rt_tcp_recv max_bytes truncation
**Why false positive:** `rt_bytes_new(max_bytes)` allocates the buffer first. Even if `(int)max_bytes` wraps, `recv()` can only write into the allocated buffer. The returned size is correct via the exact-size copy at line 772.

### NET-004 — FALSE POSITIVE: rt_udp_recv_from truncation
**Why false positive:** Same safe pattern as NET-003.

### NET-005 — CONFIRMED (P1-HIGH): ViperDOS missing ifaddrs.h
**Root cause:** `rt_dns_local_addrs()` calls `getifaddrs()`/`freeifaddrs()` in the `#else` branch (line 2050), which covers ViperDOS. But ViperDOS block (lines 70-92) does not include `<ifaddrs.h>`, and ViperDOS likely doesn't provide these functions.
**Fix (rt_network.c:2050):**
```c
// BEFORE the #else block for rt_dns_local_addrs():
#ifdef __viperdos__
    // ViperDOS does not provide getifaddrs(); return empty list
    (void)seq;
#else
    // Unix: use getifaddrs for local addresses
    struct ifaddrs *ifaddr, *ifa;
    ...
#endif
```

### NET-006 — FALSE POSITIVE: ViperDOS missing sys/ioctl.h
**Why false positive:** ViperDOS provides BSD-style socket APIs via libc (documented at line 71). `ioctl` with `FIONREAD` is a standard BSD socket operation.

### NET-007 — FALSE POSITIVE: Timeout int truncation
**Why false positive:** Timeouts are configuration values, not security-sensitive. Socket APIs handle negative timeouts gracefully (disable timeout). Inconsistent but not dangerous.

### NET-008 — FALSE POSITIVE: File docstring
**Why false positive:** Documentation accuracy issue, not a code bug.

### NET-009 — FALSE POSITIVE: get_local_port IPv4 only
**Why false positive:** Entire network layer is explicitly IPv4-only (documented at line 435). Not a bug, a design decision.

### NET-010 — CONFIRMED (P3-LOW): Inconsistent port 0 validation
**Root cause:** TCP server rejects port 0 (`port < 1`), UDP bind accepts it (`port < 0`).
**Fix:** Document that UDP port 0 means ephemeral port. Add comment to `rt_udp_bind_at()`:
```c
// port == 0 requests an OS-assigned ephemeral port (valid for UDP)
if (port < 0 || port > 65535)
```

---

## rt_http_url.c (4 confirmed / 5 original)

### URL-001 — FALSE POSITIVE: Port parsing overflow
**Why false positive:** `result->port` is `int64_t`. Even parsing "99999999999999999" won't overflow int64_t (max ~9.2×10^18). Invalid port values are caught downstream.

### URL-002 — CONFIRMED (P2-MEDIUM): Memory leaks in query param operations
**Root cause:** 4 functions create temporary maps via `rt_url_decode_query()` and temporary `rt_string` objects that are never released.
**Fix for all 4 functions — pattern:**
```c
// In rt_url_set_query_param, rt_url_get_query_param,
// rt_url_has_query_param, rt_url_del_query_param:

// Create temporary string for existing query
rt_string tmp_query = rt_string_from_bytes(
    url->query ? url->query : "", url->query ? strlen(url->query) : 0);
void *map = rt_url_decode_query(tmp_query);
rt_string_unref(tmp_query);  // <-- ADD: release temp string

// ... use map ...

// At end of function, release map:
if (map && rt_obj_release_check0(map))
    rt_obj_free(map);  // <-- ADD: release temp map
```

Additionally in `rt_url_set_query_param()` (line 883):
```c
rt_string new_query = rt_url_encode_query(map);
// ... use new_query ...
rt_string_unref(new_query);  // <-- ADD: release after strdup
```

### URL-003 — CONFIRMED (P2-MEDIUM): key_str leak in rt_url_decode_query
**Root cause:** In the key=value branch (line 1262), `key_str` is created but never unreferenced. `val_str` IS unreferenced at line 1268.
**Fix (rt_http_url.c:1268):**
```c
// BEFORE:
rt_string_unref(val_str);

// AFTER:
rt_string_unref(key_str);
rt_string_unref(val_str);
```
Also add `rt_string_unref(key_str)` in the key-without-value branch after line 1229.

### URL-004 — FALSE POSITIVE: Header comment
**Why false positive:** The comment is about internal `char*` fields, which IS correct. The `rt_string` return values are GC-managed separately.

### URL-005 — CONFIRMED (P2-MEDIUM): parse_url_full return unchecked
**Root cause:** Line 970 calls `parse_url_full(rel_str, &rel)` ignoring the return value. If parsing fails, `rel` has partially-initialized fields.
**Fix (rt_http_url.c:970):**
```c
// BEFORE:
parse_url_full(rel_str, &rel);

// AFTER:
if (parse_url_full(rel_str, &rel) != 0)
{
    // Parse failure — treat as empty relative URL, clone base
    memset(&rel, 0, sizeof(rel));
}
```

---

## rt_network_http.c (3 confirmed / 8 original)

### HTTP-001 — CONFIRMED (P1-HIGH): rt_http_head returns wrong type
**Root cause:** Header doc says "Return Map of response headers" but implementation returns `rt_http_res_t*` (full response object). Test at line 995 treats it as a Map via `rt_map_get(headers, ...)`. runtime.def registers it as `"obj(str)"` which is ambiguous.
**Fix:** Change the implementation to return the headers map:
```c
// rt_network_http.c, rt_http_head():
void *rt_http_head(rt_string url)
{
    // ... (existing setup code) ...
    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    // ... (existing cleanup) ...
    if (!res)
        rt_trap_net("HTTP: request failed", Err_NetworkError);

    // Return the headers map (what the doc and test expect)
    void *headers = res->headers;
    // Prevent finalizer from freeing headers when res is released
    res->headers = NULL;
    if (rt_obj_release_check0(res))
        rt_obj_free(res);
    return headers;
}
```

### HTTP-002 — CONFIRMED (P2-MEDIUM): HTTP 303 redirect not handled
**Root cause:** Line 1057 checks 301/302/307/308 but not 303. RFC 7231 §6.4.4 says 303 should redirect with GET.
**Fix (rt_network_http.c:1057):**
```c
// BEFORE:
if ((status == 301 || status == 302 || status == 307 || status == 308) && redirect_location)

// AFTER:
if ((status == 301 || status == 302 || status == 303 ||
     status == 307 || status == 308) && redirect_location)
{
    // ... existing redirect code ...

    // RFC 7231: 303 must change method to GET and remove body
    if (status == 303)
    {
        free(req->method);
        req->method = strdup("GET");
        free(req->body);
        req->body = NULL;
        req->body_len = 0;
    }

    return do_http_request(req, redirects_remaining - 1);
}
```

### HTTP-003 — FALSE POSITIVE: body_cap overflow
**Why false positive:** `HTTP_MAX_BODY_SIZE` (256MB) prevents body_cap from reaching overflow territory on 64-bit. On 32-bit, 256MB is within size_t range.

### HTTP-004 — FALSE POSITIVE: timeout clamping
**Why false positive:** Negative timeouts from int truncation are handled gracefully by socket APIs.

### HTTP-005 — FALSE POSITIVE: TLS partial send
**Why false positive:** `rt_tls_send()` loops internally (lines 869-877 of rt_tls.c).

### HTTP-006, HTTP-007 — FALSE POSITIVE: Doc/buffer issues
**Why false positive:** Documentation is acceptable; hostname buffer (300) fits RFC max FQDN (253 chars + port).

### HTTP-008 — CONFIRMED (P3-LOW): Boilerplate duplication
**Root cause:** ~450 lines of near-identical code across rt_http_get/post/put/delete/patch/options.
**Fix:** Extract common helper. Low priority — functional but maintainability concern. Defer to file splitting phase.

---

## rt_tls.c (6 confirmed / 7 original)

### TLS-001 — CONFIRMED (P2-MEDIUM): Large stack allocations
**Root cause:** `send_record()` ~33KB, `recv_record()` ~16.6KB on stack. Combined >50KB.
**Fix:** Move to heap allocation for the largest buffers:
```c
// In send_record():
uint8_t *record = malloc(5 + TLS_MAX_CIPHERTEXT);
if (!record) { session->error = "OOM"; return RT_TLS_ERROR; }
// ... use record ...
free(record);
```
Or alternatively, keep stack but document minimum 128KB stack requirement for network threads.

### TLS-002 — CONFIRMED (P3-LOW): O(n²) transcript hashing
**Root cause:** `transcript_update()` re-hashes full buffer each call. Already commented at line 147.
**Fix:** Replace with incremental SHA-256 context in session struct:
```c
// In rt_tls_session struct, add:
sha256_ctx transcript_ctx;  // incremental hash context

// In transcript_update():
sha256_update(&session->transcript_ctx, data, len);
sha256_final_copy(&session->transcript_ctx, session->transcript_hash);
```
Requires exposing `sha256_ctx` from rt_crypto.c. Low priority since typical handshakes have few updates.

### TLS-003/004 — CONFIRMED (P3-LOW): Byte-at-a-time copy
**Root cause:** `rt_viper_tls_send()` (line 1242) and `rt_viper_tls_recv()` (line 1302) copy bytes individually.
**Fix:** Use internal `bytes_impl` accessor or add a bulk copy helper. In rt_tls.c, the `bytes_impl` struct is not available, so either:
- Add `rt_bytes_data()` public accessor to rt_bytes.h
- Or use the existing `bytes_impl` pattern from rt_network.c:
```c
// In rt_viper_tls_send():
typedef struct { int64_t len; uint8_t *data; } bytes_impl;
uint8_t *buffer = ((bytes_impl *)data)->data;
int64_t len = ((bytes_impl *)data)->len;
// Direct memcpy, no loop needed
```

### TLS-005 — CONFIRMED (P1-HIGH): Windows socket `< 0` check
**Root cause:** Line 1007: `if (sock < 0)`. SOCKET is unsigned on Windows; failed socket() returns INVALID_SOCKET (~0), never < 0.
**Fix (rt_tls.c:1007):**
```c
// BEFORE:
if (sock < 0)

// AFTER:
#ifdef _WIN32
if (sock == INVALID_SOCKET)
#else
if (sock < 0)
#endif
```

### TLS-006 — FALSE POSITIVE: rt_viper_tls_close socket
**Why false positive:** Finalizer correctly handles socket closure. The close() function intentionally only closes TLS layer; socket cleanup happens on GC.

### TLS-007 — CONFIRMED (P2-MEDIUM): recv_line no 64KB cap
**Root cause:** `rt_viper_tls_recv_line()` (line 1375) grows buffer without hard limit, unlike `rt_tcp_recv_line()` which caps at 64KB (line 876).
**Fix (rt_tls.c, inside the while(1) loop after `if (c == '\n')`):**
```c
// Add before the capacity growth check:
if (len >= 65536)
{
    free(line);
    return rt_string_from_bytes("", 0);  // Line too long
}
```

---

## rt_tls_verify.c (4 confirmed / 3 original + 3 new)

### TLSV-001 — CONFIRMED (P2-MEDIUM): TODO SHA-384/SHA-512 for RSA-PSS
**Root cause:** Line 1645: `content_hash` always uses SHA-256 (32 bytes). RSA-PSS schemes 0x0805/0x0806 require SHA-384/SHA-512.
**Fix:** Implement `rt_sha384()` and `rt_sha512()` in rt_crypto.c, then dispatch based on scheme. Large task — defer to crypto improvement phase.

### TLSV-002 — FALSE POSITIVE: Windows #error guard
**Why false positive:** Negative guard (`#ifndef _WINDOWS_`) is safe defensive coding.

### TLSV-003 — FALSE POSITIVE: Only first cert stored
**Why false positive:** Correct TLS 1.3 behavior. OS trust stores handle chain building.

### TLSV-004 — CONFIRMED (P2-MEDIUM): macOS CertVerify hash size
**Root cause:** Line 1138 always passes 32 bytes. For SHA-384/SHA-512 schemes, the hash is too short. Same root cause as TLSV-001.
**Fix:** Part of SHA-384/512 implementation — compute correct hash size per scheme.

### TLSV-005 — CONFIRMED (P1-HIGH): Static ca_der not thread-safe
**Root cause:** Line 989: `static uint8_t ca_der[16384]` is shared across all threads. Two concurrent TLS handshakes on Linux will corrupt each other's CA DER decoding.
**Fix (rt_tls_verify.c:989):**
```c
// BEFORE:
static uint8_t ca_der[16384];

// AFTER:
uint8_t *ca_der = (uint8_t *)malloc(16384);
if (!ca_der) { fclose(f); session->error = "TLS: OOM"; return RT_TLS_ERROR_HANDSHAKE; }
// ... use ca_der ...
free(ca_der);  // at end of function, before return
```

### TLSV-006 — CONFIRMED (P2-MEDIUM): 64KB pem_b64 on stack
**Root cause:** Line 984: `char pem_b64[65536]` + other TLS stack buffers = >100KB total.
**Fix:** Move to heap allocation alongside ca_der fix above:
```c
char *pem_b64 = (char *)malloc(65536);
if (!pem_b64) { free(ca_der); fclose(f); ... }
// ... use pem_b64 ...
free(pem_b64);
```

---

## rt_crypto.c (4 confirmed / 1 original + 3 new)

### CRYPTO-001 — CONFIRMED (P0-CRITICAL): Missing AES-128-GCM
**Root cause:** Only ChaCha20-Poly1305 (0x1303) offered in ClientHello. RFC 8446 §9.1
mandates TLS_AES_128_GCM_SHA256 (0x1301). Servers that only offer AES-GCM reject the
handshake, causing HTTPS failures.

**Fix — 3 components (~560 LOC total):**

#### A. AES-128 Block Cipher (rt_crypto.c, ~150 LOC new)

```c
// Precomputed S-box (FIPS 197 §5.1.1)
static const uint8_t aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

// Round constants for key expansion
static const uint8_t aes_rcon[10] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

// Key expansion: 16-byte key → 176 bytes (11 round keys)
static void aes128_key_expand(const uint8_t key[16], uint8_t rk[176])
{
    memcpy(rk, key, 16);
    for (int i = 4; i < 44; i++)
    {
        uint8_t tmp[4];
        memcpy(tmp, rk + (i - 1) * 4, 4);
        if (i % 4 == 0)
        {
            uint8_t t = tmp[0];
            tmp[0] = aes_sbox[tmp[1]] ^ aes_rcon[i / 4 - 1];
            tmp[1] = aes_sbox[tmp[2]];
            tmp[2] = aes_sbox[tmp[3]];
            tmp[3] = aes_sbox[t];
        }
        for (int j = 0; j < 4; j++)
            rk[i * 4 + j] = rk[(i - 4) * 4 + j] ^ tmp[j];
    }
}

// Single-block AES-128 encrypt (SubBytes, ShiftRows, MixColumns, AddRoundKey × 10 rounds)
static void aes128_encrypt_block(const uint8_t rk[176],
                                  const uint8_t in[16], uint8_t out[16])
{
    uint8_t state[16];
    memcpy(state, in, 16);
    // AddRoundKey (round 0)
    for (int i = 0; i < 16; i++) state[i] ^= rk[i];
    // Rounds 1-9: SubBytes, ShiftRows, MixColumns, AddRoundKey
    for (int round = 1; round <= 9; round++) { /* ... standard AES round ... */ }
    // Round 10: SubBytes, ShiftRows, AddRoundKey (no MixColumns)
    // ...
    memcpy(out, state, 16);
}
```

#### B. GCM Mode (rt_crypto.c, ~260 LOC new)

```c
// GF(2^128) multiplication (GHASH core operation)
// Implements schoolbook multiplication in GF(2^128) with reducing polynomial
// x^128 + x^7 + x^2 + x + 1 (0xE1000...0)
static void ghash_mult(const uint8_t H[16], const uint8_t X[16], uint8_t out[16])
{
    uint8_t V[16], Z[16];
    memcpy(V, H, 16);
    memset(Z, 0, 16);
    for (int i = 0; i < 128; i++)
    {
        if ((X[i / 8] >> (7 - (i % 8))) & 1)
            for (int j = 0; j < 16; j++) Z[j] ^= V[j];
        // Right shift V, reduce if needed
        uint8_t carry = V[15] & 1;
        for (int j = 15; j > 0; j--)
            V[j] = (V[j] >> 1) | (V[j-1] << 7);
        V[0] >>= 1;
        if (carry) V[0] ^= 0xE1; // reduction polynomial
    }
    memcpy(out, Z, 16);
}

// GHASH: iterative multiplication over padded AAD + ciphertext + lengths
static void ghash(const uint8_t H[16], const uint8_t *aad, size_t aad_len,
                  const uint8_t *ct, size_t ct_len, uint8_t tag[16])
{
    uint8_t X[16] = {0};
    // Process AAD in 16-byte blocks
    // Process ciphertext in 16-byte blocks
    // Process length block: aad_len_bits || ct_len_bits
    // Each block: X = ghash_mult(H, X ^ block)
    memcpy(tag, X, 16);
}

// AES-128-GCM AEAD encrypt
size_t rt_aes128_gcm_encrypt(const uint8_t key[16], const uint8_t nonce[12],
                              const void *aad, size_t aad_len,
                              const void *plaintext, size_t plaintext_len,
                              uint8_t *ciphertext)
{
    uint8_t rk[176];
    aes128_key_expand(key, rk);
    // H = AES(key, 0^128)
    uint8_t H[16] = {0};
    aes128_encrypt_block(rk, H, H);
    // J0 = nonce || 0x00000001  (initial counter)
    uint8_t J0[16] = {0};
    memcpy(J0, nonce, 12);
    J0[15] = 1;
    // Encrypt plaintext with AES-CTR starting at J0+1
    // Compute GHASH tag over AAD and ciphertext
    // XOR tag with AES(key, J0) for final authentication tag
    // Append 16-byte tag after ciphertext
    rt_secure_zero(rk, sizeof(rk));
    return plaintext_len + 16;
}

// AES-128-GCM AEAD decrypt (verify-then-decrypt)
long rt_aes128_gcm_decrypt(const uint8_t key[16], const uint8_t nonce[12],
                            const void *aad, size_t aad_len,
                            const void *ciphertext, size_t ciphertext_len,
                            uint8_t *plaintext)
{
    if (ciphertext_len < 16) return -1;
    size_t data_len = ciphertext_len - 16;
    // Recompute GHASH tag and compare (constant-time)
    // If match: decrypt with AES-CTR
    // If mismatch: return -1
    return (long)data_len;
}
```

#### C. TLS Integration (rt_tls.c, ~50 LOC changes)

**rt_tls.c line 93 — add cipher suite constant:**
```c
#define TLS_AES_128_GCM_SHA256 0x1301
#define TLS_CHACHA20_POLY1305_SHA256 0x1303
```

**rt_tls.c send_client_hello (line 449-453) — offer both suites:**
```c
write_u16(msg + pos, 4);  // 2 suites
pos += 2;
write_u16(msg + pos, TLS_AES_128_GCM_SHA256);
pos += 2;
write_u16(msg + pos, TLS_CHACHA20_POLY1305_SHA256);
pos += 2;
```

**rt_tls.c process_server_hello (line 563) — accept either:**
```c
if (session->cipher_suite != TLS_AES_128_GCM_SHA256 &&
    session->cipher_suite != TLS_CHACHA20_POLY1305_SHA256)
```

**rt_tls.c derive_handshake_keys/derive_application_keys — key size dispatch:**
```c
int key_len = (session->cipher_suite == TLS_AES_128_GCM_SHA256) ? 16 : 32;
rt_hkdf_expand_label(secret, "key", NULL, 0, keys.key, key_len);
rt_hkdf_expand_label(secret, "iv", NULL, 0, keys.iv, 12); // always 12
```

**rt_tls.c send_record/recv_record — AEAD dispatch:**
```c
if (session->cipher_suite == TLS_AES_128_GCM_SHA256)
    ciphertext_len = rt_aes128_gcm_encrypt(key, nonce, aad, 5, plaintext, len+1, out);
else
    ciphertext_len = rt_chacha20_poly1305_encrypt(key, nonce, aad, 5, plaintext, len+1, out);
```

#### D. rt_crypto.h — declarations

```c
size_t rt_aes128_gcm_encrypt(const uint8_t key[16], const uint8_t nonce[12],
                              const void *aad, size_t aad_len,
                              const void *plaintext, size_t plaintext_len,
                              uint8_t *ciphertext);
long rt_aes128_gcm_decrypt(const uint8_t key[16], const uint8_t nonce[12],
                            const void *aad, size_t aad_len,
                            const void *ciphertext, size_t ciphertext_len,
                            uint8_t *plaintext);
```

#### E. Tests (5 tests)

1. **AES-128 NIST FIPS 197 known-answer:** Encrypt with key `2b7e151628aed2a6abf7158809cf4f3c`,
   plaintext `3243f6a8885a308d313198a2e0370734`, expect ciphertext `3925841d02dc09fbdc118597196a0b32`
2. **GCM encrypt/decrypt round-trip:** Arbitrary data, verify decrypt recovers plaintext
3. **GCM NIST SP 800-38D Test Case 3:** Key=`feffe9928665731c6d6a8f9467308308`,
   IV=`cafebabefacedbaddecaf888`, known ciphertext+tag
4. **GCM auth failure:** Tamper one byte of ciphertext, verify decrypt returns -1
5. **TLS AES-GCM handshake:** Connect to public HTTPS server, verify cipher suite negotiation

### CRYPTO-002 — CONFIRMED (P3-LOW): ChaCha20 counter overflow
**Root cause:** Line 444: `state[12]++` wraps at 2^32. For TLS (max 16KB records), this is ~256GB — unreachable. But direct API users could hit it.
**Fix (rt_crypto.c:444):**
```c
// BEFORE:
state[12]++;

// AFTER:
if (++state[12] == 0)
    return;  // Counter overflow — stop encrypting
```

### CRYPTO-003 — CONFIRMED (P3-LOW): HKDF expand label silent failure
**Root cause:** Line 320: returns without writing to output buffer on overflow. Caller can't detect failure.
**Fix:** Change return type to `int` (0=success, -1=error). Update all callers to check. Or add `memset(out, 0, out_len)` before the early return so callers get a deterministic (zeroed) failure value.

### CRYPTO-004 — CONFIRMED (P3-LOW): Poly1305 key not zeroed
**Root cause:** `poly_key[64]` at lines 705/754 not zeroed after encrypt/decrypt. Key material persists on stack.
**Fix (rt_crypto.c, at end of encrypt and decrypt functions):**
```c
rt_secure_zero(poly_key, sizeof(poly_key));
```

---

## rt_ecdsa_p256.c (1 confirmed / 1 original + 1 new)

### ECDSA-001 — FALSE POSITIVE: Stack allocations
**Why false positive:** Acceptable for verification-only code.

### ECDSA-003 — CONFIRMED (P3-LOW): sn_mul carry overflow
**Root cause:** Line 568: `u256_add(sum2, hr2_lo, sum)` doesn't check carry. If carry occurs, result wraps silently.
**Fix (rt_ecdsa_p256.c:568):**
```c
// BEFORE:
u256_add(sum2, hr2_lo, sum);
sn_reduce_once(sum2, sum2);

// AFTER:
uint64_t carry = u256_add(sum2, hr2_lo, sum);
sn_reduce_once(sum2, sum2);
if (carry) {
    // Add carry equivalent: subtract n (since 2^256 mod n = R_MOD_N)
    u256_add(sum2, sum2, R_MOD_N);
    sn_reduce_once(sum2, sum2);
}
```

---

## rt_websocket.c (2 confirmed / 5 original)

### WS-002 — FALSE POSITIVE: strstr "101"
**Why false positive:** Single-socket handshake response; "101" in a header value is vanishingly unlikely and benign.

### WS-003 — FALSE POSITIVE: 64-bit length truncation
**Why false positive:** 64MB cap at line 734 makes upper 4 bytes irrelevant.

### WS-004 — FALSE POSITIVE: No fragmentation
**Why false positive:** By design — TCP/TLS handle segmentation. Sending unfragmented is valid per RFC 6455.

### WS-005 — CONFIRMED (P3-LOW): Wrong file path in header
**Root cause:** Line 8: `src/runtime/rt_websocket.c` should be `src/runtime/network/rt_websocket.c`.
**Fix:** Update the comment.

### WS-006 — CONFIRMED (P1-HIGH): select() ignores TLS buffer
**Root cause:** `rt_ws_recv_for()` line 1149 calls `ws_wait_socket()` on the raw fd. If TLS has buffered decrypted data in `session->app_buffer`, select() reports "not ready" even though data IS available.
**Fix (rt_websocket.c:1148):**
```c
// BEFORE:
if (timeout_ms > 0)
{
    int ready = ws_wait_socket(ws->socket_fd, (int)timeout_ms, 0);
    if (ready <= 0)
        return NULL;
}

// AFTER:
if (timeout_ms > 0)
{
    // Check if TLS already has buffered data before going to select()
    if (ws->tls && rt_tls_has_buffered_data(ws->tls))
    {
        // Data already available — skip select()
    }
    else
    {
        int ready = ws_wait_socket(ws->socket_fd, (int)timeout_ms, 0);
        if (ready <= 0)
            return NULL;
    }
}
```
Requires adding `rt_tls_has_buffered_data()` to rt_tls.h:
```c
int rt_tls_has_buffered_data(rt_tls_session_t *session)
{
    return session && session->app_buffer_pos < session->app_buffer_len;
}
```
Same fix needed for `rt_ws_recv_bytes_for()`.

---

## rt_restclient.c — ALL FALSE POSITIVE

### REST-001, REST-002, REST-003 — FALSE POSITIVE
**Why:** rt_string values are reference-counted. The map stores references correctly. JSON format results are managed by GC. Auth string temporaries are properly freed.

---

## rt_ratelimit.c, rt_retry.c — No issues

### RL-001, RETRY-001 — No action needed. Clean code.

---

# Part 2: 10 New Network Classes

## Priority Order & Dependencies

```
Phase 1 (Foundation):  HttpRouter → HttpServer
Phase 2 (Server):      WsServer, SSE (depend on HttpServer)
Phase 3 (Client):      ConnectionPool → HttpClient
Phase 4 (Utilities):   Multipart, NetUtils, AsyncSocket, SmtpClient
```

---

## Class 1: HttpRouter

**Purpose:** URL pattern matching with parameter extraction and middleware chain.
**File:** `rt_http_router.c` (~400 LOC), `rt_http_router.h`

### API Design

```
RT_CLASS_BEGIN("Viper.Network.HttpRouter", HttpRouter, "obj", HttpRouterNew)
    RT_METHOD("New", "obj()", HttpRouterNew)
    RT_METHOD("Get", "obj(str,obj)", HttpRouterGet)       // pattern, handler_id
    RT_METHOD("Post", "obj(str,obj)", HttpRouterPost)
    RT_METHOD("Put", "obj(str,obj)", HttpRouterPut)
    RT_METHOD("Delete", "obj(str,obj)", HttpRouterDelete)
    RT_METHOD("Use", "obj(obj)", HttpRouterUse)            // middleware
    RT_METHOD("Match", "obj(str,str)", HttpRouterMatch)    // method, path → route match
RT_CLASS_END()
```

### Internal Structure
```c
typedef struct {
    char *method;           // "GET", "POST", etc.
    char *pattern;          // "/users/:id/posts"
    void *handler;          // Opaque handler reference
    char **param_names;     // ["id"]
    int param_count;
} rt_route_t;

typedef struct {
    rt_route_t *routes;
    int route_count;
    int route_cap;
    void **middleware;       // Ordered middleware chain
    int middleware_count;
} rt_http_router_t;
```

### Tests (8)
1. Exact path match: `/api/users` matches GET /api/users
2. Parameter extraction: `/users/:id` extracts `id=42` from `/users/42`
3. Wildcard: `/static/*path` matches `/static/css/main.css`
4. Method filtering: GET handler doesn't match POST
5. 404 no match: returns NULL for unregistered path
6. Multiple routes: first match wins
7. Middleware ordering: middleware runs in registration order
8. Route count/cleanup: finalizer frees all routes

---

## Class 2: HttpServer

**Purpose:** Threaded HTTP/1.1 server with routing, request/response objects.
**File:** `rt_http_server.c` (~800 LOC), `rt_http_server.h`
**Depends on:** HttpRouter, TcpServer, Threads.Pool

### API Design

```
RT_CLASS_BEGIN("Viper.Network.HttpServer", HttpServer, "obj", HttpServerNew)
    RT_METHOD("New", "obj(i64)", HttpServerNew)            // port
    RT_METHOD("Route", "void(str,str,obj)", HttpServerRoute) // method, pattern, handler
    RT_METHOD("Use", "void(obj)", HttpServerUse)           // middleware
    RT_METHOD("Start", "void()", HttpServerStart)
    RT_METHOD("Stop", "void()", HttpServerStop)
    RT_PROP("Port", "i64", HttpServerPort, none)
    RT_PROP("IsRunning", "i1", HttpServerIsRunning, none)
RT_CLASS_END()

RT_CLASS_BEGIN("Viper.Network.ServerReq", ServerReq, "obj", none)
    RT_PROP("Method", "str", ServerReqMethod, none)
    RT_PROP("Path", "str", ServerReqPath, none)
    RT_PROP("Body", "str", ServerReqBody, none)
    RT_PROP("BodyBytes", "obj", ServerReqBodyBytes, none)
    RT_METHOD("Header", "str(str)", ServerReqHeader)
    RT_METHOD("Param", "str(str)", ServerReqParam)         // URL params from router
    RT_METHOD("Query", "str(str)", ServerReqQuery)
RT_CLASS_END()

RT_CLASS_BEGIN("Viper.Network.ServerRes", ServerRes, "obj", none)
    RT_METHOD("Status", "obj(i64)", ServerResStatus)       // fluent
    RT_METHOD("Header", "obj(str,str)", ServerResHeader)   // fluent
    RT_METHOD("Send", "void(str)", ServerResSend)
    RT_METHOD("SendBytes", "void(obj)", ServerResSendBytes)
    RT_METHOD("Json", "void(obj)", ServerResJson)           // auto content-type
RT_CLASS_END()
```

### Architecture
- Accept loop on dedicated thread using `rt_tcp_server_accept()`
- Each request dispatched to thread pool worker
- Worker parses HTTP/1.1 request, creates ServerReq, routes via HttpRouter
- Handler populates ServerRes, which is serialized back over TCP
- Connection: close after each request (no keep-alive in v1)

### Tests (12)
1. Server start/stop lifecycle
2. GET request returns 200
3. POST request with body
4. Route parameters extracted
5. Query string parsed
6. Custom headers sent/received
7. 404 for unmatched routes
8. JSON response auto content-type
9. Concurrent requests (thread pool)
10. Server port property
11. Middleware modifies request
12. Large response body

---

## Class 3: WsServer

**Purpose:** WebSocket server accepting upgrade requests.
**File:** `rt_ws_server.c` (~500 LOC), `rt_ws_server.h`
**Depends on:** HttpServer, Threads.Pool

### API Design
```
RT_CLASS_BEGIN("Viper.Network.WsServer", WsServer, "obj", WsServerNew)
    RT_METHOD("New", "obj(obj,str)", WsServerNew)    // httpServer, path
    RT_METHOD("Broadcast", "void(str)", WsServerBroadcast)
    RT_METHOD("BroadcastBytes", "void(obj)", WsServerBroadcastBytes)
    RT_PROP("ClientCount", "i64", WsServerClientCount, none)
    RT_METHOD("Stop", "void()", WsServerStop)
RT_CLASS_END()
```

### Tests (6)
1. Accept WebSocket upgrade
2. Echo message back
3. Broadcast to all clients
4. Client disconnect handling
5. Close handshake
6. Client count tracking

---

## Class 4: ConnectionPool

**Purpose:** Reusable TCP/TLS connection pooling.
**File:** `rt_connpool.c` (~350 LOC), `rt_connpool.h`

### API Design
```
RT_CLASS_BEGIN("Viper.Network.ConnectionPool", ConnPool, "obj", ConnPoolNew)
    RT_METHOD("New", "obj(i64)", ConnPoolNew)          // max connections
    RT_METHOD("Acquire", "obj(str,i64)", ConnPoolAcquire)  // host, port → Tcp
    RT_METHOD("Release", "void(obj)", ConnPoolRelease)
    RT_METHOD("Clear", "void()", ConnPoolClear)
    RT_PROP("Size", "i64", ConnPoolSize, none)
    RT_PROP("Available", "i64", ConnPoolAvailable, none)
RT_CLASS_END()
```

### Internal Structure
```c
typedef struct {
    void *tcp;          // TCP connection
    char *host;
    int port;
    time_t last_used;
} pooled_conn_t;

typedef struct {
    pooled_conn_t *conns;
    int count, cap, max_size;
    int max_idle_sec;
    pthread_mutex_t lock;   // Thread-safe
} rt_connpool_t;
```

### Tests (8)
1. Acquire creates new connection
2. Release returns connection to pool
3. Re-acquire reuses pooled connection
4. Max size enforced
5. Idle connections expired
6. Thread-safe concurrent acquire/release
7. Clear removes all connections
8. Finalizer closes all connections

---

## Class 5: HttpClient (session-based)

**Purpose:** HTTP client with cookies, auto-redirect, connection reuse.
**File:** `rt_http_client.c` (~500 LOC), `rt_http_client.h`
**Depends on:** ConnectionPool

### API Design
```
RT_CLASS_BEGIN("Viper.Network.HttpClient", HttpClient, "obj", HttpClientNew)
    RT_METHOD("New", "obj()", HttpClientNew)
    RT_METHOD("Get", "obj(str)", HttpClientGet)         // → HttpRes
    RT_METHOD("Post", "obj(str,str)", HttpClientPost)
    RT_METHOD("Put", "obj(str,str)", HttpClientPut)
    RT_METHOD("Delete", "obj(str)", HttpClientDelete)
    RT_METHOD("SetHeader", "void(str,str)", HttpClientSetHeader)
    RT_METHOD("SetCookie", "void(str,str,str)", HttpClientSetCookie) // domain, name, value
    RT_METHOD("GetCookies", "obj(str)", HttpClientGetCookies)        // domain → Map
    RT_METHOD("SetMaxRedirects", "void(i64)", HttpClientSetMaxRedirects)
    RT_METHOD("SetTimeout", "void(i64)", HttpClientSetTimeout)
    RT_PROP("FollowRedirects", "i1", HttpClientFollowRedirects, HttpClientSetFollowRedirects)
RT_CLASS_END()
```

### Tests (10)
1. Basic GET/POST
2. Cookies persist across requests to same domain
3. Set-Cookie header parsed
4. Auto-redirect follows 301/302/303/307/308
5. Max redirects enforced
6. Custom headers applied
7. Timeout works
8. Connection reuse from pool
9. Different domains get different cookies
10. FollowRedirects=false skips redirect

---

## Class 6: SSE (Server-Sent Events)

**Purpose:** EventSource client + server-side push.
**File:** `rt_sse.c` (~300 LOC), `rt_sse.h`

### API Design
```
// Client
RT_CLASS_BEGIN("Viper.Network.SseClient", SseClient, "obj", SseClientConnect)
    RT_METHOD("Connect", "obj(str)", SseClientConnect)     // url
    RT_METHOD("Recv", "str()", SseClientRecv)              // blocking, returns event data
    RT_METHOD("RecvFor", "str(i64)", SseClientRecvFor)     // with timeout
    RT_PROP("IsOpen", "i1", SseClientIsOpen, none)
    RT_METHOD("Close", "void()", SseClientClose)
RT_CLASS_END()

// Server-side writer (created from HttpServer handler)
RT_CLASS_BEGIN("Viper.Network.SseWriter", SseWriter, "obj", none)
    RT_METHOD("Send", "void(str)", SseWriterSend)          // data only
    RT_METHOD("SendEvent", "void(str,str)", SseWriterSendEvent) // event, data
    RT_METHOD("SendId", "void(str,str,str)", SseWriterSendId) // id, event, data
    RT_PROP("IsOpen", "i1", SseWriterIsOpen, none)
    RT_METHOD("Close", "void()", SseWriterClose)
RT_CLASS_END()
```

### Tests (6)
1. Client connects and receives events
2. Named events parsed correctly
3. Multi-line data reassembled
4. Server-side push sends correct SSE format
5. Client timeout returns empty
6. Connection close detected

---

## Class 7: Multipart

**Purpose:** Multipart form-data builder and parser for file uploads.
**File:** `rt_multipart.c` (~350 LOC), `rt_multipart.h`

### API Design
```
RT_CLASS_BEGIN("Viper.Network.Multipart", Multipart, "obj", MultipartNew)
    RT_METHOD("New", "obj()", MultipartNew)
    RT_METHOD("AddField", "obj(str,str)", MultipartAddField)     // name, value (fluent)
    RT_METHOD("AddFile", "obj(str,str,obj)", MultipartAddFile)   // name, filename, bytes (fluent)
    RT_METHOD("ContentType", "str()", MultipartContentType)      // with boundary
    RT_METHOD("Build", "obj()", MultipartBuild)                  // → Bytes
    RT_METHOD("Parse", "obj(str,obj)", MultipartParse)           // content-type, body → Multipart
    RT_METHOD("GetField", "str(str)", MultipartGetField)         // name → value
    RT_METHOD("GetFile", "obj(str)", MultipartGetFile)           // name → Bytes
    RT_PROP("FieldCount", "i64", MultipartFieldCount, none)
RT_CLASS_END()
```

### Tests (8)
1. Build with text fields
2. Build with file attachment
3. ContentType includes boundary
4. Parse round-trip (build then parse)
5. Multiple fields same name
6. Binary file content preserved
7. Empty multipart
8. Large file handling

---

## Class 8: NetUtils

**Purpose:** Static network utility functions.
**File:** `rt_netutils.c` (~300 LOC), `rt_netutils.h`

### API Design
```
RT_CLASS_BEGIN("Viper.Network.NetUtils", NetUtils, "none", none)
    RT_METHOD("IsPortOpen", "i1(str,i64,i64)", NetUtilsIsPortOpen)  // host, port, timeout_ms
    RT_METHOD("GetFreePort", "i64()", NetUtilsGetFreePort)
    RT_METHOD("ParseCIDR", "obj(str)", NetUtilsParseCIDR)           // "10.0.0.0/8" → obj
    RT_METHOD("MatchCIDR", "i1(str,str)", NetUtilsMatchCIDR)        // ip, cidr → bool
    RT_METHOD("IsPrivateIP", "i1(str)", NetUtilsIsPrivateIP)
    RT_METHOD("LocalIPv4", "str()", NetUtilsLocalIPv4)              // primary local IP
    RT_METHOD("MacAddress", "str()", NetUtilsMacAddress)
RT_CLASS_END()
```

### Tests (8)
1. IsPortOpen on listening port
2. IsPortOpen on closed port
3. GetFreePort returns bindable port
4. ParseCIDR valid/invalid
5. MatchCIDR 10.0.0.0/8
6. IsPrivateIP for 192.168.x.x, 10.x.x.x, 172.16-31.x.x
7. LocalIPv4 returns non-empty
8. MacAddress format xx:xx:xx:xx:xx:xx

---

## Class 9: SmtpClient

**Purpose:** Simple email sending with STARTTLS support.
**File:** `rt_smtp.c` (~400 LOC), `rt_smtp.h`
**Depends on:** TCP, TLS

### API Design
```
RT_CLASS_BEGIN("Viper.Network.SmtpClient", SmtpClient, "obj", SmtpClientNew)
    RT_METHOD("New", "obj(str,i64)", SmtpClientNew)        // host, port
    RT_METHOD("SetAuth", "void(str,str)", SmtpClientSetAuth) // username, password
    RT_METHOD("SetTls", "void(i1)", SmtpClientSetTls)
    RT_METHOD("Send", "i1(str,str,str,str)", SmtpClientSend)  // from, to, subject, body
    RT_METHOD("SendHtml", "i1(str,str,str,str)", SmtpClientSendHtml)
    RT_PROP("LastError", "str", SmtpClientLastError, none)
    RT_METHOD("Close", "void()", SmtpClientClose)
RT_CLASS_END()
```

### Tests (6)
1. EHLO handshake with mock server
2. AUTH LOGIN encoding
3. MIME message format (plain text)
4. MIME message format (HTML)
5. STARTTLS upgrade
6. Error handling (connection refused, auth failed)

---

## Class 10: AsyncSocket

**Purpose:** Non-blocking socket wrapper integrated with Threads.Future.
**File:** `rt_async_socket.c` (~400 LOC), `rt_async_socket.h`
**Depends on:** Threads.Pool, Threads.Future, TCP/TLS

### API Design
```
RT_CLASS_BEGIN("Viper.Network.AsyncSocket", AsyncSocket, "none", none)
    RT_METHOD("ConnectAsync", "obj(str,i64)", AsyncConnectAsync)     // → Future[Tcp]
    RT_METHOD("SendAsync", "obj(obj,obj)", AsyncSendAsync)           // tcp, data → Future[i64]
    RT_METHOD("RecvAsync", "obj(obj,i64)", AsyncRecvAsync)           // tcp, maxBytes → Future[Bytes]
    RT_METHOD("HttpGetAsync", "obj(str)", AsyncHttpGetAsync)         // → Future[str]
    RT_METHOD("HttpPostAsync", "obj(str,str)", AsyncHttpPostAsync)   // → Future[str]
RT_CLASS_END()
```

### Implementation Pattern
Each Async method creates a Future, submits a blocking operation to the thread pool, and resolves the Future when done:
```c
void *rt_async_connect(rt_string host, int64_t port)
{
    void *future = rt_future_new();
    // Capture args in heap-allocated closure
    async_connect_args *args = malloc(sizeof(...));
    args->host = strdup(...);
    args->port = port;
    args->future = future;
    rt_threadpool_submit(get_default_pool(), async_connect_worker, args);
    return future;
}
```

### Tests (6)
1. ConnectAsync resolves to Tcp
2. SendAsync resolves to bytes sent
3. RecvAsync resolves to data
4. HttpGetAsync returns response body
5. Concurrent async operations
6. Timeout/cancellation

---

# Summary

## Fix Implementation Priority

| Priority | Items | Description |
|----------|-------|-------------|
| P1-HIGH | 4 | HTTP-001 (Head return type), NET-005 (ViperDOS), TLS-005 (Windows socket), TLSV-005 (thread-safe ca_der), WS-006 (TLS buffer) |
| P2-MEDIUM | 8 | URL-002/003/005, HTTP-002, TLS-001/007, TLSV-001/004/006 |
| P3-LOW | 9 | NET-002/010, TLS-002/003/004, CRYPTO-002/003/004, ECDSA-003, WS-005, HTTP-008 |
| P0-CRITICAL | 1 | CRYPTO-001 (AES-128-GCM — RFC 8446 mandatory, ~560 LOC) |

## New Classes Implementation Order

| Phase | Classes | Est. LOC | Depends On |
|-------|---------|----------|------------|
| 1 | HttpRouter, HttpServer | 1,200 | TcpServer, Pool |
| 2 | WsServer, SSE | 800 | HttpServer |
| 3 | ConnectionPool, HttpClient | 850 | TCP/TLS |
| 4 | Multipart, NetUtils, SmtpClient, AsyncSocket | 1,450 | Various |
| **Total** | **10 classes** | **~4,300** | |
