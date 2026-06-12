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
#include "rt_rsa.h"
#include "rt_socket_platform.h"
#include "rt_tls.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct rt_tls_server_ctx;

// Max sizes
#define TLS_MAX_RECORD_SIZE 16384
#define TLS_MAX_CIPHERTEXT (TLS_MAX_RECORD_SIZE + 256)

/// @brief TLS 1.3 client-side handshake state machine positions.
/// @details RFC 8446 §A.1 client state diagram. The session advances
///          through each value monotonically; ERROR and CLOSED are
///          sinks that no transition can leave.
typedef enum {
    TLS_STATE_INITIAL,                   ///< No bytes on wire yet.
    TLS_STATE_CLIENT_HELLO_SENT,         ///< ClientHello flushed; waiting for response.
    TLS_STATE_SERVER_HELLO_RECEIVED,     ///< Cipher suite + key_share negotiated.
    TLS_STATE_WAIT_ENCRYPTED_EXTENSIONS, ///< Server-handshake keys derived; awaiting EE.
    TLS_STATE_WAIT_CERTIFICATE,          ///< Awaiting peer Certificate message.
    TLS_STATE_WAIT_CERTIFICATE_VERIFY,   ///< Certificate seen; awaiting CertificateVerify.
    TLS_STATE_WAIT_FINISHED,             ///< CertificateVerify validated; awaiting Finished.
    TLS_STATE_CONNECTED,                 ///< Application traffic keys live, ready for I/O.
    TLS_STATE_CLOSED,                    ///< Peer or local close_notify processed.
    TLS_STATE_ERROR                      ///< Unrecoverable handshake / protocol failure.
} tls_state_t;

/// @brief One direction's record-layer traffic keys + monotonic sequence number.
/// @details The AEAD nonce per record is @c iv XOR @c seq_num (big-endian
///          padded). Sequence counters never wrap — sender bails out
///          before key reuse becomes possible.
typedef struct {
    uint8_t key[32];  ///< Application traffic key (AES-128/256-GCM or ChaCha20-Poly1305).
    uint8_t iv[12];   ///< Per-direction static IV xored with @c seq_num to form the nonce.
    uint64_t seq_num; ///< Records sent or received in this direction since key install.
} traffic_keys_t;

// TLS session structure
struct rt_tls_session {
    // Socket handle owned by the TLS session.
    socket_t socket_fd;
    tls_state_t state;
    const char *error;
    int is_server;

    // Configuration
    char hostname[256];
    char alpn_protocols[128];
    char negotiated_alpn[64];
    char ca_file[512];
    int verify_cert;
    int timeout_ms;
    uint8_t legacy_session_id[32];
    size_t legacy_session_id_len;
    const struct rt_tls_server_ctx *server_ctx;

    // Handshake state
    uint8_t client_private_key[32];
    uint8_t client_public_key[32];
    uint8_t server_public_key[32];
    uint8_t client_random[32];
    uint8_t server_random[32];
    uint16_t cipher_suite;
    uint16_t server_sig_scheme;

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

// Certificate / DER parsing helpers (split across rt_tls.c + rt_tls_certs.c).
int tls_der_read_tlv(
    const uint8_t *buf, size_t buf_len, uint8_t *tag, size_t *val_len, size_t *hdr_len);
int tls_oid_matches(const uint8_t *buf, size_t buf_len, const uint8_t *oid, size_t oid_len);
char *tls_read_text_file(const char *path, size_t *len_out);
int tls_extract_cert_key_type(const uint8_t *cert_der, size_t cert_len);

// PEM/key parsers shared between rt_tls.c (server config) and rt_tls_certs.c.
size_t tls_pem_base64_decode(const char *pem_b64, size_t b64_len, uint8_t *out_der, size_t max_der);
int tls_find_pem_block(const char *pem, const char *begin_marker, const char *end_marker,
                       const char **body_out, size_t *body_len_out, const char **next_out);
int tls_parse_sec1_ec_private_key(const uint8_t *der, size_t der_len, uint8_t out_priv[32]);
int tls_parse_pkcs8_ec_private_key(const uint8_t *der, size_t der_len, uint8_t out_priv[32]);
int tls_extract_cert_ec_pubkey(
    const uint8_t *cert_der, size_t cert_len, uint8_t x_out[32], uint8_t y_out[32]);
int tls_extract_cert_rsa_pubkey(const uint8_t *cert_der, size_t cert_len, rt_rsa_key_t *out);

// Server key/signature type (shared: cert detection in rt_tls_certs.c sets it,
// the handshake in rt_tls.c selects the signature algorithm from it).
enum {
    TLS_SERVER_KEY_NONE = 0,
    TLS_SERVER_KEY_ECDSA_P256 = 1,
    TLS_SERVER_KEY_RSA_PSS_SHA256 = 2,
};
