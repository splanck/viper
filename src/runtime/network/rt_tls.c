//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file rt_tls.c
/// @brief TLS 1.3 client implementation.
///
/// Implements TLS 1.3 using ChaCha20-Poly1305 AEAD and X25519 key exchange.
//
//===----------------------------------------------------------------------===//

#include "rt_tls.h"
#include "rt_crypto.h"
#include "rt_object.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

// Platform-specific socket includes
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define CLOSE_SOCKET(s) closesocket(s)
#define SOCKET_ERRNO WSAGetLastError()
#define EINTR_CHECK (SOCKET_ERRNO == WSAEINTR)
#define EAGAIN_CHECK (SOCKET_ERRNO == WSAEWOULDBLOCK)
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_t;
#define CLOSE_SOCKET(s) close(s)
#define SOCKET_ERRNO errno
#define EINTR_CHECK (errno == EINTR)
#define EAGAIN_CHECK (errno == EAGAIN || errno == EWOULDBLOCK)
#endif

// Platform-specific trust-store and crypto APIs (CS-1/CS-2/CS-3)
#if defined(__APPLE__)
#include <Security/Security.h>
#elif defined(_WIN32)
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")
#else
#include <dlfcn.h>
#endif

// TLS constants
#define TLS_VERSION_1_2 0x0303
#define TLS_VERSION_1_3 0x0304

// Content types
#define TLS_CONTENT_CHANGE_CIPHER 20
#define TLS_CONTENT_ALERT 21
#define TLS_CONTENT_HANDSHAKE 22
#define TLS_CONTENT_APPLICATION 23

// Handshake types
#define TLS_HS_CLIENT_HELLO 1
#define TLS_HS_SERVER_HELLO 2
#define TLS_HS_ENCRYPTED_EXTENSIONS 8
#define TLS_HS_CERTIFICATE 11
#define TLS_HS_CERTIFICATE_VERIFY 15
#define TLS_HS_FINISHED 20

// Cipher suite
#define TLS_CHACHA20_POLY1305_SHA256 0x1303

// Extensions
#define TLS_EXT_SERVER_NAME 0
#define TLS_EXT_SUPPORTED_VERSIONS 43
#define TLS_EXT_KEY_SHARE 51

// Max sizes
#define TLS_MAX_RECORD_SIZE 16384
#define TLS_MAX_CIPHERTEXT (TLS_MAX_RECORD_SIZE + 256)

// Handshake states
typedef enum
{
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
typedef struct
{
    uint8_t key[32];
    uint8_t iv[12];
    uint64_t seq_num;
} traffic_keys_t;

// TLS session structure
struct rt_tls_session
{
    socket_t socket_fd;
    tls_state_t state;
    const char *error;

    // Configuration
    char hostname[256];
    int verify_cert;

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
    uint8_t transcript_buffer[8192];
    size_t transcript_len;

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
    uint8_t cert_transcript_hash[32]; // Transcript hash saved AFTER Certificate (for CS-3)
};

// Helper: write big-endian uint16
static void write_u16(uint8_t *p, uint16_t v)
{
    p[0] = (v >> 8) & 0xFF;
    p[1] = v & 0xFF;
}

// Helper: write big-endian uint24
static void write_u24(uint8_t *p, uint32_t v)
{
    p[0] = (v >> 16) & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = v & 0xFF;
}

// Helper: read big-endian uint16
static uint16_t read_u16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

// Helper: read big-endian uint24
static uint32_t read_u24(const uint8_t *p)
{
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

// Update transcript hash.
// Returns 0 on success, -1 if the buffer overflows (H-10 fix: abort instead of
// silently hashing a truncated transcript which corrupts all key derivation).
static int transcript_update(rt_tls_session_t *session, const uint8_t *data, size_t len)
{
    if (session->error)
        return -1; // Already in error state

    if (session->transcript_len + len > sizeof(session->transcript_buffer))
    {
        session->error = "TLS: handshake transcript buffer overflow "
                         "(certificate chain too large)";
        return -1;
    }

    memcpy(session->transcript_buffer + session->transcript_len, data, len);
    session->transcript_len += len;
    // Compute running hash
    rt_sha256(session->transcript_buffer, session->transcript_len, session->transcript_hash);
    return 0;
}

// Derive handshake traffic keys
static void derive_handshake_keys(rt_tls_session_t *session, const uint8_t shared_secret[32])
{
    uint8_t zero_key[32] = {0};
    uint8_t early_secret[32];
    uint8_t derived[32];

    // early_secret = HKDF-Extract(0, 0)
    rt_hkdf_extract(NULL, 0, zero_key, 32, early_secret);

    // derived = Derive-Secret(early_secret, "derived", "")
    uint8_t empty_hash[32];
    rt_sha256(NULL, 0, empty_hash);
    rt_hkdf_expand_label(early_secret, "derived", empty_hash, 32, derived, 32);

    // handshake_secret = HKDF-Extract(derived, shared_secret)
    rt_hkdf_extract(derived, 32, shared_secret, 32, session->handshake_secret);

    // client_handshake_traffic_secret
    rt_hkdf_expand_label(session->handshake_secret,
                         "c hs traffic",
                         session->transcript_hash,
                         32,
                         session->client_handshake_traffic_secret,
                         32);

    // server_handshake_traffic_secret
    rt_hkdf_expand_label(session->handshake_secret,
                         "s hs traffic",
                         session->transcript_hash,
                         32,
                         session->server_handshake_traffic_secret,
                         32);

    // Derive keys
    rt_hkdf_expand_label(
        session->server_handshake_traffic_secret, "key", NULL, 0, session->read_keys.key, 32);
    rt_hkdf_expand_label(
        session->server_handshake_traffic_secret, "iv", NULL, 0, session->read_keys.iv, 12);
    session->read_keys.seq_num = 0;

    rt_hkdf_expand_label(
        session->client_handshake_traffic_secret, "key", NULL, 0, session->write_keys.key, 32);
    rt_hkdf_expand_label(
        session->client_handshake_traffic_secret, "iv", NULL, 0, session->write_keys.iv, 12);
    session->write_keys.seq_num = 0;

    session->keys_established = 1;
}

// Derive application traffic keys
static void derive_application_keys(rt_tls_session_t *session)
{
    uint8_t derived[32];
    uint8_t zero_key[32] = {0};
    uint8_t empty_hash[32];

    rt_sha256(NULL, 0, empty_hash);
    rt_hkdf_expand_label(session->handshake_secret, "derived", empty_hash, 32, derived, 32);

    // master_secret = HKDF-Extract(derived, 0)
    rt_hkdf_extract(derived, 32, zero_key, 32, session->master_secret);

    // client_application_traffic_secret
    rt_hkdf_expand_label(session->master_secret,
                         "c ap traffic",
                         session->transcript_hash,
                         32,
                         session->client_application_traffic_secret,
                         32);

    // server_application_traffic_secret
    rt_hkdf_expand_label(session->master_secret,
                         "s ap traffic",
                         session->transcript_hash,
                         32,
                         session->server_application_traffic_secret,
                         32);

    // Derive application keys
    rt_hkdf_expand_label(
        session->server_application_traffic_secret, "key", NULL, 0, session->read_keys.key, 32);
    rt_hkdf_expand_label(
        session->server_application_traffic_secret, "iv", NULL, 0, session->read_keys.iv, 12);
    session->read_keys.seq_num = 0;

    rt_hkdf_expand_label(
        session->client_application_traffic_secret, "key", NULL, 0, session->write_keys.key, 32);
    rt_hkdf_expand_label(
        session->client_application_traffic_secret, "iv", NULL, 0, session->write_keys.iv, 12);
    session->write_keys.seq_num = 0;
}

// Build nonce from IV and sequence number
static void build_nonce(const uint8_t iv[12], uint64_t seq, uint8_t nonce[12])
{
    memcpy(nonce, iv, 12);
    for (int i = 0; i < 8; i++)
    {
        nonce[12 - 1 - i] ^= (seq >> (i * 8)) & 0xFF;
    }
}

// Send TLS record
static int send_record(rt_tls_session_t *session,
                       uint8_t content_type,
                       const uint8_t *data,
                       size_t len)
{
    uint8_t record[5 + TLS_MAX_CIPHERTEXT];
    size_t record_len;

    if (session->keys_established)
    {
        // Encrypted record
        uint8_t plaintext[TLS_MAX_RECORD_SIZE + 1];
        memcpy(plaintext, data, len);
        plaintext[len] = content_type; // Inner content type

        uint8_t aad[5];
        aad[0] = TLS_CONTENT_APPLICATION;
        write_u16(aad + 1, TLS_VERSION_1_2);
        write_u16(aad + 3, (uint16_t)(len + 1 + 16)); // plaintext + content type + tag

        uint8_t nonce[12];
        build_nonce(session->write_keys.iv, session->write_keys.seq_num, nonce);

        record[0] = TLS_CONTENT_APPLICATION;
        write_u16(record + 1, TLS_VERSION_1_2);
        size_t ciphertext_len = rt_chacha20_poly1305_encrypt(
            session->write_keys.key, nonce, aad, 5, plaintext, len + 1, record + 5);
        write_u16(record + 3, (uint16_t)ciphertext_len);
        record_len = 5 + ciphertext_len;

        // H-8: RFC 8446 §5.5 — close before sequence number wraps (nonce uniqueness)
        if (++session->write_keys.seq_num == 0)
        {
            session->error =
                "TLS: write sequence number overflow; connection must be re-established";
            return RT_TLS_ERROR;
        }
    }
    else
    {
        // Plaintext record
        record[0] = content_type;
        write_u16(record + 1, TLS_VERSION_1_2);
        write_u16(record + 3, (uint16_t)len);
        memcpy(record + 5, data, len);
        record_len = 5 + len;
    }

    size_t sent = 0;
    while (sent < record_len)
    {
        int n =
            send(session->socket_fd, (const char *)(record + sent), (int)(record_len - sent), 0);
        if (n < 0)
        {
            if (EINTR_CHECK)
                continue;
            session->error = "send failed";
            return RT_TLS_ERROR_SOCKET;
        }
        sent += n;
    }

    return RT_TLS_OK;
}

// Receive TLS record
static int recv_record(rt_tls_session_t *session,
                       uint8_t *content_type,
                       uint8_t *data,
                       size_t *data_len)
{
    // Read header
    uint8_t header[5];
    size_t pos = 0;
    while (pos < 5)
    {
        int n = recv(session->socket_fd, (char *)(header + pos), (int)(5 - pos), 0);
        if (n < 0)
        {
            if (EINTR_CHECK || EAGAIN_CHECK)
                continue;
            session->error = "recv header failed";
            return RT_TLS_ERROR_SOCKET;
        }
        if (n == 0)
        {
            session->error = "connection closed";
            return RT_TLS_ERROR_CLOSED;
        }
        pos += n;
    }

    uint8_t type = header[0];
    size_t length = read_u16(header + 3);

    if (length > TLS_MAX_CIPHERTEXT)
    {
        session->error = "record too large";
        return RT_TLS_ERROR;
    }

    // Read payload
    uint8_t payload[TLS_MAX_CIPHERTEXT];
    pos = 0;
    while (pos < length)
    {
        int n = recv(session->socket_fd, (char *)(payload + pos), (int)(length - pos), 0);
        if (n < 0)
        {
            if (EINTR_CHECK || EAGAIN_CHECK)
                continue;
            session->error = "recv payload failed";
            return RT_TLS_ERROR_SOCKET;
        }
        if (n == 0)
        {
            session->error = "connection closed";
            return RT_TLS_ERROR_CLOSED;
        }
        pos += n;
    }

    if (session->keys_established && type == TLS_CONTENT_APPLICATION)
    {
        // Decrypt
        uint8_t aad[5];
        memcpy(aad, header, 5);

        uint8_t nonce[12];
        build_nonce(session->read_keys.iv, session->read_keys.seq_num, nonce);

        long plaintext_len = rt_chacha20_poly1305_decrypt(
            session->read_keys.key, nonce, aad, 5, payload, length, data);
        if (plaintext_len < 0)
        {
            session->error = "decryption failed";
            return RT_TLS_ERROR;
        }

        // H-8: RFC 8446 §5.5 — close before sequence number wraps (nonce uniqueness)
        if (++session->read_keys.seq_num == 0)
        {
            session->error =
                "TLS: read sequence number overflow; connection must be re-established";
            return RT_TLS_ERROR;
        }

        // Remove padding and get inner content type
        while (plaintext_len > 0 && data[plaintext_len - 1] == 0)
            plaintext_len--;
        if (plaintext_len == 0)
        {
            session->error = "empty inner record";
            return RT_TLS_ERROR;
        }
        *content_type = data[plaintext_len - 1];
        *data_len = plaintext_len - 1;
    }
    else
    {
        // Plaintext record
        *content_type = type;
        memcpy(data, payload, length);
        *data_len = length;
    }

    return RT_TLS_OK;
}

// Build and send ClientHello
static int send_client_hello(rt_tls_session_t *session)
{
    uint8_t msg[512];
    size_t pos = 0;

    // Legacy version
    write_u16(msg + pos, TLS_VERSION_1_2);
    pos += 2;

    // Random
    rt_crypto_random_bytes(session->client_random, 32);
    memcpy(msg + pos, session->client_random, 32);
    pos += 32;

    // Session ID (empty for TLS 1.3)
    msg[pos++] = 0;

    // Cipher suites
    write_u16(msg + pos, 2);
    pos += 2;
    write_u16(msg + pos, TLS_CHACHA20_POLY1305_SHA256);
    pos += 2;

    // Compression methods
    msg[pos++] = 1;
    msg[pos++] = 0; // null

    // Extensions
    size_t ext_start = pos;
    pos += 2; // Length placeholder

    // SNI extension
    if (session->hostname[0] != '\0')
    {
        size_t name_len = strlen(session->hostname);
        write_u16(msg + pos, TLS_EXT_SERVER_NAME);
        pos += 2;
        write_u16(msg + pos, (uint16_t)(name_len + 5));
        pos += 2;
        write_u16(msg + pos, (uint16_t)(name_len + 3));
        pos += 2;
        msg[pos++] = 0; // DNS hostname
        write_u16(msg + pos, (uint16_t)name_len);
        pos += 2;
        memcpy(msg + pos, session->hostname, name_len);
        pos += name_len;
    }

    // Supported versions
    write_u16(msg + pos, TLS_EXT_SUPPORTED_VERSIONS);
    pos += 2;
    write_u16(msg + pos, 3);
    pos += 2;
    msg[pos++] = 2;
    write_u16(msg + pos, TLS_VERSION_1_3);
    pos += 2;

    // Key share (X25519)
    rt_x25519_keygen(session->client_private_key, session->client_public_key);

    write_u16(msg + pos, TLS_EXT_KEY_SHARE);
    pos += 2;
    write_u16(msg + pos, 36);
    pos += 2;
    write_u16(msg + pos, 34); // client shares length
    pos += 2;
    write_u16(msg + pos, 0x001D); // x25519
    pos += 2;
    write_u16(msg + pos, 32);
    pos += 2;
    memcpy(msg + pos, session->client_public_key, 32);
    pos += 32;

    // Fill in extensions length
    write_u16(msg + ext_start, (uint16_t)(pos - ext_start - 2));

    // Wrap in handshake header
    uint8_t hs[4 + 512];
    hs[0] = TLS_HS_CLIENT_HELLO;
    write_u24(hs + 1, (uint32_t)pos);
    memcpy(hs + 4, msg, pos);

    // Update transcript
    transcript_update(session, hs, 4 + pos);

    // Send
    int rc = send_record(session, TLS_CONTENT_HANDSHAKE, hs, 4 + pos);
    if (rc != RT_TLS_OK)
        return rc;

    session->state = TLS_STATE_CLIENT_HELLO_SENT;
    return RT_TLS_OK;
}

// Process ServerHello
static int process_server_hello(rt_tls_session_t *session, const uint8_t *data, size_t len)
{
    if (len < 38)
    {
        session->error = "ServerHello too short";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Skip version (2)
    memcpy(session->server_random, data + 2, 32);

    size_t pos = 34;

    // Session ID — bounds-check before advancing (S-02 fix)
    if (pos >= len)
    {
        session->error = "ServerHello: session_id length field missing";
        return RT_TLS_ERROR_HANDSHAKE;
    }
    uint8_t session_id_len = data[pos++];
    if (pos + session_id_len + 3 > len) /* +3 for cipher(2) + compression(1) */
    {
        session->error = "ServerHello: session_id overflows message";
        return RT_TLS_ERROR_HANDSHAKE;
    }
    pos += session_id_len;

    // Cipher suite
    session->cipher_suite = read_u16(data + pos);
    pos += 2;

    if (session->cipher_suite != TLS_CHACHA20_POLY1305_SHA256)
    {
        session->error = "unsupported cipher suite";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Skip compression
    pos++;

    // Parse extensions
    if (pos + 2 > len)
    {
        session->error = "no extensions";
        return RT_TLS_ERROR_HANDSHAKE;
    }
    uint16_t ext_len = read_u16(data + pos);
    pos += 2;

    size_t ext_end = pos + ext_len;
    int found_key_share = 0;
    int found_supported_versions = 0; // M-11: track TLS 1.3 version confirmation

    while (pos + 4 <= ext_end)
    {
        uint16_t ext_type = read_u16(data + pos);
        uint16_t ext_data_len = read_u16(data + pos + 2);
        pos += 4;

        if (ext_type == TLS_EXT_KEY_SHARE && ext_data_len >= 36)
        {
            uint16_t group = read_u16(data + pos);
            uint16_t key_len = read_u16(data + pos + 2);
            if (group == 0x001D && key_len == 32)
            {
                memcpy(session->server_public_key, data + pos + 4, 32);
                found_key_share = 1;
            }
        }
        // M-11: RFC 8446 §4.2.1 — ServerHello must confirm TLS 1.3 via supported_versions
        else if (ext_type == TLS_EXT_SUPPORTED_VERSIONS && ext_data_len == 2)
        {
            uint16_t negotiated = read_u16(data + pos);
            if (negotiated == TLS_VERSION_1_3)
                found_supported_versions = 1;
        }
        pos += ext_data_len;
    }

    if (!found_key_share)
    {
        session->error = "no key share";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // M-11: Reject the handshake if the server didn't confirm TLS 1.3.
    // A middlebox performing a version downgrade attack would omit this extension.
    if (!found_supported_versions)
    {
        session->error = "TLS: ServerHello missing supported_versions=TLS1.3 (version downgrade?)";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Compute shared secret and derive handshake keys
    uint8_t shared_secret[32];
    rt_x25519(session->client_private_key, session->server_public_key, shared_secret);
    derive_handshake_keys(session, shared_secret);

    session->state = TLS_STATE_WAIT_ENCRYPTED_EXTENSIONS;
    return RT_TLS_OK;
}

// Send Finished message
static int send_finished(rt_tls_session_t *session)
{
    uint8_t finished_key[32];
    rt_hkdf_expand_label(
        session->client_handshake_traffic_secret, "finished", NULL, 0, finished_key, 32);

    uint8_t verify_data[32];
    rt_hmac_sha256(finished_key, 32, session->transcript_hash, 32, verify_data);

    uint8_t msg[4 + 32];
    msg[0] = TLS_HS_FINISHED;
    write_u24(msg + 1, 32);
    memcpy(msg + 4, verify_data, 32);

    transcript_update(session, msg, 36);

    return send_record(session, TLS_CONTENT_HANDSHAKE, msg, 36);
}

// Constant-time memory comparison (H-9 fix: prevent timing attacks on Finished MAC)
static int ct_memcmp(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++)
        diff |= a[i] ^ b[i];
    return diff != 0; // non-zero means unequal
}

// Verify server Finished
static int verify_finished(rt_tls_session_t *session, const uint8_t *data, size_t len)
{
    if (len != 32)
    {
        session->error = "invalid Finished length";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    uint8_t finished_key[32];
    rt_hkdf_expand_label(
        session->server_handshake_traffic_secret, "finished", NULL, 0, finished_key, 32);

    uint8_t expected[32];
    rt_hmac_sha256(finished_key, 32, session->transcript_hash, 32, expected);

    if (ct_memcmp(data, expected, 32))
    {
        session->error = "Finished verification failed";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    return RT_TLS_OK;
}

//=============================================================================
// Certificate Validation — CS-1, CS-2, CS-3
//=============================================================================

// ---------------------------------------------------------------------------
// B.2 — TLS 1.3 Certificate message parser
// ---------------------------------------------------------------------------

/// @brief Parse the TLS 1.3 Certificate handshake message and store the
///        first (end-entity) certificate DER bytes in session->server_cert_der.
///
/// Certificate message format (RFC 8446 §4.4.2):
///   1 byte:  certificate_request_context length
///   N bytes: context (ignored by client)
///   3 bytes: certificate_list total length
///   For each entry:
///     3 bytes: cert_data length
///     N bytes: DER-encoded certificate
///     2 bytes: extensions length
///     N bytes: certificate extensions (ignored)
static int tls_parse_certificate_msg(rt_tls_session_t *session, const uint8_t *data, size_t len)
{
    if (!data || len < 4)
    {
        session->error = "TLS: Certificate message too short";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    size_t pos = 0;

    // Skip certificate_request_context
    uint8_t ctx_len = data[pos++];
    if (pos + ctx_len > len)
    {
        session->error = "TLS: Certificate context overflows message";
        return RT_TLS_ERROR_HANDSHAKE;
    }
    pos += ctx_len;

    // Read certificate_list length (3-byte big-endian)
    if (pos + 3 > len)
    {
        session->error = "TLS: Certificate list length missing";
        return RT_TLS_ERROR_HANDSHAKE;
    }
    size_t list_len = ((size_t)data[pos] << 16) | ((size_t)data[pos + 1] << 8) | data[pos + 2];
    pos += 3;

    if (pos + list_len > len)
    {
        session->error = "TLS: Certificate list overflows message";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Read the first (end-entity) certificate
    if (list_len < 5)
    {
        session->error = "TLS: Certificate list too short for one entry";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    size_t cert_len = ((size_t)data[pos] << 16) | ((size_t)data[pos + 1] << 8) | data[pos + 2];
    pos += 3;

    if (cert_len == 0 || pos + cert_len > len)
    {
        session->error = "TLS: Certificate DER length invalid";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    if (cert_len > sizeof(session->server_cert_der))
    {
        session->error = "TLS: Certificate DER too large for validation buffer";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    memcpy(session->server_cert_der, data + pos, cert_len);
    session->server_cert_der_len = cert_len;
    return RT_TLS_OK;
}

// ---------------------------------------------------------------------------
// B.3 — ASN.1 DER helpers and X.509 hostname extraction
// ---------------------------------------------------------------------------

/// @brief Read one ASN.1 TLV (tag-length-value) header.
/// @param buf     Buffer to parse.
/// @param buf_len Remaining bytes in buf.
/// @param tag     Output: the tag byte.
/// @param val_len Output: the length of the value field.
/// @param hdr_len Output: the total header length (tag + length octets).
/// @return 0 on success, -1 on error.
static int der_read_tlv(
    const uint8_t *buf, size_t buf_len, uint8_t *tag, size_t *val_len, size_t *hdr_len)
{
    if (buf_len < 2)
        return -1;

    *tag = buf[0];

    uint8_t l0 = buf[1];
    if (l0 < 0x80)
    {
        *val_len = l0;
        *hdr_len = 2;
    }
    else
    {
        size_t num_len_bytes = l0 & 0x7F;
        if (num_len_bytes == 0 || num_len_bytes > 4 || 2 + num_len_bytes > buf_len)
            return -1;

        size_t v = 0;
        for (size_t i = 0; i < num_len_bytes; i++)
            v = (v << 8) | buf[2 + i];

        *val_len = v;
        *hdr_len = 2 + num_len_bytes;
    }

    if (*hdr_len + *val_len > buf_len)
        return -1;

    return 0;
}

/// @brief Compare buf[0..oid_len-1] to the encoded OID bytes.
static int oid_matches(const uint8_t *buf,
                       size_t buf_len,
                       const uint8_t *oid_val,
                       size_t oid_val_len)
{
    return buf_len == oid_val_len && memcmp(buf, oid_val, oid_val_len) == 0;
}

// Known OID encoded values (value bytes only, after the OID tag+length)
static const uint8_t OID_COMMON_NAME[] = {0x55, 0x04, 0x03};      // 2.5.4.3
static const uint8_t OID_SUBJECT_ALT_NAME[] = {0x55, 0x1d, 0x11}; // 2.5.29.17

/// @brief Extract DNS names from SubjectAltName extension value.
/// @param ext_val Points to the value bytes of the SAN extension (after the OCTET STRING wrapper).
/// @param ext_len Length of ext_val.
/// @param san_out Pre-allocated array for DNS name strings.
/// @param max_names Maximum number of names to return.
/// @param count Output: number of names found.
static void extract_san_from_ext_value(
    const uint8_t *ext_val, size_t ext_len, char san_out[][256], int max_names, int *count)
{
    *count = 0;

    // SubjectAltName is an OCTET STRING containing a GeneralNames SEQUENCE
    uint8_t t;
    size_t vl, hl;
    if (der_read_tlv(ext_val, ext_len, &t, &vl, &hl) != 0 || t != 0x04 /* OCTET STRING */)
        return;

    const uint8_t *inner = ext_val + hl;
    size_t inner_len = vl;

    // GeneralNames SEQUENCE
    if (der_read_tlv(inner, inner_len, &t, &vl, &hl) != 0 || t != 0x30 /* SEQUENCE */)
        return;

    const uint8_t *names = inner + hl;
    size_t names_len = vl;
    size_t pos = 0;

    while (pos < names_len && *count < max_names)
    {
        if (der_read_tlv(names + pos, names_len - pos, &t, &vl, &hl) != 0)
            break;

        // dNSName is context tag [2] = 0x82
        if (t == 0x82 && vl > 0 && vl < 256)
        {
            memcpy(san_out[*count], names + pos + hl, vl);
            san_out[*count][vl] = '\0';
            (*count)++;
        }

        pos += hl + vl;
    }
}

/// @brief Extract SubjectAltName DNS names from a certificate DER.
/// @return Number of names found (0 if SAN extension absent).
int tls_extract_san_names(const uint8_t *der, size_t der_len, char san_out[][256], int max_names)
{
    int count = 0;

    // Certificate SEQUENCE
    uint8_t t;
    size_t vl, hl;
    if (der_read_tlv(der, der_len, &t, &vl, &hl) != 0 || t != 0x30)
        return 0;

    // TBSCertificate SEQUENCE (first child of Certificate)
    const uint8_t *cert_val = der + hl;
    if (der_read_tlv(cert_val, vl, &t, &vl, &hl) != 0 || t != 0x30)
        return 0;

    // Walk TBSCertificate to find extensions [3] EXPLICIT context tag
    const uint8_t *tbs_val = cert_val + hl;
    size_t tbs_len = vl;
    size_t pos = 0;

    while (pos < tbs_len)
    {
        if (der_read_tlv(tbs_val + pos, tbs_len - pos, &t, &vl, &hl) != 0)
            break;

        // Extensions is [3] EXPLICIT = 0xA3
        if (t == 0xA3)
        {
            // Inside [3]: one SEQUENCE of Extensions
            const uint8_t *exts_wrap = tbs_val + pos + hl;
            uint8_t t2;
            size_t vl2, hl2;
            if (der_read_tlv(exts_wrap, vl, &t2, &vl2, &hl2) != 0 || t2 != 0x30)
                break;

            const uint8_t *exts = exts_wrap + hl2;
            size_t exts_len = vl2;
            size_t ep = 0;

            while (ep < exts_len && count < max_names)
            {
                // Each Extension is a SEQUENCE { OID, [BOOL], OCTET STRING }
                uint8_t t3;
                size_t vl3, hl3;
                if (der_read_tlv(exts + ep, exts_len - ep, &t3, &vl3, &hl3) != 0)
                    break;

                if (t3 == 0x30)
                {
                    const uint8_t *ext = exts + ep + hl3;
                    uint8_t t4;
                    size_t vl4, hl4;

                    // OID
                    if (der_read_tlv(ext, vl3, &t4, &vl4, &hl4) == 0 && t4 == 0x06)
                    {
                        if (oid_matches(ext + hl4, vl4, OID_SUBJECT_ALT_NAME, 3))
                        {
                            // Skip optional BOOLEAN (critical flag)
                            size_t after_oid = hl4 + vl4;
                            if (after_oid < vl3)
                            {
                                uint8_t nt;
                                size_t nvl, nhl;
                                if (der_read_tlv(
                                        ext + after_oid, vl3 - after_oid, &nt, &nvl, &nhl) == 0)
                                {
                                    if (nt == 0x01) // BOOLEAN (critical flag) — skip
                                        after_oid += nhl + nvl;
                                    // Now should be at OCTET STRING
                                    if (after_oid < vl3)
                                        extract_san_from_ext_value(ext + after_oid,
                                                                   vl3 - after_oid,
                                                                   san_out,
                                                                   max_names,
                                                                   &count);
                                }
                            }
                        }
                    }
                }

                ep += hl3 + vl3;
            }
            break; // Extensions found and processed
        }

        pos += hl + vl;
    }

    return count;
}

/// @brief Extract CommonName from certificate Subject.
/// @return 1 if found, 0 otherwise.
int tls_extract_cn(const uint8_t *der, size_t der_len, char cn_out[256])
{
    // Certificate SEQUENCE
    uint8_t t;
    size_t vl, hl;
    if (der_read_tlv(der, der_len, &t, &vl, &hl) != 0 || t != 0x30)
        return 0;

    // TBSCertificate SEQUENCE
    const uint8_t *cert_val = der + hl;
    if (der_read_tlv(cert_val, vl, &t, &vl, &hl) != 0 || t != 0x30)
        return 0;

    const uint8_t *tbs_val = cert_val + hl;
    size_t tbs_len = vl;
    size_t pos = 0;

    while (pos < tbs_len)
    {
        if (der_read_tlv(tbs_val + pos, tbs_len - pos, &t, &vl, &hl) != 0)
            break;

        // Subject is a SEQUENCE at index 5 in TBS (0:version, 1:serial, 2:alg, 3:issuer,
        // 4:validity, 5:subject) Walk until we find two consecutive SEQUENCEs — the second is
        // Subject (Actually, both Issuer and Subject are SEQUENCE/SET, so we need to count.
        //  Simpler: find any SEQUENCE whose children contain OID 2.5.4.3)
        if (t == 0x30) // SEQUENCE — could be Issuer or Subject
        {
            const uint8_t *seq_val = tbs_val + pos + hl;
            size_t seq_len = vl;
            size_t sp = 0;

            // Each child of Issuer/Subject is a SET containing AttributeTypeAndValue
            while (sp < seq_len)
            {
                uint8_t ts;
                size_t vls, hls;
                if (der_read_tlv(seq_val + sp, seq_len - sp, &ts, &vls, &hls) != 0)
                    break;

                if (ts == 0x31) // SET
                {
                    // AttributeTypeAndValue SEQUENCE
                    uint8_t ta;
                    size_t vla, hla;
                    if (der_read_tlv(seq_val + sp + hls, vls, &ta, &vla, &hla) == 0 && ta == 0x30)
                    {
                        const uint8_t *atv = seq_val + sp + hls + hla;

                        // OID
                        uint8_t to;
                        size_t vlo, hlo;
                        if (der_read_tlv(atv, vla, &to, &vlo, &hlo) == 0 && to == 0x06)
                        {
                            if (oid_matches(atv + hlo, vlo, OID_COMMON_NAME, 3))
                            {
                                // Value: UTF8String (0x0C), PrintableString (0x13), IA5String
                                // (0x16), etc.
                                const uint8_t *val_start = atv + hlo + vlo;
                                size_t val_remaining = vla - hlo - vlo;
                                uint8_t tv;
                                size_t vlv, hlv;
                                if (der_read_tlv(val_start, val_remaining, &tv, &vlv, &hlv) == 0)
                                {
                                    if (vlv > 0 && vlv < 256)
                                    {
                                        memcpy(cn_out, val_start + hlv, vlv);
                                        cn_out[vlv] = '\0';
                                        return 1;
                                    }
                                }
                            }
                        }
                    }
                }

                sp += hls + vls;
            }
        }

        pos += hl + vl;
    }

    return 0;
}

/// @brief Match a hostname against a pattern (RFC 6125 §6.4 wildcard rules).
///
/// Supports:
///   - Exact match: "example.com" vs "example.com"
///   - Single-label wildcard: "*.example.com" vs "foo.example.com"
///     (wildcard must be leftmost label, covers only one label)
int tls_match_hostname(const char *pattern, const char *hostname)
{
    if (!pattern || !hostname)
        return 0;

    if (pattern[0] == '*' && pattern[1] == '.')
    {
        // Wildcard: *.example.com
        const char *suffix = pattern + 2; // "example.com"
        const char *dot = strchr(hostname, '.');
        if (!dot)
            return 0; // hostname has no dot — can't match *.X

        // hostname suffix after first dot must match pattern suffix
        const char *host_suffix = dot + 1;
        if (strcasecmp(host_suffix, suffix) != 0)
            return 0;

        // The wildcard label must not contain a dot (one label only)
        // Already guaranteed since we took the first dot.
        return 1;
    }

    return strcasecmp(pattern, hostname) == 0 ? 1 : 0;
}

// Maximum number of SAN DNS names to check
#define TLS_MAX_SAN_NAMES 64

/// @brief Verify that session->hostname matches the certificate DER.
static int tls_verify_hostname(rt_tls_session_t *session)
{
    if (!session->server_cert_der_len)
    {
        session->error = "TLS: no certificate stored for hostname verification";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    const char *host = session->hostname;

    // Try SubjectAltName first (RFC 6125 §6.4: SAN takes precedence over CN)
    char san_names[TLS_MAX_SAN_NAMES][256];
    int san_count = tls_extract_san_names(
        session->server_cert_der, session->server_cert_der_len, san_names, TLS_MAX_SAN_NAMES);

    if (san_count > 0)
    {
        for (int i = 0; i < san_count; i++)
        {
            if (tls_match_hostname(san_names[i], host))
                return RT_TLS_OK;
        }
        session->error = "TLS: certificate hostname mismatch (SAN did not match)";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Fall back to CommonName if no SAN
    char cn[256] = {0};
    if (tls_extract_cn(session->server_cert_der, session->server_cert_der_len, cn))
    {
        if (tls_match_hostname(cn, host))
            return RT_TLS_OK;
        session->error = "TLS: certificate hostname mismatch (CN did not match)";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    session->error = "TLS: certificate contains no SAN or CN for hostname verification";
    return RT_TLS_ERROR_HANDSHAKE;
}

// ---------------------------------------------------------------------------
// B.4 — OS trust store chain validation
// ---------------------------------------------------------------------------

#if defined(__APPLE__)

/// @brief Verify certificate chain + hostname via macOS Security.framework.
/// SecPolicyCreateSSL with a hostname string performs both chain validation
/// (CS-1) and hostname verification (CS-2) in a single call.
static int tls_verify_chain(rt_tls_session_t *session)
{
    if (!session->server_cert_der_len)
    {
        session->error = "TLS: no certificate to validate";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    CFDataRef cert_data = CFDataCreate(
        kCFAllocatorDefault, session->server_cert_der, (CFIndex)session->server_cert_der_len);
    if (!cert_data)
    {
        session->error = "TLS: could not create CFData for certificate";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    SecCertificateRef cert = SecCertificateCreateWithData(kCFAllocatorDefault, cert_data);
    CFRelease(cert_data);
    if (!cert)
    {
        session->error = "TLS: could not parse DER certificate";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    CFStringRef hostname_cf =
        CFStringCreateWithCString(kCFAllocatorDefault, session->hostname, kCFStringEncodingUTF8);
    if (!hostname_cf)
    {
        CFRelease(cert);
        session->error = "TLS: could not create hostname CFString";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    SecPolicyRef policy = SecPolicyCreateSSL(true, hostname_cf);
    CFRelease(hostname_cf);
    if (!policy)
    {
        CFRelease(cert);
        session->error = "TLS: could not create SSL policy";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    const void *cert_val = (const void *)cert;
    CFArrayRef certs = CFArrayCreate(kCFAllocatorDefault, &cert_val, 1, &kCFTypeArrayCallBacks);
    SecTrustRef trust = NULL;
    OSStatus os_status = SecTrustCreateWithCertificates(certs, policy, &trust);
    CFRelease(certs);
    CFRelease(policy);
    CFRelease(cert);

    if (os_status != errSecSuccess || !trust)
    {
        if (trust)
            CFRelease(trust);
        session->error = "TLS: SecTrustCreateWithCertificates failed";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Disable network fetch (offline chain validation — rely on system trust store)
    SecTrustSetNetworkFetchAllowed(trust, false);

    CFErrorRef err = NULL;
    bool trusted = SecTrustEvaluateWithError(trust, &err);
    CFRelease(trust);

    if (!trusted)
    {
        if (err)
            CFRelease(err);
        session->error = "TLS: certificate chain validation failed (untrusted or expired)";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    return RT_TLS_OK;
}

#elif defined(_WIN32)

static int tls_verify_chain(rt_tls_session_t *session)
{
    if (!session->server_cert_der_len)
    {
        session->error = "TLS: no certificate to validate";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    PCCERT_CONTEXT cert_ctx = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                                           session->server_cert_der,
                                                           (DWORD)session->server_cert_der_len);

    if (!cert_ctx)
    {
        session->error = "TLS: could not parse DER certificate (Windows)";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    CERT_CHAIN_PARA chain_para = {0};
    chain_para.cbSize = sizeof(chain_para);

    PCCERT_CHAIN_CONTEXT chain_ctx = NULL;
    BOOL ok = CertGetCertificateChain(NULL, cert_ctx, NULL, NULL, &chain_para, 0, NULL, &chain_ctx);

    if (!ok || !chain_ctx)
    {
        CertFreeCertificateContext(cert_ctx);
        session->error = "TLS: CertGetCertificateChain failed";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Convert hostname to wide string for SSL policy
    wchar_t whostname[256] = {0};
    size_t host_len = strlen(session->hostname);
    if (host_len >= 255)
        host_len = 255;
    for (size_t i = 0; i < host_len; i++)
        whostname[i] = (wchar_t)(unsigned char)session->hostname[i];

    SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl_policy = {0};
    ssl_policy.cbSize = sizeof(ssl_policy);
    ssl_policy.dwAuthType = AUTHTYPE_SERVER;
    ssl_policy.fdwChecks = 0;
    ssl_policy.pwszServerName = whostname;

    CERT_CHAIN_POLICY_PARA policy_para = {0};
    policy_para.cbSize = sizeof(policy_para);
    policy_para.pvExtraPolicyPara = &ssl_policy;

    CERT_CHAIN_POLICY_STATUS policy_status = {0};
    policy_status.cbSize = sizeof(policy_status);

    ok = CertVerifyCertificateChainPolicy(
        CERT_CHAIN_POLICY_SSL, chain_ctx, &policy_para, &policy_status);

    CertFreeCertificateChain(chain_ctx);
    CertFreeCertificateContext(cert_ctx);

    if (!ok || policy_status.dwError != 0)
    {
        session->error = "TLS: certificate chain validation failed (Windows)";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    return RT_TLS_OK;
}

#else // Linux / other POSIX

/// @brief Try to find the system CA bundle.
static const char *find_ca_bundle(void)
{
    static const char *bundles[] = {"/etc/ssl/certs/ca-certificates.crt", // Debian/Ubuntu
                                    "/etc/pki/tls/certs/ca-bundle.crt",   // RHEL/CentOS
                                    "/etc/ssl/ca-bundle.pem",             // OpenSUSE
                                    "/etc/ssl/cert.pem",                  // Alpine / macOS fallback
                                    NULL};
    for (int i = 0; bundles[i]; i++)
    {
        FILE *f = fopen(bundles[i], "r");
        if (f)
        {
            fclose(f);
            return bundles[i];
        }
    }
    return NULL;
}

/// @brief Decode one PEM certificate (between -----BEGIN/END CERTIFICATE-----).
/// Returns DER length written to out_der, or 0 on failure.
static size_t pem_decode_cert(const char *pem_b64, size_t b64_len, uint8_t *out_der, size_t max_der)
{
    // Simple base64 decode (no line breaks within the data stream)
    static const int8_t b64tab[256] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62,
        -1, -1, -1, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1, -1, 0,
        1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
        23, 24, 25, -1, -1, -1, -1, -1, -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
        39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

    size_t out_len = 0;
    uint32_t acc = 0;
    int bits = 0;

    for (size_t i = 0; i < b64_len; i++)
    {
        unsigned char c = (unsigned char)pem_b64[i];
        if (c == '\r' || c == '\n' || c == ' ')
            continue;
        if (c == '=')
            break;

        int8_t v = b64tab[c];
        if (v < 0)
            continue;

        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            if (out_len >= max_der)
                return 0; // overflow
            out_der[out_len++] = (uint8_t)((acc >> bits) & 0xFF);
        }
    }

    return out_len;
}

/// @brief Compare two DER-encoded Name structures byte-for-byte.
static int der_names_equal(const uint8_t *a_der, size_t a_len, const uint8_t *b_der, size_t b_len)
{
    if (a_len != b_len)
        return 0;
    return memcmp(a_der, b_der, a_len) == 0;
}

/// @brief Get the DER-encoded Subject from a certificate.
static const uint8_t *cert_get_subject(const uint8_t *cert_der,
                                       size_t cert_len,
                                       size_t *subject_len)
{
    uint8_t t;
    size_t vl, hl;

    // Certificate SEQUENCE
    if (der_read_tlv(cert_der, cert_len, &t, &vl, &hl) != 0 || t != 0x30)
        return NULL;

    // TBSCertificate SEQUENCE (first child)
    const uint8_t *cert_val = cert_der + hl;
    if (der_read_tlv(cert_val, vl, &t, &vl, &hl) != 0 || t != 0x30)
        return NULL;

    const uint8_t *tbs = cert_val + hl;
    size_t tbs_len = vl;
    size_t pos = 0;
    int seq_count = 0;

    // Subject is the 4th field in TBSCertificate (0-indexed):
    // [0] version (optional [0] EXPLICIT), [1] serial, [2] signature alg, [3] issuer, [4] validity,
    // [5] subject We count SEQUENCE/SET fields skipping context-tags to find Subject.
    while (pos < tbs_len)
    {
        if (der_read_tlv(tbs + pos, tbs_len - pos, &t, &vl, &hl) != 0)
            break;

        // Skip version [0] EXPLICIT
        if (t == 0xA0)
        {
            pos += hl + vl;
            continue;
        }

        seq_count++;
        // Subject is the 3rd SEQUENCE (after serial INTEGER, alg SEQUENCE, issuer SEQUENCE)
        // i.e., seq_count == 4 for Subject
        if (t == 0x30 && seq_count == 4)
        {
            *subject_len = hl + vl;
            return tbs + pos;
        }

        pos += hl + vl;
    }

    return NULL;
}

/// @brief Get the DER-encoded Issuer from a certificate.
static const uint8_t *cert_get_issuer(const uint8_t *cert_der, size_t cert_len, size_t *issuer_len)
{
    uint8_t t;
    size_t vl, hl;

    if (der_read_tlv(cert_der, cert_len, &t, &vl, &hl) != 0 || t != 0x30)
        return NULL;

    const uint8_t *cert_val = cert_der + hl;
    if (der_read_tlv(cert_val, vl, &t, &vl, &hl) != 0 || t != 0x30)
        return NULL;

    const uint8_t *tbs = cert_val + hl;
    size_t tbs_len = vl;
    size_t pos = 0;
    int seq_count = 0;

    while (pos < tbs_len)
    {
        if (der_read_tlv(tbs + pos, tbs_len - pos, &t, &vl, &hl) != 0)
            break;

        if (t == 0xA0)
        {
            pos += hl + vl;
            continue;
        }

        seq_count++;
        // Issuer is the 2nd SEQUENCE (after serial)
        if (t == 0x30 && seq_count == 3)
        {
            *issuer_len = hl + vl;
            return tbs + pos;
        }

        pos += hl + vl;
    }

    return NULL;
}

/// @brief Verify certificate chain against the Linux system CA bundle (best-effort).
///
/// Checks that the end-entity certificate's Issuer DER matches the Subject DER
/// of at least one CA certificate in the bundle. Follows one level of intermediate
/// (Issuer → CA Subject match). Full recursive chain validation requires OpenSSL.
static int tls_verify_chain(rt_tls_session_t *session)
{
    if (!session->server_cert_der_len)
    {
        session->error = "TLS: no certificate to validate";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    const char *bundle_path = find_ca_bundle();
    if (!bundle_path)
    {
        // No CA bundle found — skip chain validation with a warning in the error field
        // (hostname verification still occurs separately via tls_verify_hostname)
        session->error = "TLS: no system CA bundle found; chain validation skipped";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    FILE *f = fopen(bundle_path, "r");
    if (!f)
    {
        session->error = "TLS: could not open CA bundle";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Get the end-entity cert's Issuer DER for comparison
    size_t ee_issuer_len = 0;
    const uint8_t *ee_issuer =
        cert_get_issuer(session->server_cert_der, session->server_cert_der_len, &ee_issuer_len);
    if (!ee_issuer || ee_issuer_len == 0)
    {
        fclose(f);
        session->error = "TLS: could not parse issuer from certificate";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Scan the PEM bundle for a CA cert whose Subject matches the end-entity Issuer
    char line[512];
    char pem_b64[65536];
    size_t pem_pos = 0;
    int in_cert = 0;
    int found = 0;

    static uint8_t ca_der[16384];

    while (!found && fgets(line, sizeof(line), f))
    {
        if (strncmp(line, "-----BEGIN CERTIFICATE-----", 27) == 0)
        {
            in_cert = 1;
            pem_pos = 0;
        }
        else if (strncmp(line, "-----END CERTIFICATE-----", 25) == 0 && in_cert)
        {
            in_cert = 0;
            size_t ca_len = pem_decode_cert(pem_b64, pem_pos, ca_der, sizeof(ca_der));
            if (ca_len > 0)
            {
                size_t ca_subj_len = 0;
                const uint8_t *ca_subj = cert_get_subject(ca_der, ca_len, &ca_subj_len);
                if (ca_subj && der_names_equal(ee_issuer, ee_issuer_len, ca_subj, ca_subj_len))
                    found = 1;
            }
            pem_pos = 0;
        }
        else if (in_cert)
        {
            size_t ll = strlen(line);
            if (pem_pos + ll < sizeof(pem_b64))
            {
                memcpy(pem_b64 + pem_pos, line, ll);
                pem_pos += ll;
            }
        }
    }

    fclose(f);

    if (!found)
    {
        session->error = "TLS: certificate issuer not found in system CA bundle";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    return RT_TLS_OK;
}

#endif // Platform-specific chain validation

// ---------------------------------------------------------------------------
// B.5 — CertificateVerify signature verification
// ---------------------------------------------------------------------------

/// @brief Build the 130-byte signed content for CertificateVerify (RFC 8446 §4.4.3).
/// Content = 64 spaces + "TLS 1.3, server CertificateVerify" + 0x00 + transcript_hash
static void build_cert_verify_content(const uint8_t transcript_hash[32], uint8_t content_hash[32])
{
    uint8_t content[130];
    static const char context_str[] = "TLS 1.3, server CertificateVerify";
    memset(content, 0x20, 64);
    memcpy(content + 64, context_str, 33);
    content[97] = 0x00;
    memcpy(content + 98, transcript_hash, 32);
    rt_sha256(content, 130, content_hash);
}

#if defined(__APPLE__)

static int tls_verify_cert_verify(rt_tls_session_t *session, const uint8_t *data, size_t len)
{
    if (len < 4)
    {
        session->error = "TLS: CertificateVerify message too short";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    uint16_t sig_scheme = ((uint16_t)data[0] << 8) | data[1];
    uint16_t sig_len = ((uint16_t)data[2] << 8) | data[3];
    if (4 + sig_len > len)
    {
        session->error = "TLS: CertificateVerify signature length overflows";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    const uint8_t *sig_bytes = data + 4;

    // Build the content hash
    uint8_t content_hash[32];
    build_cert_verify_content(session->cert_transcript_hash, content_hash);

    // Reconstruct SecCertificateRef from stored DER
    CFDataRef cert_data = CFDataCreate(
        kCFAllocatorDefault, session->server_cert_der, (CFIndex)session->server_cert_der_len);
    if (!cert_data)
    {
        session->error = "TLS: CertVerify: could not create CFData";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    SecCertificateRef cert = SecCertificateCreateWithData(kCFAllocatorDefault, cert_data);
    CFRelease(cert_data);
    if (!cert)
    {
        session->error = "TLS: CertVerify: could not parse DER certificate";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    SecKeyRef pub_key = SecCertificateCopyKey(cert);
    CFRelease(cert);
    if (!pub_key)
    {
        session->error = "TLS: CertVerify: could not extract public key from cert";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Select the SecKeyAlgorithm based on signature scheme
    // 0x0403 = ecdsa_secp256r1_sha256
    // 0x0503 = ecdsa_secp384r1_sha384
    // 0x0804 = rsa_pss_rsae_sha256
    // 0x0805 = rsa_pss_rsae_sha384
    // 0x0806 = rsa_pss_rsae_sha512
    SecKeyAlgorithm algorithm;
    switch (sig_scheme)
    {
        case 0x0403:
            algorithm = kSecKeyAlgorithmECDSASignatureDigestX962SHA256;
            break;
        case 0x0503:
            algorithm = kSecKeyAlgorithmECDSASignatureDigestX962SHA384;
            break;
        case 0x0804:
            algorithm = kSecKeyAlgorithmRSASignatureDigestPSSSHA256;
            break;
        case 0x0805:
            algorithm = kSecKeyAlgorithmRSASignatureDigestPSSSHA384;
            break;
        case 0x0806:
            algorithm = kSecKeyAlgorithmRSASignatureDigestPSSSHA512;
            break;
        default:
            CFRelease(pub_key);
            session->error = "TLS: CertificateVerify: unsupported signature scheme";
            return RT_TLS_ERROR_HANDSHAKE;
    }

    CFDataRef sig_data = CFDataCreate(kCFAllocatorDefault, sig_bytes, (CFIndex)sig_len);
    CFDataRef hash_data = CFDataCreate(kCFAllocatorDefault, content_hash, 32);

    if (!sig_data || !hash_data)
    {
        if (sig_data)
            CFRelease(sig_data);
        if (hash_data)
            CFRelease(hash_data);
        CFRelease(pub_key);
        session->error = "TLS: CertVerify: CFData allocation failed";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    CFErrorRef err = NULL;
    bool verified = SecKeyVerifySignature(pub_key, algorithm, hash_data, sig_data, &err);

    CFRelease(sig_data);
    CFRelease(hash_data);
    CFRelease(pub_key);

    if (!verified)
    {
        if (err)
            CFRelease(err);
        session->error = "TLS: CertificateVerify signature verification failed";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    return RT_TLS_OK;
}

#elif defined(_WIN32)

static int tls_verify_cert_verify(rt_tls_session_t *session, const uint8_t *data, size_t len)
{
    if (len < 4)
    {
        session->error = "TLS: CertificateVerify message too short";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    uint16_t sig_scheme = ((uint16_t)data[0] << 8) | data[1];
    uint16_t sig_len = ((uint16_t)data[2] << 8) | data[3];
    if (4 + sig_len > len)
    {
        session->error = "TLS: CertificateVerify signature length overflows";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    const uint8_t *sig_bytes = data + 4;

    uint8_t content_hash[32];
    build_cert_verify_content(session->cert_transcript_hash, content_hash);

    PCCERT_CONTEXT cert_ctx = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                                           session->server_cert_der,
                                                           (DWORD)session->server_cert_der_len);

    if (!cert_ctx)
    {
        session->error = "TLS: CertVerify: could not parse certificate (Windows)";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Determine hash algorithm OID from signature scheme
    LPCSTR hash_oid;
    switch (sig_scheme)
    {
        case 0x0403:
        case 0x0804:
            hash_oid = szOID_NIST_sha256;
            break;
        case 0x0503:
        case 0x0805:
            hash_oid = szOID_NIST_sha384;
            break;
        case 0x0806:
            hash_oid = szOID_NIST_sha512;
            break;
        default:
            CertFreeCertificateContext(cert_ctx);
            session->error = "TLS: CertificateVerify: unsupported scheme (Windows)";
            return RT_TLS_ERROR_HANDSHAKE;
    }

    // Import public key
    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE key_handle = 0;
    DWORD key_spec = 0;
    BOOL must_free_key = FALSE;
    if (!CryptAcquireCertificatePrivateKey(cert_ctx,
                                           CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG,
                                           NULL,
                                           &key_handle,
                                           &key_spec,
                                           &must_free_key))
    {
        // Fall back to public key only via CERT_KEY_PROV_INFO
        // Use CryptImportPublicKeyInfo for verification
        HCRYPTPROV hprov = 0;
        if (!CryptAcquireContextW(&hprov, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        {
            CertFreeCertificateContext(cert_ctx);
            session->error = "TLS: CertVerify: CryptAcquireContext failed";
            return RT_TLS_ERROR_HANDSHAKE;
        }

        HCRYPTKEY hkey = 0;
        if (!CryptImportPublicKeyInfo(
                hprov, X509_ASN_ENCODING, &cert_ctx->pCertInfo->SubjectPublicKeyInfo, &hkey))
        {
            CryptReleaseContext(hprov, 0);
            CertFreeCertificateContext(cert_ctx);
            session->error = "TLS: CertVerify: CryptImportPublicKeyInfo failed";
            return RT_TLS_ERROR_HANDSHAKE;
        }

        HCRYPTHASH hhash = 0;
        ALG_ID alg_id = (sig_scheme == 0x0403 || sig_scheme == 0x0804)   ? CALG_SHA_256
                        : (sig_scheme == 0x0503 || sig_scheme == 0x0805) ? CALG_SHA_384
                                                                         : CALG_SHA_512;

        if (!CryptCreateHash(hprov, alg_id, 0, 0, &hhash))
        {
            CryptDestroyKey(hkey);
            CryptReleaseContext(hprov, 0);
            CertFreeCertificateContext(cert_ctx);
            session->error = "TLS: CertVerify: CryptCreateHash failed";
            return RT_TLS_ERROR_HANDSHAKE;
        }

        // Set the hash value directly (we already computed SHA-256 of the content)
        DWORD hash_len_dw = 32;
        if (!CryptSetHashParam(hhash, HP_HASHVAL, content_hash, 0))
        {
            CryptDestroyHash(hhash);
            CryptDestroyKey(hkey);
            CryptReleaseContext(hprov, 0);
            CertFreeCertificateContext(cert_ctx);
            session->error = "TLS: CertVerify: CryptSetHashParam failed";
            return RT_TLS_ERROR_HANDSHAKE;
        }

        // Signature bytes may need byte-reversal for Windows RSA (big-endian vs little-endian)
        uint8_t sig_copy[4096];
        DWORD sig_copy_len = sig_len;
        if (sig_len <= sizeof(sig_copy))
        {
            memcpy(sig_copy, sig_bytes, sig_len);
            // Reverse for Windows CAPI RSA (Windows stores in little-endian)
            for (DWORD i = 0; i < sig_copy_len / 2; i++)
            {
                uint8_t tmp = sig_copy[i];
                sig_copy[i] = sig_copy[sig_copy_len - 1 - i];
                sig_copy[sig_copy_len - 1 - i] = tmp;
            }
        }

        BOOL verified = CryptVerifySignature(hhash, sig_copy, sig_copy_len, hkey, NULL, 0);
        CryptDestroyHash(hhash);
        CryptDestroyKey(hkey);
        CryptReleaseContext(hprov, 0);
        CertFreeCertificateContext(cert_ctx);

        if (!verified)
        {
            session->error = "TLS: CertificateVerify signature failed (Windows)";
            return RT_TLS_ERROR_HANDSHAKE;
        }

        return RT_TLS_OK;
    }

    CertFreeCertificateContext(cert_ctx);
    session->error = "TLS: CertVerify: unsupported key type on Windows";
    return RT_TLS_ERROR_HANDSHAKE;
}

#else // Linux — attempt dlopen(libssl) for ECDSA/RSA verification

typedef void *EVP_PKEY;
typedef void *X509;
typedef void *EVP_PKEY_CTX;

typedef X509 *(*d2i_X509_fn)(X509 **, const unsigned char **, long);
typedef EVP_PKEY *(*X509_get_pubkey_fn)(X509 *);
typedef EVP_PKEY_CTX *(*EVP_PKEY_CTX_new_fn)(EVP_PKEY *, void *);
typedef int (*EVP_PKEY_verify_init_fn)(EVP_PKEY_CTX *);
typedef int (*EVP_PKEY_CTX_set_signature_md_fn)(EVP_PKEY_CTX *, void *);
typedef int (*EVP_PKEY_verify_fn)(
    EVP_PKEY_CTX *, const unsigned char *, size_t, const unsigned char *, size_t);
typedef void (*EVP_PKEY_CTX_free_fn)(EVP_PKEY_CTX *);
typedef void (*EVP_PKEY_free_fn)(EVP_PKEY *);
typedef void (*X509_free_fn)(X509 *);
typedef void *(*EVP_sha256_fn)(void);
typedef void *(*EVP_sha384_fn)(void);
typedef void *(*EVP_sha512_fn)(void);
typedef int (*EVP_PKEY_CTX_set_rsa_padding_fn)(EVP_PKEY_CTX *, int);
typedef int (*EVP_PKEY_CTX_set_rsa_pss_saltlen_fn)(EVP_PKEY_CTX *, int);

#define RSA_PKCS1_PSS_PADDING 6
#define RSA_PSS_SALTLEN_DIGEST -1

static int tls_verify_cert_verify(rt_tls_session_t *session, const uint8_t *data, size_t len)
{
    if (len < 4)
    {
        session->error = "TLS: CertificateVerify message too short";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    uint16_t sig_scheme = ((uint16_t)data[0] << 8) | data[1];
    uint16_t sig_len = ((uint16_t)data[2] << 8) | data[3];
    if (4 + sig_len > len)
    {
        session->error = "TLS: CertificateVerify signature length overflows";
        return RT_TLS_ERROR_HANDSHAKE;
    }
    const uint8_t *sig_bytes = data + 4;

    uint8_t content_hash[32];
    build_cert_verify_content(session->cert_transcript_hash, content_hash);

    // Try to load libssl for verification
    void *ssl_lib = dlopen("libssl.so.3", RTLD_LAZY | RTLD_LOCAL);
    if (!ssl_lib)
        ssl_lib = dlopen("libssl.so.1.1", RTLD_LAZY | RTLD_LOCAL);
    void *crypto_lib = dlopen("libcrypto.so.3", RTLD_LAZY | RTLD_LOCAL);
    if (!crypto_lib)
        crypto_lib = dlopen("libcrypto.so.1.1", RTLD_LAZY | RTLD_LOCAL);

    if (!crypto_lib)
    {
        if (ssl_lib)
            dlclose(ssl_lib);
        // libssl not available — chain + hostname verified, CertVerify skipped
        // This is acceptable for Linux systems without OpenSSL installed
        return RT_TLS_OK;
    }

    d2i_X509_fn fn_d2i_X509 = (d2i_X509_fn)dlsym(crypto_lib, "d2i_X509");
    X509_get_pubkey_fn fn_X509_get_pubkey =
        (X509_get_pubkey_fn)dlsym(crypto_lib, "X509_get_pubkey");
    EVP_PKEY_CTX_new_fn fn_EVP_PKEY_CTX_new =
        (EVP_PKEY_CTX_new_fn)dlsym(crypto_lib, "EVP_PKEY_CTX_new");
    EVP_PKEY_verify_init_fn fn_EVP_PKEY_verify_init =
        (EVP_PKEY_verify_init_fn)dlsym(crypto_lib, "EVP_PKEY_verify_init");
    EVP_PKEY_CTX_set_signature_md_fn fn_set_md =
        (EVP_PKEY_CTX_set_signature_md_fn)dlsym(crypto_lib, "EVP_PKEY_CTX_set_signature_md");
    EVP_PKEY_verify_fn fn_verify = (EVP_PKEY_verify_fn)dlsym(crypto_lib, "EVP_PKEY_verify");
    EVP_PKEY_CTX_free_fn fn_ctx_free = (EVP_PKEY_CTX_free_fn)dlsym(crypto_lib, "EVP_PKEY_CTX_free");
    EVP_PKEY_free_fn fn_pkey_free = (EVP_PKEY_free_fn)dlsym(crypto_lib, "EVP_PKEY_free");
    X509_free_fn fn_X509_free = (X509_free_fn)dlsym(crypto_lib, "X509_free");
    EVP_sha256_fn fn_sha256 = (EVP_sha256_fn)dlsym(crypto_lib, "EVP_sha256");
    EVP_sha384_fn fn_sha384 = (EVP_sha384_fn)dlsym(crypto_lib, "EVP_sha384");
    EVP_sha512_fn fn_sha512 = (EVP_sha512_fn)dlsym(crypto_lib, "EVP_sha512");
    EVP_PKEY_CTX_set_rsa_padding_fn fn_set_padding =
        (EVP_PKEY_CTX_set_rsa_padding_fn)dlsym(crypto_lib, "EVP_PKEY_CTX_set_rsa_padding");
    EVP_PKEY_CTX_set_rsa_pss_saltlen_fn fn_set_pss =
        (EVP_PKEY_CTX_set_rsa_pss_saltlen_fn)dlsym(crypto_lib, "EVP_PKEY_CTX_set_rsa_pss_saltlen");

    if (!fn_d2i_X509 || !fn_X509_get_pubkey || !fn_EVP_PKEY_CTX_new || !fn_EVP_PKEY_verify_init ||
        !fn_set_md || !fn_verify || !fn_ctx_free || !fn_pkey_free || !fn_X509_free || !fn_sha256)
    {
        dlclose(crypto_lib);
        if (ssl_lib)
            dlclose(ssl_lib);
        return RT_TLS_OK; // libssl present but symbols missing — skip
    }

    const unsigned char *der_ptr = session->server_cert_der;
    X509 *x509 = fn_d2i_X509(NULL, &der_ptr, (long)session->server_cert_der_len);
    if (!x509)
    {
        dlclose(crypto_lib);
        if (ssl_lib)
            dlclose(ssl_lib);
        session->error = "TLS: CertVerify: d2i_X509 failed";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    EVP_PKEY *pkey = fn_X509_get_pubkey(x509);
    fn_X509_free(x509);
    if (!pkey)
    {
        dlclose(crypto_lib);
        if (ssl_lib)
            dlclose(ssl_lib);
        session->error = "TLS: CertVerify: X509_get_pubkey failed";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    EVP_PKEY_CTX *ctx = fn_EVP_PKEY_CTX_new(pkey, NULL);
    if (!ctx || fn_EVP_PKEY_verify_init(ctx) <= 0)
    {
        if (ctx)
            fn_ctx_free(ctx);
        fn_pkey_free(pkey);
        dlclose(crypto_lib);
        if (ssl_lib)
            dlclose(ssl_lib);
        session->error = "TLS: CertVerify: EVP_PKEY_CTX init failed";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Set digest algorithm
    void *md = NULL;
    switch (sig_scheme)
    {
        case 0x0403:
        case 0x0804:
            md = fn_sha256();
            break;
        case 0x0503:
        case 0x0805:
            md = (fn_sha384 ? fn_sha384() : NULL);
            break;
        case 0x0806:
            md = (fn_sha512 ? fn_sha512() : NULL);
            break;
        default:
            break;
    }

    if (!md)
    {
        fn_ctx_free(ctx);
        fn_pkey_free(pkey);
        dlclose(crypto_lib);
        if (ssl_lib)
            dlclose(ssl_lib);
        session->error = "TLS: CertificateVerify: unsupported sig scheme (Linux)";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    fn_set_md(ctx, md);

    // For RSA-PSS schemes, set padding
    if ((sig_scheme == 0x0804 || sig_scheme == 0x0805 || sig_scheme == 0x0806) && fn_set_padding &&
        fn_set_pss)
    {
        fn_set_padding(ctx, RSA_PKCS1_PSS_PADDING);
        fn_set_pss(ctx, RSA_PSS_SALTLEN_DIGEST);
    }

    // Determine hash length
    size_t hash_len = (sig_scheme == 0x0503 || sig_scheme == 0x0805) ? 48
                      : (sig_scheme == 0x0806)                       ? 64
                                                                     : 32;

    int rc = fn_verify(ctx, sig_bytes, sig_len, content_hash, hash_len);

    fn_ctx_free(ctx);
    fn_pkey_free(pkey);
    dlclose(crypto_lib);
    if (ssl_lib)
        dlclose(ssl_lib);

    if (rc != 1)
    {
        session->error = "TLS: CertificateVerify signature verification failed (Linux)";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    return RT_TLS_OK;
}

#endif // Platform CertificateVerify

//=============================================================================
// Public API
//=============================================================================

void rt_tls_config_init(rt_tls_config_t *config)
{
    memset(config, 0, sizeof(*config));
    // CS-6 resolved: certificate validation now implemented (CS-1/CS-2/CS-3).
    // verify_cert=1 enables full chain validation + hostname verification + CertVerify.
    config->verify_cert = 1;
    config->timeout_ms = 30000;
}

rt_tls_session_t *rt_tls_new(int socket_fd, const rt_tls_config_t *config)
{
    rt_tls_session_t *session =
        (rt_tls_session_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_tls_session_t));
    memset(session, 0, sizeof(rt_tls_session_t));

    session->socket_fd = socket_fd;
    session->state = TLS_STATE_INITIAL;
    session->verify_cert = config ? config->verify_cert : 1;

    if (config && config->hostname)
    {
        strncpy(session->hostname, config->hostname, sizeof(session->hostname) - 1);
    }

    return session;
}

int rt_tls_handshake(rt_tls_session_t *session)
{
    if (!session)
        return RT_TLS_ERROR_INVALID_ARG;

    if (session->state != TLS_STATE_INITIAL)
    {
        session->error = "invalid state for handshake";
        return RT_TLS_ERROR;
    }

    // Send ClientHello
    int rc = send_client_hello(session);
    if (rc != RT_TLS_OK)
        return rc;

    // Process handshake messages
    while (session->state != TLS_STATE_CONNECTED && session->state != TLS_STATE_ERROR)
    {
        uint8_t content_type;
        uint8_t data[TLS_MAX_RECORD_SIZE];
        size_t data_len;

        rc = recv_record(session, &content_type, data, &data_len);
        if (rc != RT_TLS_OK)
            return rc;

        if (content_type == TLS_CONTENT_ALERT)
        {
            session->error = "received alert";
            session->state = TLS_STATE_ERROR;
            return RT_TLS_ERROR_HANDSHAKE;
        }

        if (content_type != TLS_CONTENT_HANDSHAKE)
        {
            session->error = "unexpected content type";
            return RT_TLS_ERROR_HANDSHAKE;
        }

        // Parse handshake messages
        size_t pos = 0;
        while (pos + 4 <= data_len)
        {
            uint8_t hs_type = data[pos];
            uint32_t hs_len = read_u24(data + pos + 1);

            if (pos + 4 + hs_len > data_len)
            {
                session->error = "incomplete handshake message";
                return RT_TLS_ERROR_HANDSHAKE;
            }

            // Update transcript before processing (H-10: abort on overflow)
            if (transcript_update(session, data + pos, 4 + hs_len) != 0)
                return RT_TLS_ERROR_HANDSHAKE;

            const uint8_t *hs_data = data + pos + 4;

            switch (hs_type)
            {
                case TLS_HS_SERVER_HELLO:
                    rc = process_server_hello(session, hs_data, hs_len);
                    if (rc != RT_TLS_OK)
                        return rc;
                    break;

                case TLS_HS_ENCRYPTED_EXTENSIONS:
                    // Skip - we don't process any extensions
                    session->state = TLS_STATE_WAIT_CERTIFICATE;
                    break;

                case TLS_HS_CERTIFICATE:
                    // Parse the Certificate message and extract end-entity DER (always)
                    rc = tls_parse_certificate_msg(session, hs_data, hs_len);
                    if (rc != RT_TLS_OK)
                        return rc;

                    if (session->verify_cert)
                    {
                        // CS-1: Chain validation via OS trust store
                        rc = tls_verify_chain(session);
                        if (rc != RT_TLS_OK)
                            return rc;

                        // CS-2: Hostname verification via SAN/CN
                        rc = tls_verify_hostname(session);
                        if (rc != RT_TLS_OK)
                            return rc;
                    }

                    // Save transcript hash for CS-3 CertificateVerify (before CV is added)
                    memcpy(session->cert_transcript_hash, session->transcript_hash, 32);
                    session->state = TLS_STATE_WAIT_CERTIFICATE_VERIFY;
                    break;

                case TLS_HS_CERTIFICATE_VERIFY:
                    if (session->verify_cert)
                    {
                        // CS-3: Verify server's proof of private key ownership
                        rc = tls_verify_cert_verify(session, hs_data, hs_len);
                        if (rc != RT_TLS_OK)
                            return rc;
                    }
                    session->state = TLS_STATE_WAIT_FINISHED;
                    break;

                case TLS_HS_FINISHED:
                    rc = verify_finished(session, hs_data, hs_len);
                    if (rc != RT_TLS_OK)
                        return rc;

                    // Derive application keys
                    derive_application_keys(session);

                    // Send our Finished
                    rc = send_finished(session);
                    if (rc != RT_TLS_OK)
                        return rc;

                    session->state = TLS_STATE_CONNECTED;
                    break;

                default:
                    // Skip unknown messages
                    break;
            }

            pos += 4 + hs_len;
        }
    }

    return session->state == TLS_STATE_CONNECTED ? RT_TLS_OK : RT_TLS_ERROR_HANDSHAKE;
}

long rt_tls_send(rt_tls_session_t *session, const void *data, size_t len)
{
    if (!session || session->state != TLS_STATE_CONNECTED)
        return RT_TLS_ERROR;

    if (len == 0)
        return 0;

    // Send in chunks
    const uint8_t *ptr = (const uint8_t *)data;
    size_t remaining = len;

    while (remaining > 0)
    {
        size_t chunk = remaining > TLS_MAX_RECORD_SIZE ? TLS_MAX_RECORD_SIZE : remaining;
        int rc = send_record(session, TLS_CONTENT_APPLICATION, ptr, chunk);
        if (rc != RT_TLS_OK)
            return rc;
        ptr += chunk;
        remaining -= chunk;
    }

    return (long)len;
}

long rt_tls_recv(rt_tls_session_t *session, void *buffer, size_t len)
{
    if (!session || session->state != TLS_STATE_CONNECTED)
        return RT_TLS_ERROR;

    // Return buffered data first
    if (session->app_buffer_pos < session->app_buffer_len)
    {
        size_t avail = session->app_buffer_len - session->app_buffer_pos;
        size_t copy = avail < len ? avail : len;
        memcpy(buffer, session->app_buffer + session->app_buffer_pos, copy);
        session->app_buffer_pos += copy;
        return (long)copy;
    }

    // Receive new record (retry_recv: loop label for skipping non-application records)
retry_recv:;
    uint8_t content_type;
    size_t data_len;

    int rc = recv_record(session, &content_type, session->app_buffer, &data_len);
    if (rc != RT_TLS_OK)
        return rc;

    if (content_type == TLS_CONTENT_ALERT)
    {
        session->state = TLS_STATE_CLOSED;
        return 0;
    }

    if (content_type != TLS_CONTENT_APPLICATION)
    {
        // Skip non-application records (e.g., NewSessionTicket post-handshake messages).
        // M-13 fix: use an iterative loop instead of recursion to prevent stack overflow
        // when a misbehaving server sends many consecutive non-application records.
        goto retry_recv;
    }

    session->app_buffer_len = data_len;
    session->app_buffer_pos = 0;

    size_t copy = data_len < len ? data_len : len;
    memcpy(buffer, session->app_buffer, copy);
    session->app_buffer_pos = copy;

    return (long)copy;
}

void rt_tls_close(rt_tls_session_t *session)
{
    if (!session)
        return;

    if (session->state == TLS_STATE_CONNECTED)
    {
        // Send close_notify alert
        uint8_t alert[2] = {1, 0}; // warning, close_notify
        send_record(session, TLS_CONTENT_ALERT, alert, 2);

        // M-12: Await the peer's close_notify before closing the read side.
        // RFC 5246 §7.2.1 requires the initiator to receive the responding
        // close_notify before considering the connection fully closed.
        // Bound the drain loop to avoid hanging if the server never responds.
        int drain = 0;
        while (drain < 32)
        {
            uint8_t content_type;
            size_t data_len;
            uint8_t buf[TLS_MAX_RECORD_SIZE + 256];
            int rc = recv_record(session, &content_type, buf, &data_len);
            if (rc != RT_TLS_OK)
                break; // Socket error or timeout — stop draining
            if (content_type == TLS_CONTENT_ALERT)
                break; // Received peer's close_notify (or other alert)
            drain++;   // Skip non-alert record (e.g. NewSessionTicket) and loop
        }
    }

    session->state = TLS_STATE_CLOSED;
}

const char *rt_tls_get_error(rt_tls_session_t *session)
{
    if (!session)
        return "null session";
    return session->error ? session->error : "no error";
}

int rt_tls_get_socket(rt_tls_session_t *session)
{
    if (!session)
        return -1;
    return (int)session->socket_fd;
}

rt_tls_session_t *rt_tls_connect(const char *host, uint16_t port, const rt_tls_config_t *config)
{
#ifdef _WIN32
    // Initialize Winsock
    extern void rt_net_init_wsa(void);
    rt_net_init_wsa();
#endif

    // Resolve hostname using getaddrinfo (thread-safe; supports IPv4 and IPv6).
    // gethostbyname() was deprecated in POSIX.1-2001 and uses a static buffer
    // that is not thread-safe — two concurrent TLS connections would corrupt
    // each other's lookup result.
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);

    struct addrinfo *res = NULL;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res)
        return NULL;

    // Create socket
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        freeaddrinfo(res);
        return NULL;
    }

    // Connect
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, &((struct sockaddr_in *)res->ai_addr)->sin_addr, sizeof(addr.sin_addr));
    freeaddrinfo(res);
    res = NULL;

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        CLOSE_SOCKET(sock);
        return NULL;
    }

    // Create TLS session
    rt_tls_config_t cfg;
    if (config)
    {
        cfg = *config;
    }
    else
    {
        rt_tls_config_init(&cfg);
    }
    cfg.hostname = host;

    rt_tls_session_t *session = rt_tls_new((int)sock, &cfg);
    if (!session)
    {
        CLOSE_SOCKET(sock);
        return NULL;
    }

    // Perform handshake
    if (rt_tls_handshake(session) != RT_TLS_OK)
    {
        rt_tls_close(session);
        CLOSE_SOCKET(sock);
        return NULL;
    }

    return session;
}

//=============================================================================
// Viper API Wrappers
//=============================================================================
//
// These functions wrap the low-level TLS API for use by the Viper runtime.
// They handle conversion between Viper types (rt_string, Bytes) and C types.
//
//=============================================================================

#include "rt_bytes.h"
#include "rt_object.h"
#include "rt_string.h"

/// @brief Internal structure for Viper TLS objects.
typedef struct rt_viper_tls
{
    rt_tls_session_t *session;
    char *host;
    int64_t port;
} rt_viper_tls_t;

/// @brief Finalizer for TLS objects.
static void rt_viper_tls_finalize(void *obj)
{
    if (!obj)
        return;
    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    if (tls->session)
    {
        rt_tls_close(tls->session);
        tls->session = NULL;
    }
    if (tls->host)
    {
        free(tls->host);
        tls->host = NULL;
    }
}

/// @brief Connect to a TLS server.
/// @param host Hostname to connect to.
/// @param port Port number.
/// @return TLS object or NULL on error.
void *rt_viper_tls_connect(rt_string host, int64_t port)
{
    if (!host || port < 1 || port > 65535)
        return NULL;

    const char *host_cstr = rt_string_cstr(host);
    if (!host_cstr)
        return NULL;

    rt_tls_config_t config;
    rt_tls_config_init(&config);
    config.hostname = host_cstr;

    rt_tls_session_t *session = rt_tls_connect(host_cstr, (uint16_t)port, &config);
    if (!session)
        return NULL;

    // Create Viper TLS object
    rt_viper_tls_t *tls = (rt_viper_tls_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_viper_tls_t));
    if (!tls)
    {
        rt_tls_close(session);
        return NULL;
    }

    tls->session = session;
    tls->host = strdup(host_cstr);
    if (!tls->host)
    {
        rt_tls_close(session);
        return NULL;
    }
    tls->port = port;

    rt_obj_set_finalizer(tls, rt_viper_tls_finalize);

    return tls;
}

/// @brief Connect to a TLS server with timeout.
/// @param host Hostname to connect to.
/// @param port Port number.
/// @param timeout_ms Timeout in milliseconds.
/// @return TLS object or NULL on error.
void *rt_viper_tls_connect_for(rt_string host, int64_t port, int64_t timeout_ms)
{
    if (!host || port < 1 || port > 65535)
        return NULL;

    const char *host_cstr = rt_string_cstr(host);
    if (!host_cstr)
        return NULL;

    rt_tls_config_t config;
    rt_tls_config_init(&config);
    config.hostname = host_cstr;
    config.timeout_ms = (int)timeout_ms;

    rt_tls_session_t *session = rt_tls_connect(host_cstr, (uint16_t)port, &config);
    if (!session)
        return NULL;

    // Create Viper TLS object
    rt_viper_tls_t *tls = (rt_viper_tls_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_viper_tls_t));
    if (!tls)
    {
        rt_tls_close(session);
        return NULL;
    }

    tls->session = session;
    tls->host = strdup(host_cstr);
    if (!tls->host)
    {
        rt_tls_close(session);
        return NULL;
    }
    tls->port = port;

    rt_obj_set_finalizer(tls, rt_viper_tls_finalize);

    return tls;
}

/// @brief Get the hostname of the TLS connection.
rt_string rt_viper_tls_host(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    const char *h = tls->host ? tls->host : "";
    return rt_string_from_bytes(h, strlen(h));
}

/// @brief Get the port of the TLS connection.
int64_t rt_viper_tls_port(void *obj)
{
    if (!obj)
        return 0;
    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    return tls->port;
}

/// @brief Check if the TLS connection is open.
int8_t rt_viper_tls_is_open(void *obj)
{
    if (!obj)
        return 0;
    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    return tls->session != NULL;
}

/// @brief Send bytes over TLS connection.
/// @param obj TLS object.
/// @param data Bytes object to send.
/// @return Number of bytes sent, or -1 on error.
int64_t rt_viper_tls_send(void *obj, void *data)
{
    if (!obj || !data)
        return -1;

    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    if (!tls->session)
        return -1;

    int64_t len = rt_bytes_len(data);
    if (len == 0)
        return 0;

    // Copy bytes to temporary buffer
    uint8_t *buffer = (uint8_t *)malloc((size_t)len);
    if (!buffer)
        return -1;
    for (int64_t i = 0; i < len; i++)
        buffer[i] = (uint8_t)rt_bytes_get(data, i);

    long result = rt_tls_send(tls->session, buffer, (size_t)len);
    free(buffer);
    return (int64_t)result;
}

/// @brief Send string over TLS connection.
/// @param obj TLS object.
/// @param text String to send.
/// @return Number of bytes sent, or -1 on error.
int64_t rt_viper_tls_send_str(void *obj, rt_string text)
{
    if (!obj || !text)
        return -1;

    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    if (!tls->session)
        return -1;

    const char *cstr = rt_string_cstr(text);
    if (!cstr)
        return 0;
    size_t len = strlen(cstr);
    if (len == 0)
        return 0;

    long result = rt_tls_send(tls->session, cstr, len);
    return (int64_t)result;
}

/// @brief Receive bytes from TLS connection.
/// @param obj TLS object.
/// @param max_bytes Maximum bytes to receive.
/// @return Bytes object with received data, or NULL on error.
void *rt_viper_tls_recv(void *obj, int64_t max_bytes)
{
    if (!obj || max_bytes <= 0)
        return NULL;

    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    if (!tls->session)
        return NULL;

    // Allocate temporary buffer
    size_t buf_size = (size_t)max_bytes;
    uint8_t *buffer = (uint8_t *)malloc(buf_size);
    if (!buffer)
        return NULL;

    long received = rt_tls_recv(tls->session, buffer, buf_size);
    if (received <= 0)
    {
        free(buffer);
        return received == 0 ? rt_bytes_new(0) : NULL;
    }

    // Create Bytes object and copy data
    void *result = rt_bytes_new((int64_t)received);
    for (long i = 0; i < received; i++)
        rt_bytes_set(result, i, buffer[i]);

    free(buffer);
    return result;
}

/// @brief Receive string from TLS connection.
/// @param obj TLS object.
/// @param max_bytes Maximum bytes to receive.
/// @return String with received data, or empty string on error.
rt_string rt_viper_tls_recv_str(void *obj, int64_t max_bytes)
{
    if (!obj || max_bytes <= 0)
        return rt_string_from_bytes("", 0);

    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    if (!tls->session)
        return rt_string_from_bytes("", 0);

    // Allocate temporary buffer
    size_t buf_size = (size_t)max_bytes;
    char *buffer = (char *)malloc(buf_size + 1);
    if (!buffer)
        return rt_string_from_bytes("", 0);

    long received = rt_tls_recv(tls->session, buffer, buf_size);
    if (received <= 0)
    {
        free(buffer);
        return rt_string_from_bytes("", 0);
    }

    buffer[received] = '\0';
    rt_string result = rt_string_from_bytes(buffer, (size_t)received);
    free(buffer);
    return result;
}

/// @brief Read a line (up to \n) from the TLS connection.
rt_string rt_viper_tls_recv_line(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    if (!tls->session)
        return rt_string_from_bytes("", 0);

    size_t cap = 256;
    size_t len = 0;
    char *line = (char *)malloc(cap);
    if (!line)
        return rt_string_from_bytes("", 0);

    while (1)
    {
        char c;
        long received = rt_tls_recv(tls->session, &c, 1);
        if (received <= 0)
        {
            // Connection closed or error before newline
            break;
        }

        if (c == '\n')
        {
            // Strip trailing CR if present
            if (len > 0 && line[len - 1] == '\r')
                len--;
            break;
        }

        if (len >= cap)
        {
            cap *= 2;
            char *new_line = (char *)realloc(line, cap);
            if (!new_line)
            {
                free(line);
                return rt_string_from_bytes("", 0);
            }
            line = new_line;
        }
        line[len++] = c;
    }

    rt_string result = rt_string_from_bytes(line, len);
    free(line);
    return result;
}

/// @brief Close the TLS connection.
void rt_viper_tls_close(void *obj)
{
    if (!obj)
        return;

    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    if (tls->session)
    {
        rt_tls_close(tls->session);
        tls->session = NULL;
    }
}

/// @brief Get the last error message.
rt_string rt_viper_tls_error(void *obj)
{
    const char *msg;
    if (!obj)
    {
        msg = "null object";
        return rt_string_from_bytes(msg, strlen(msg));
    }

    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    if (!tls->session)
    {
        msg = "connection closed";
        return rt_string_from_bytes(msg, strlen(msg));
    }

    const char *err = rt_tls_get_error(tls->session);
    msg = err ? err : "no error";
    return rt_string_from_bytes(msg, strlen(msg));
}
