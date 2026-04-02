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

#include "rt_crypto.h"
#include "rt_object.h"
#include "rt_tls_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_t;
#define CLOSE_SOCKET(s) close(s)
#define SOCKET_ERRNO errno
#define EINTR_CHECK (errno == EINTR)
#define EAGAIN_CHECK (errno == EAGAIN || errno == EWOULDBLOCK)
#endif

#ifdef _WIN32
// Forward declaration (must be at file scope for MSVC)
extern void rt_net_init_wsa(void);
#endif

// SIGPIPE suppression.
#if defined(__linux__) || defined(__viperdos__)
#define SEND_FLAGS MSG_NOSIGNAL
#else
#define SEND_FLAGS 0
#endif

static void suppress_sigpipe(socket_t sock) {
#if defined(__APPLE__) && defined(SO_NOSIGPIPE)
    int val = 1;
    setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val));
#endif
    (void)sock;
}

static int tls_wait_socket(socket_t sock, int timeout_ms, int for_write) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET((unsigned)sock, &fds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (for_write)
        return select((int)sock + 1, NULL, &fds, NULL, &tv);
    return select((int)sock + 1, &fds, NULL, NULL, &tv);
}

static void tls_set_nonblocking(socket_t sock, int nonblocking) {
#ifdef _WIN32
    u_long mode = nonblocking ? 1 : 0;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0)
        return;
    if (nonblocking)
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    else
        fcntl(sock, F_SETFL, flags & ~O_NONBLOCK);
#endif
}

static void tls_set_socket_timeout(socket_t sock, int timeout_ms, int is_recv) {
#ifdef _WIN32
    DWORD tv = (DWORD)timeout_ms;
    setsockopt(sock, SOL_SOCKET, is_recv ? SO_RCVTIMEO : SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, is_recv ? SO_RCVTIMEO : SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

static void tls_release_dynamic_state(rt_tls_session_t *session) {
    if (!session)
        return;

    if (session->server_cert_list) {
        free(session->server_cert_list);
        session->server_cert_list = NULL;
        session->server_cert_list_len = 0;
        session->server_cert_count = 0;
    }
    if (session->hello_retry_cookie) {
        free(session->hello_retry_cookie);
        session->hello_retry_cookie = NULL;
        session->hello_retry_cookie_len = 0;
    }
    session->app_buffer_len = 0;
    session->app_buffer_pos = 0;
}

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

// Cipher suites
#define TLS_AES_128_GCM_SHA256 0x1301
#define TLS_CHACHA20_POLY1305_SHA256 0x1303

// Extensions
#define TLS_EXT_SERVER_NAME 0
#define TLS_EXT_SUPPORTED_GROUPS 10
#define TLS_EXT_SIGNATURE_ALGORITHMS 13
#define TLS_EXT_COOKIE 44
#define TLS_EXT_SUPPORTED_VERSIONS 43
#define TLS_EXT_KEY_SHARE 51

static const uint8_t TLS_HELLO_RETRY_RANDOM[32] = {0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A, 0x61, 0x11,
                                                    0xBE, 0x1D, 0x8C, 0x02, 0x1E, 0x65, 0xB8, 0x91,
                                                    0xC2, 0xA2, 0x11, 0x16, 0x7A, 0xBB, 0x8C, 0x5E,
                                                    0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C};

// Note: TLS_MAX_RECORD_SIZE, TLS_MAX_CIPHERTEXT, tls_state_t, traffic_keys_t,
// and struct rt_tls_session are defined in rt_tls_internal.h.

// Helper: write big-endian uint16
static void write_u16(uint8_t *p, uint16_t v) {
    p[0] = (v >> 8) & 0xFF;
    p[1] = v & 0xFF;
}

// Helper: write big-endian uint24
static void write_u24(uint8_t *p, uint32_t v) {
    p[0] = (v >> 16) & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = v & 0xFF;
}

// Helper: read big-endian uint16
static uint16_t read_u16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

// Helper: read big-endian uint24
static uint32_t read_u24(const uint8_t *p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

// Update transcript hash.
// Returns 0 on success, -1 on size overflow.
static void transcript_init(rt_tls_session_t *session) {
    session->transcript_len = 0;
    rt_sha256_init(&session->transcript_ctx);
    rt_sha256(NULL, 0, session->transcript_hash);
}

static int transcript_update(rt_tls_session_t *session, const uint8_t *data, size_t len) {
    if (session->error)
        return -1; // Already in error state

    if (len > SIZE_MAX - session->transcript_len) {
        session->error = "TLS: handshake transcript length overflow";
        return -1;
    }

    session->transcript_len += len;
    rt_sha256_update(&session->transcript_ctx, data, len);
    rt_sha256_ctx hash_snapshot = session->transcript_ctx;
    rt_sha256_final(&hash_snapshot, session->transcript_hash);
    return 0;
}

// Derive handshake traffic keys
static void derive_handshake_keys(rt_tls_session_t *session, const uint8_t shared_secret[32]) {
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

    // Derive keys (AES-128-GCM uses 16-byte keys; ChaCha20 uses 32-byte keys)
    int key_len = (session->cipher_suite == TLS_AES_128_GCM_SHA256) ? 16 : 32;
    rt_hkdf_expand_label(
        session->server_handshake_traffic_secret, "key", NULL, 0, session->read_keys.key, key_len);
    rt_hkdf_expand_label(
        session->server_handshake_traffic_secret, "iv", NULL, 0, session->read_keys.iv, 12);
    session->read_keys.seq_num = 0;

    rt_hkdf_expand_label(
        session->client_handshake_traffic_secret, "key", NULL, 0, session->write_keys.key, key_len);
    rt_hkdf_expand_label(
        session->client_handshake_traffic_secret, "iv", NULL, 0, session->write_keys.iv, 12);
    session->write_keys.seq_num = 0;

    session->keys_established = 1;
}

// Derive application traffic keys
static void derive_application_keys(rt_tls_session_t *session) {
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

    // Derive application keys (AES-128-GCM uses 16-byte keys; ChaCha20 uses 32-byte keys)
    int app_key_len = (session->cipher_suite == TLS_AES_128_GCM_SHA256) ? 16 : 32;
    rt_hkdf_expand_label(session->server_application_traffic_secret,
                         "key",
                         NULL,
                         0,
                         session->read_keys.key,
                         app_key_len);
    rt_hkdf_expand_label(
        session->server_application_traffic_secret, "iv", NULL, 0, session->read_keys.iv, 12);
    session->read_keys.seq_num = 0;

    rt_hkdf_expand_label(session->client_application_traffic_secret,
                         "key",
                         NULL,
                         0,
                         session->write_keys.key,
                         app_key_len);
    rt_hkdf_expand_label(
        session->client_application_traffic_secret, "iv", NULL, 0, session->write_keys.iv, 12);
    session->write_keys.seq_num = 0;
}

// Build nonce from IV and sequence number
static void build_nonce(const uint8_t iv[12], uint64_t seq, uint8_t nonce[12]) {
    memcpy(nonce, iv, 12);
    for (int i = 0; i < 8; i++) {
        nonce[12 - 1 - i] ^= (seq >> (i * 8)) & 0xFF;
    }
}

// Send TLS record
static int send_record(rt_tls_session_t *session,
                       uint8_t content_type,
                       const uint8_t *data,
                       size_t len) {
    // ~32KB stack allocation (TLS_MAX_CIPHERTEXT). Consider heap allocation
    // if stack depth is a concern in deeply nested call chains.
    uint8_t record[5 + TLS_MAX_CIPHERTEXT];
    size_t record_len;

    if (session->keys_established) {
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
        size_t ciphertext_len;
        if (session->cipher_suite == TLS_AES_128_GCM_SHA256)
            ciphertext_len = rt_aes128_gcm_encrypt(
                session->write_keys.key, nonce, aad, 5, plaintext, len + 1, record + 5);
        else
            ciphertext_len = rt_chacha20_poly1305_encrypt(
                session->write_keys.key, nonce, aad, 5, plaintext, len + 1, record + 5);
        if (ciphertext_len == 0 && (len + 1) != 0) {
            session->error = "TLS: record encryption failed";
            return RT_TLS_ERROR;
        }
        write_u16(record + 3, (uint16_t)ciphertext_len);
        record_len = 5 + ciphertext_len;

        // H-8: RFC 8446 §5.5 — close before sequence number wraps (nonce uniqueness)
        if (++session->write_keys.seq_num == 0) {
            session->error =
                "TLS: write sequence number overflow; connection must be re-established";
            return RT_TLS_ERROR;
        }
    } else {
        // Plaintext record
        record[0] = content_type;
        write_u16(record + 1, TLS_VERSION_1_2);
        write_u16(record + 3, (uint16_t)len);
        memcpy(record + 5, data, len);
        record_len = 5 + len;
    }

    size_t sent = 0;
    while (sent < record_len) {
        int n = send(session->socket_fd,
                     (const char *)(record + sent),
                     (int)(record_len - sent),
                     SEND_FLAGS);
        if (n < 0) {
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
                       size_t *data_len) {
    // Read header
    uint8_t header[5];
    size_t pos = 0;
    while (pos < 5) {
        int n = recv(session->socket_fd, (char *)(header + pos), (int)(5 - pos), 0);
        if (n < 0) {
            if (EINTR_CHECK || EAGAIN_CHECK)
                continue;
            session->error = "recv header failed";
            return RT_TLS_ERROR_SOCKET;
        }
        if (n == 0) {
            session->error = "connection closed";
            return RT_TLS_ERROR_CLOSED;
        }
        pos += n;
    }

    uint8_t type = header[0];
    size_t length = read_u16(header + 3);

    if (length > TLS_MAX_CIPHERTEXT) {
        session->error = "record too large";
        return RT_TLS_ERROR;
    }

    // Read payload
    uint8_t payload[TLS_MAX_CIPHERTEXT];
    pos = 0;
    while (pos < length) {
        int n = recv(session->socket_fd, (char *)(payload + pos), (int)(length - pos), 0);
        if (n < 0) {
            if (EINTR_CHECK || EAGAIN_CHECK)
                continue;
            session->error = "recv payload failed";
            return RT_TLS_ERROR_SOCKET;
        }
        if (n == 0) {
            session->error = "connection closed";
            return RT_TLS_ERROR_CLOSED;
        }
        pos += n;
    }

    if (session->keys_established && type == TLS_CONTENT_APPLICATION) {
        // Decrypt
        uint8_t aad[5];
        memcpy(aad, header, 5);

        uint8_t nonce[12];
        build_nonce(session->read_keys.iv, session->read_keys.seq_num, nonce);

        long plaintext_len;
        if (session->cipher_suite == TLS_AES_128_GCM_SHA256)
            plaintext_len =
                rt_aes128_gcm_decrypt(session->read_keys.key, nonce, aad, 5, payload, length, data);
        else
            plaintext_len = rt_chacha20_poly1305_decrypt(
                session->read_keys.key, nonce, aad, 5, payload, length, data);
        if (plaintext_len < 0) {
            session->error = "decryption failed";
            return RT_TLS_ERROR;
        }

        // H-8: RFC 8446 §5.5 — close before sequence number wraps (nonce uniqueness)
        if (++session->read_keys.seq_num == 0) {
            session->error =
                "TLS: read sequence number overflow; connection must be re-established";
            return RT_TLS_ERROR;
        }

        // Remove padding and get inner content type
        while (plaintext_len > 0 && data[plaintext_len - 1] == 0)
            plaintext_len--;
        if (plaintext_len == 0) {
            session->error = "empty inner record";
            return RT_TLS_ERROR;
        }
        *content_type = data[plaintext_len - 1];
        *data_len = plaintext_len - 1;
    } else {
        // Plaintext record
        *content_type = type;
        memcpy(data, payload, length);
        *data_len = length;
    }

    return RT_TLS_OK;
}

// Build and send ClientHello
static int send_client_hello(rt_tls_session_t *session) {
    uint8_t msg[1400];
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

    // Cipher suites (RFC 8446 §9.1: AES-128-GCM-SHA256 is mandatory)
    write_u16(msg + pos, 4); // 2 suites × 2 bytes
    pos += 2;
    write_u16(msg + pos, TLS_AES_128_GCM_SHA256);
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
    if (session->hostname[0] != '\0') {
        size_t name_len = strlen(session->hostname);
        if (pos + name_len + 11 > sizeof(msg) - 64) {
            session->error = "ClientHello: hostname too long";
            return RT_TLS_ERROR;
        }
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

    // Supported groups
    write_u16(msg + pos, TLS_EXT_SUPPORTED_GROUPS);
    pos += 2;
    write_u16(msg + pos, 4);
    pos += 2;
    write_u16(msg + pos, 2);
    pos += 2;
    write_u16(msg + pos, 0x001D); // x25519
    pos += 2;

    // Signature algorithms (ECDSA + RSA-PSS)
    write_u16(msg + pos, TLS_EXT_SIGNATURE_ALGORITHMS);
    pos += 2;
    write_u16(msg + pos, 12);
    pos += 2;
    write_u16(msg + pos, 10);
    pos += 2;
    write_u16(msg + pos, 0x0403); // ecdsa_secp256r1_sha256
    pos += 2;
    write_u16(msg + pos, 0x0503); // ecdsa_secp384r1_sha384
    pos += 2;
    write_u16(msg + pos, 0x0804); // rsa_pss_rsae_sha256
    pos += 2;
    write_u16(msg + pos, 0x0805); // rsa_pss_rsae_sha384
    pos += 2;
    write_u16(msg + pos, 0x0806); // rsa_pss_rsae_sha512
    pos += 2;

    // HelloRetryRequest cookie, if any
    if (session->hello_retry_cookie && session->hello_retry_cookie_len > 0) {
        if (session->hello_retry_cookie_len > 0xFFFF - 2 || pos + 6 + session->hello_retry_cookie_len > sizeof(msg)) {
            session->error = "ClientHello: retry cookie too large";
            return RT_TLS_ERROR;
        }
        write_u16(msg + pos, TLS_EXT_COOKIE);
        pos += 2;
        write_u16(msg + pos, (uint16_t)(2 + session->hello_retry_cookie_len));
        pos += 2;
        write_u16(msg + pos, (uint16_t)session->hello_retry_cookie_len);
        pos += 2;
        memcpy(msg + pos, session->hello_retry_cookie, session->hello_retry_cookie_len);
        pos += session->hello_retry_cookie_len;
    }

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
    uint8_t hs[4 + 1400];
    hs[0] = TLS_HS_CLIENT_HELLO;
    write_u24(hs + 1, (uint32_t)pos);
    memcpy(hs + 4, msg, pos);

    // Update transcript
    if (transcript_update(session, hs, 4 + pos) != 0)
        return RT_TLS_ERROR_HANDSHAKE;
    if (!session->have_client_hello_hash) {
        memcpy(session->client_hello_hash, session->transcript_hash, 32);
        session->have_client_hello_hash = 1;
    }

    // Send
    int rc = send_record(session, TLS_CONTENT_HANDSHAKE, hs, 4 + pos);
    if (rc != RT_TLS_OK)
        return rc;

    session->state = TLS_STATE_CLIENT_HELLO_SENT;
    return RT_TLS_OK;
}

// Process ServerHello or HelloRetryRequest.
static int process_server_hello(rt_tls_session_t *session,
                                const uint8_t *data,
                                size_t len,
                                const uint8_t *full_msg,
                                size_t full_len) {
    if (len < 38) {
        session->error = "ServerHello too short";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Skip version (2)
    memcpy(session->server_random, data + 2, 32);
    int is_hrr = memcmp(session->server_random, TLS_HELLO_RETRY_RANDOM, sizeof(TLS_HELLO_RETRY_RANDOM)) == 0;

    size_t pos = 34;

    // Session ID — bounds-check before advancing (S-02 fix)
    if (pos >= len) {
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

    if (session->cipher_suite != TLS_AES_128_GCM_SHA256 &&
        session->cipher_suite != TLS_CHACHA20_POLY1305_SHA256) {
        session->error = "unsupported cipher suite";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Skip compression
    pos++;

    // Parse extensions
    if (pos + 2 > len) {
        session->error = "no extensions";
        return RT_TLS_ERROR_HANDSHAKE;
    }
    uint16_t ext_len = read_u16(data + pos);
    pos += 2;

    size_t ext_end = pos + ext_len;
    uint16_t selected_group = 0;
    int found_key_share = 0;
    int found_cookie = 0;
    int found_supported_versions = 0; // M-11: track TLS 1.3 version confirmation

    while (pos + 4 <= ext_end) {
        uint16_t ext_type = read_u16(data + pos);
        uint16_t ext_data_len = read_u16(data + pos + 2);
        pos += 4;
        if (pos + ext_data_len > ext_end) {
            session->error = "ServerHello extension overflows message";
            return RT_TLS_ERROR_HANDSHAKE;
        }

        if (ext_type == TLS_EXT_KEY_SHARE) {
            if (is_hrr && ext_data_len == 2) {
                selected_group = read_u16(data + pos);
                found_key_share = 1;
            } else if (!is_hrr && ext_data_len >= 36) {
                uint16_t group = read_u16(data + pos);
                uint16_t key_len = read_u16(data + pos + 2);
                if (group == 0x001D && key_len == 32) {
                    memcpy(session->server_public_key, data + pos + 4, 32);
                    selected_group = group;
                    found_key_share = 1;
                }
            }
        } else if (ext_type == TLS_EXT_COOKIE && is_hrr && ext_data_len >= 2) {
            uint16_t cookie_len = read_u16(data + pos);
            if ((size_t)cookie_len + 2 != ext_data_len) {
                session->error = "HelloRetryRequest cookie length mismatch";
                return RT_TLS_ERROR_HANDSHAKE;
            }
            uint8_t *cookie = (uint8_t *)malloc(cookie_len);
            if (!cookie) {
                session->error = "HelloRetryRequest cookie allocation failed";
                return RT_TLS_ERROR_MEMORY;
            }
            memcpy(cookie, data + pos + 2, cookie_len);
            if (session->hello_retry_cookie) {
                free(session->hello_retry_cookie);
                session->hello_retry_cookie = NULL;
                session->hello_retry_cookie_len = 0;
            }
            session->hello_retry_cookie = cookie;
            session->hello_retry_cookie_len = cookie_len;
            found_cookie = 1;
        }
        // M-11: RFC 8446 §4.2.1 — ServerHello must confirm TLS 1.3 via supported_versions
        else if (ext_type == TLS_EXT_SUPPORTED_VERSIONS && ext_data_len == 2) {
            uint16_t negotiated = read_u16(data + pos);
            if (negotiated == TLS_VERSION_1_3)
                found_supported_versions = 1;
        }
        pos += ext_data_len;
    }

    if (!found_key_share) {
        session->error = "no key share";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    if (is_hrr) {
        if (session->hello_retry_seen) {
            session->error = "TLS: multiple HelloRetryRequest messages are not supported";
            return RT_TLS_ERROR_HANDSHAKE;
        }
        if (selected_group != 0x001D) {
            session->error = "TLS: HelloRetryRequest selected an unsupported key share group";
            return RT_TLS_ERROR_HANDSHAKE;
        }
        if (!session->have_client_hello_hash) {
            session->error = "TLS: missing initial ClientHello transcript hash for HelloRetryRequest";
            return RT_TLS_ERROR_HANDSHAKE;
        }

        uint8_t message_hash[4 + 32];
        message_hash[0] = 0xFE; // message_hash synthetic handshake message
        write_u24(message_hash + 1, 32);
        memcpy(message_hash + 4, session->client_hello_hash, 32);

        transcript_init(session);
        if (transcript_update(session, message_hash, sizeof(message_hash)) != 0 ||
            transcript_update(session, full_msg, full_len) != 0) {
            return RT_TLS_ERROR_HANDSHAKE;
        }

        session->hello_retry_seen = 1;
        (void)found_cookie;
        return send_client_hello(session);
    }

    // M-11: Reject the handshake if the server didn't confirm TLS 1.3.
    // A middlebox performing a version downgrade attack would omit this extension.
    if (!found_supported_versions) {
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
static int send_finished(rt_tls_session_t *session) {
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
static int ct_memcmp(const uint8_t *a, const uint8_t *b, size_t n) {
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++)
        diff |= a[i] ^ b[i];
    return diff != 0; // non-zero means unequal
}

// Verify server Finished
static int verify_finished(rt_tls_session_t *session, const uint8_t *data, size_t len) {
    if (len != 32) {
        session->error = "invalid Finished length";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    uint8_t finished_key[32];
    rt_hkdf_expand_label(
        session->server_handshake_traffic_secret, "finished", NULL, 0, finished_key, 32);

    uint8_t expected[32];
    rt_hmac_sha256(finished_key, 32, session->transcript_hash, 32, expected);

    if (ct_memcmp(data, expected, 32)) {
        session->error = "Finished verification failed";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    return RT_TLS_OK;
}

// Certificate validation functions (tls_parse_certificate_msg, tls_verify_hostname,
// tls_verify_chain, tls_verify_cert_verify) are implemented in rt_tls_verify.c.

//=============================================================================
// Public API
//=============================================================================

void rt_tls_config_init(rt_tls_config_t *config) {
    memset(config, 0, sizeof(*config));
    // CS-6 resolved: certificate validation now implemented (CS-1/CS-2/CS-3).
    // verify_cert=1 enables full chain validation + hostname verification + CertVerify.
    config->verify_cert = 1;
    config->timeout_ms = 30000;
}

rt_tls_session_t *rt_tls_new(int socket_fd, const rt_tls_config_t *config) {
    rt_tls_session_t *session =
        (rt_tls_session_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_tls_session_t));
    memset(session, 0, sizeof(rt_tls_session_t));

    session->socket_fd = socket_fd;
    session->state = TLS_STATE_INITIAL;
    session->verify_cert = config ? config->verify_cert : 1;
    session->timeout_ms = (config && config->timeout_ms > 0) ? config->timeout_ms : 30000;
    transcript_init(session);

    if (config && config->hostname) {
        strncpy(session->hostname, config->hostname, sizeof(session->hostname) - 1);
    }

    return session;
}

int rt_tls_handshake(rt_tls_session_t *session) {
    if (!session)
        return RT_TLS_ERROR_INVALID_ARG;

    if (session->state != TLS_STATE_INITIAL) {
        session->error = "invalid state for handshake";
        return RT_TLS_ERROR;
    }

    // Send ClientHello
    int rc = send_client_hello(session);
    if (rc != RT_TLS_OK)
        return rc;

    // Process handshake messages
    while (session->state != TLS_STATE_CONNECTED && session->state != TLS_STATE_ERROR) {
        uint8_t content_type;
        uint8_t data[TLS_MAX_RECORD_SIZE];
        size_t data_len;

        rc = recv_record(session, &content_type, data, &data_len);
        if (rc != RT_TLS_OK)
            return rc;

        if (content_type == TLS_CONTENT_ALERT) {
            session->error = "received alert";
            session->state = TLS_STATE_ERROR;
            return RT_TLS_ERROR_HANDSHAKE;
        }

        if (content_type != TLS_CONTENT_HANDSHAKE) {
            session->error = "unexpected content type";
            return RT_TLS_ERROR_HANDSHAKE;
        }

        // Parse handshake messages
        size_t pos = 0;
        while (pos + 4 <= data_len) {
            uint8_t hs_type = data[pos];
            uint32_t hs_len = read_u24(data + pos + 1);

            if (pos + 4 + hs_len > data_len) {
                session->error = "incomplete handshake message";
                return RT_TLS_ERROR_HANDSHAKE;
            }

            // Update transcript before processing (H-10: abort on overflow)
            if (transcript_update(session, data + pos, 4 + hs_len) != 0)
                return RT_TLS_ERROR_HANDSHAKE;

            const uint8_t *hs_data = data + pos + 4;

            switch (hs_type) {
                case TLS_HS_SERVER_HELLO:
                    rc = process_server_hello(session, hs_data, hs_len, data + pos, 4 + hs_len);
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

                    if (session->verify_cert) {
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
                    if (session->verify_cert) {
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

long rt_tls_send(rt_tls_session_t *session, const void *data, size_t len) {
    if (!session || session->state != TLS_STATE_CONNECTED)
        return RT_TLS_ERROR;

    if (len == 0)
        return 0;

    // Send in chunks
    const uint8_t *ptr = (const uint8_t *)data;
    size_t remaining = len;

    while (remaining > 0) {
        size_t chunk = remaining > TLS_MAX_RECORD_SIZE ? TLS_MAX_RECORD_SIZE : remaining;
        int rc = send_record(session, TLS_CONTENT_APPLICATION, ptr, chunk);
        if (rc != RT_TLS_OK)
            return rc;
        ptr += chunk;
        remaining -= chunk;
    }

    return (long)len;
}

long rt_tls_recv(rt_tls_session_t *session, void *buffer, size_t len) {
    if (!session || session->state != TLS_STATE_CONNECTED)
        return RT_TLS_ERROR;

    // Return buffered data first
    if (session->app_buffer_pos < session->app_buffer_len) {
        size_t avail = session->app_buffer_len - session->app_buffer_pos;
        size_t copy = avail < len ? avail : len;
        memcpy(buffer, session->app_buffer + session->app_buffer_pos, copy);
        session->app_buffer_pos += copy;
        return (long)copy;
    }

    // Receive new record (retry loop for skipping non-application records)
    int non_app_retries = 0;
retry_recv:;
    uint8_t content_type;
    size_t data_len;

    int rc = recv_record(session, &content_type, session->app_buffer, &data_len);
    if (rc != RT_TLS_OK)
        return rc;

    if (content_type == TLS_CONTENT_ALERT) {
        session->state = TLS_STATE_CLOSED;
        return 0;
    }

    if (content_type != TLS_CONTENT_APPLICATION) {
        // Skip non-application records (e.g., NewSessionTicket post-handshake messages).
        // Limit retries to prevent CPU starvation from malicious servers.
        if (++non_app_retries > 100) {
            session->error = "TLS: too many non-application records";
            return RT_TLS_ERROR;
        }
        goto retry_recv;
    }

    session->app_buffer_len = data_len;
    session->app_buffer_pos = 0;

    size_t copy = data_len < len ? data_len : len;
    memcpy(buffer, session->app_buffer, copy);
    session->app_buffer_pos = copy;

    return (long)copy;
}

void rt_tls_close(rt_tls_session_t *session) {
    if (!session)
        return;

    if (session->state == TLS_STATE_CONNECTED) {
        // Send close_notify alert
        uint8_t alert[2] = {1, 0}; // warning, close_notify
        send_record(session, TLS_CONTENT_ALERT, alert, 2);

        // M-12: Await the peer's close_notify before closing the read side.
        // RFC 5246 §7.2.1 requires the initiator to receive the responding
        // close_notify before considering the connection fully closed.
        // Bound the drain loop to avoid hanging if the server never responds.
        int drain = 0;
        while (drain < 32) {
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

    tls_release_dynamic_state(session);
    if (session->socket_fd >= 0) {
        CLOSE_SOCKET((socket_t)session->socket_fd);
        session->socket_fd = -1;
    }
    session->state = TLS_STATE_CLOSED;

    if (rt_obj_release_check0(session))
        rt_obj_free(session);
}

const char *rt_tls_get_error(rt_tls_session_t *session) {
    if (!session)
        return "null session";
    return session->error ? session->error : "no error";
}

int rt_tls_has_buffered_data(rt_tls_session_t *session) {
    if (!session)
        return 0;
    return session->app_buffer_pos < session->app_buffer_len ? 1 : 0;
}

int rt_tls_get_socket(rt_tls_session_t *session) {
    if (!session)
        return -1;
    return (int)session->socket_fd;
}

rt_tls_session_t *rt_tls_connect(const char *host, uint16_t port, const rt_tls_config_t *config) {
#ifdef _WIN32
    // Initialize Winsock
    rt_net_init_wsa();
#endif

    rt_tls_config_t cfg;
    if (config) {
        cfg = *config;
    } else {
        rt_tls_config_init(&cfg);
    }
    cfg.hostname = host;
    if (cfg.timeout_ms <= 0)
        cfg.timeout_ms = 30000;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);

    struct addrinfo *res = NULL;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res)
        return NULL;

#ifdef _WIN32
    socket_t sock = INVALID_SOCKET;
#else
    socket_t sock = -1;
#endif
    for (struct addrinfo *p = res; p; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
#ifdef _WIN32
        if (sock == INVALID_SOCKET)
            continue;
#else
        if (sock < 0)
            continue;
#endif
        suppress_sigpipe(sock);

        int connected = 0;
        if (cfg.timeout_ms > 0) {
            tls_set_nonblocking(sock, 1);
            if (connect(sock, p->ai_addr, (int)p->ai_addrlen) == 0) {
                connected = 1;
            } else {
#ifdef _WIN32
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK)
#else
                int err = errno;
                if (err == EINPROGRESS)
#endif
                {
                    int ready = tls_wait_socket(sock, cfg.timeout_ms, 1);
                    if (ready > 0) {
                        int so_error = 0;
                        socklen_t so_error_len = sizeof(so_error);
                        getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&so_error, &so_error_len);
                        connected = (so_error == 0);
                    }
                }
            }
            tls_set_nonblocking(sock, 0);
        } else {
            connected = (connect(sock, p->ai_addr, (int)p->ai_addrlen) == 0);
        }

        if (connected)
            break;

        CLOSE_SOCKET(sock);
#ifdef _WIN32
        sock = INVALID_SOCKET;
#else
        sock = -1;
#endif
    }

    freeaddrinfo(res);
#ifdef _WIN32
    if (sock == INVALID_SOCKET)
#else
    if (sock < 0)
#endif
        return NULL;

    if (cfg.timeout_ms > 0) {
        tls_set_socket_timeout(sock, cfg.timeout_ms, 1);
        tls_set_socket_timeout(sock, cfg.timeout_ms, 0);
    }

    rt_tls_session_t *session = rt_tls_new((int)sock, &cfg);
    if (!session) {
        CLOSE_SOCKET(sock);
        return NULL;
    }

    // Perform handshake
    if (rt_tls_handshake(session) != RT_TLS_OK) {
        rt_tls_close(session);
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
typedef struct rt_viper_tls {
    rt_tls_session_t *session;
    char *host;
    int64_t port;
} rt_viper_tls_t;

/// @brief Finalizer for TLS objects.
static void rt_viper_tls_finalize(void *obj) {
    if (!obj)
        return;
    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    if (tls->session) {
        rt_tls_close(tls->session);
        tls->session = NULL;
    }
    if (tls->host) {
        free(tls->host);
        tls->host = NULL;
    }
}

/// @brief Connect to a TLS server.
/// @param host Hostname to connect to.
/// @param port Port number.
/// @return TLS object or NULL on error.
void *rt_viper_tls_connect(rt_string host, int64_t port) {
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
    if (!tls) {
        rt_tls_close(session);
        return NULL;
    }

    tls->session = session;
    tls->host = NULL;
    tls->port = port;
    rt_obj_set_finalizer(tls, rt_viper_tls_finalize);

    tls->host = strdup(host_cstr);
    if (!tls->host) {
        if (rt_obj_release_check0(tls))
            rt_obj_free(tls);
        return NULL;
    }

    return tls;
}

/// @brief Connect to a TLS server with timeout.
/// @param host Hostname to connect to.
/// @param port Port number.
/// @param timeout_ms Timeout in milliseconds.
/// @return TLS object or NULL on error.
void *rt_viper_tls_connect_for(rt_string host, int64_t port, int64_t timeout_ms) {
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
    if (!tls) {
        rt_tls_close(session);
        return NULL;
    }

    tls->session = session;
    tls->host = NULL;
    tls->port = port;
    rt_obj_set_finalizer(tls, rt_viper_tls_finalize);

    tls->host = strdup(host_cstr);
    if (!tls->host) {
        if (rt_obj_release_check0(tls))
            rt_obj_free(tls);
        return NULL;
    }

    return tls;
}

/// @brief Get the hostname of the TLS connection.
rt_string rt_viper_tls_host(void *obj) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    const char *h = tls->host ? tls->host : "";
    return rt_string_from_bytes(h, strlen(h));
}

/// @brief Get the port of the TLS connection.
int64_t rt_viper_tls_port(void *obj) {
    if (!obj)
        return 0;
    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    return tls->port;
}

/// @brief Check if the TLS connection is open.
int8_t rt_viper_tls_is_open(void *obj) {
    if (!obj)
        return 0;
    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    return tls->session != NULL;
}

/// @brief Send bytes over TLS connection.
/// @param obj TLS object.
/// @param data Bytes object to send.
/// @return Number of bytes sent, or -1 on error.
int64_t rt_viper_tls_send(void *obj, void *data) {
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
int64_t rt_viper_tls_send_str(void *obj, rt_string text) {
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
void *rt_viper_tls_recv(void *obj, int64_t max_bytes) {
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
    if (received <= 0) {
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
rt_string rt_viper_tls_recv_str(void *obj, int64_t max_bytes) {
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
    if (received <= 0) {
        free(buffer);
        return rt_string_from_bytes("", 0);
    }

    buffer[received] = '\0';
    rt_string result = rt_string_from_bytes(buffer, (size_t)received);
    free(buffer);
    return result;
}

/// @brief Read a line (up to \n) from the TLS connection.
rt_string rt_viper_tls_recv_line(void *obj) {
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

    while (1) {
        char c;
        long received = rt_tls_recv(tls->session, &c, 1);
        if (received <= 0) {
            // Connection closed or error before newline
            break;
        }

        if (c == '\n') {
            // Strip trailing CR if present
            if (len > 0 && line[len - 1] == '\r')
                len--;
            break;
        }

        // Cap at 64KB to prevent unbounded memory growth from a malicious peer
        // (matches the limit in rt_tcp_recv_line).
        if (len >= 65536) {
            free(line);
            return rt_string_from_bytes("", 0);
        }

        if (len >= cap) {
            cap *= 2;
            if (cap > 65536)
                cap = 65536;
            char *new_line = (char *)realloc(line, cap);
            if (!new_line) {
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
void rt_viper_tls_close(void *obj) {
    if (!obj)
        return;

    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    if (tls->session) {
        rt_tls_close(tls->session);
        tls->session = NULL;
    }
}

/// @brief Get the last error message.
rt_string rt_viper_tls_error(void *obj) {
    const char *msg;
    if (!obj) {
        msg = "null object";
        return rt_string_from_bytes(msg, strlen(msg));
    }

    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    if (!tls->session) {
        msg = "connection closed";
        return rt_string_from_bytes(msg, strlen(msg));
    }

    const char *err = rt_tls_get_error(tls->session);
    msg = err ? err : "no error";
    return rt_string_from_bytes(msg, strlen(msg));
}
