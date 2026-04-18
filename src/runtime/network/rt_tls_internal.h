//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_tls_internal.h
// Purpose: Internal shared definitions between rt_tls.c and rt_tls_verify.c.
//          Exposes the TLS session struct and certificate verification functions.
// Key invariants:
//   - This header is internal to the network module; not part of the public API.
//   - The rt_tls_session struct layout must match across both translation units.
// Ownership/Lifetime:
//   - Session objects are owned by callers of the public API (rt_tls.h).
// Links: rt_tls.c (core TLS), rt_tls_verify.c (certificate validation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_crypto.h"
#include "rt_tls.h"
#include <stddef.h>
#include <stdint.h>

// Platform socket type (needed for struct layout)
#ifdef _WIN32
#include <winsock2.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Max sizes
#define TLS_MAX_RECORD_SIZE 16384
#define TLS_MAX_CIPHERTEXT (TLS_MAX_RECORD_SIZE + 256)

// Handshake states
typedef enum {
    TLS_STATE_INITIAL,
    TLS_STATE_CLIENT_HELLO_SENT,
    TLS_STATE_SERVER_HELLO_RECEIVED,
    TLS_STATE_WAIT_ENCRYPTED_EXTENSIONS,
    TLS_STATE_WAIT_CERTIFICATE,
    TLS_STATE_WAIT_CERTIFICATE_VERIFY,
    TLS_STATE_WAIT_FINISHED,
    TLS_STATE_CONNECTED,
    TLS_STATE_CLOSED,
    TLS_STATE_ERROR
} tls_state_t;

// Traffic keys
typedef struct {
    uint8_t key[32];
    uint8_t iv[12];
    uint64_t seq_num;
} traffic_keys_t;

// TLS session structure
struct rt_tls_session {
    // Socket (platform-specific type stored as int-compatible)
#ifdef _WIN32
    SOCKET socket_fd;
#else
    int socket_fd;
#endif
    tls_state_t state;
    const char *error;

    // Configuration
    char hostname[256];
    char alpn_protocol[64];
    int verify_cert;
    int timeout_ms;

    // Handshake state
    uint8_t client_private_key[32];
    uint8_t client_public_key[32];
    uint8_t server_public_key[32];
    uint8_t client_random[32];
    uint8_t server_random[32];
    uint16_t cipher_suite;

    // Key schedule
    uint8_t handshake_secret[32];
    uint8_t client_handshake_traffic_secret[32];
    uint8_t server_handshake_traffic_secret[32];
    uint8_t master_secret[32];
    uint8_t client_application_traffic_secret[32];
    uint8_t server_application_traffic_secret[32];

    // Transcript hash
    uint8_t transcript_hash[32];
    rt_sha256_ctx transcript_ctx;
    size_t transcript_len;
    uint8_t client_hello_hash[32];
    int have_client_hello_hash;
    uint8_t *hello_retry_cookie;
    size_t hello_retry_cookie_len;
    int hello_retry_seen;

    // Record layer
    traffic_keys_t write_keys;
    traffic_keys_t read_keys;
    int keys_established;

    // Read buffer
    uint8_t read_buffer[TLS_MAX_CIPHERTEXT];
    size_t read_buffer_len;
    size_t read_buffer_pos;

    // Decrypted application data buffer
    uint8_t app_buffer[TLS_MAX_RECORD_SIZE];
    size_t app_buffer_len;
    size_t app_buffer_pos;

    // Certificate validation state (CS-1/CS-2/CS-3)
    uint8_t server_cert_der[16384];   // End-entity certificate DER bytes
    size_t server_cert_der_len;       // Bytes stored in server_cert_der
    uint8_t *server_cert_list;        // Raw TLS certificate_list entries (leaf + intermediates)
    size_t server_cert_list_len;      // Bytes stored in server_cert_list
    size_t server_cert_count;         // Number of parsed certificate entries
    uint8_t cert_transcript_hash[32]; // Transcript hash saved AFTER Certificate (for CS-3)
};

/// @brief Parse the TLS 1.3 Certificate handshake message and store the
///        first (end-entity) certificate DER bytes in session->server_cert_der.
int tls_parse_certificate_msg(rt_tls_session_t *session, const uint8_t *data, size_t len);

/// @brief Verify that session->hostname matches the certificate DER.
int tls_verify_hostname(rt_tls_session_t *session);

/// @brief Verify certificate chain against the OS trust store.
int tls_verify_chain(rt_tls_session_t *session);

/// @brief Verify the CertificateVerify handshake message signature.
int tls_verify_cert_verify(rt_tls_session_t *session, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
