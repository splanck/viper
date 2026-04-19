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
#include "rt_ecdsa_p256.h"
#include "rt_object.h"
#include "rt_rsa.h"
#include "rt_tls_internal.h"
#include "rt_tls_server_internal.h"

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

#ifdef _WIN32
#define TLS_THREAD_LOCAL __declspec(thread)
#else
#define TLS_THREAD_LOCAL __thread
#endif

static TLS_THREAD_LOCAL char g_tls_last_error[256];
static TLS_THREAD_LOCAL char g_tls_server_last_error[256];

struct rt_tls_server_ctx {
    uint8_t *cert_list_entries;
    size_t cert_list_entries_len;
    uint8_t *leaf_cert_der;
    size_t leaf_cert_der_len;
    int key_type;
    uint8_t private_key[32];
    rt_rsa_key_t rsa_key;
    char alpn_protocol[64];
    int timeout_ms;
};

enum {
    TLS_SERVER_KEY_NONE = 0,
    TLS_SERVER_KEY_ECDSA_P256 = 1,
    TLS_SERVER_KEY_RSA_PSS_SHA256 = 2,
};

static void tls_set_server_last_error_msg(const char *msg);
static char *tls_read_text_file(const char *path, size_t *len_out);
static size_t tls_pem_base64_decode(
    const char *pem_b64, size_t b64_len, uint8_t *out_der, size_t max_der);
static int tls_find_pem_block(const char *pem,
                              const char *begin_marker,
                              const char *end_marker,
                              const char **body_out,
                              size_t *body_len_out,
                              const char **next_out);
static int tls_parse_sec1_ec_private_key(
    const uint8_t *der, size_t der_len, uint8_t out_priv[32]);
static int tls_parse_pkcs8_ec_private_key(
    const uint8_t *der, size_t der_len, uint8_t out_priv[32]);
static int tls_extract_cert_ec_pubkey(
    const uint8_t *cert_der, size_t cert_len, uint8_t x_out[32], uint8_t y_out[32]);
static int tls_extract_cert_rsa_pubkey(
    const uint8_t *cert_der, size_t cert_len, rt_rsa_key_t *out);
static int tls_extract_cert_key_type(const uint8_t *cert_der, size_t cert_len);
static int tls_hostname_is_ip_literal(const char *hostname);
static int tls_parse_client_sni(rt_tls_session_t *session, const uint8_t *data, size_t len);
static int tls_server_name_matches_leaf_cert(const rt_tls_server_ctx_t *ctx, const char *hostname);

// SIGPIPE suppression.
#if defined(__linux__) || defined(__viperdos__)
#define SEND_FLAGS MSG_NOSIGNAL
#else
#define SEND_FLAGS 0
#endif

/// @brief Stop SIGPIPE from killing the process when writing to a closed socket.
///
/// macOS uses the per-socket `SO_NOSIGPIPE` option (set once at
/// socket creation). Linux uses the per-call `MSG_NOSIGNAL` flag
/// (see `SEND_FLAGS`). Other platforms have nothing to do here.
static void suppress_sigpipe(socket_t sock) {
#if defined(__APPLE__) && defined(SO_NOSIGPIPE)
    int val = 1;
    setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val));
#endif
    (void)sock;
}

/// @brief Wait for a socket to become readable or writable within `timeout_ms`.
///
/// Wraps `select(2)` so the TLS state machine can convert non-blocking
/// `EAGAIN`/`EWOULDBLOCK` returns into a bounded blocking wait.
/// @param sock        Socket to wait on.
/// @param timeout_ms  Maximum wait in milliseconds.
/// @param for_write   Non-zero waits for write-readiness, zero waits for read-readiness.
/// @return >0 if ready, 0 on timeout, -1 on error (caller checks `errno`).
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

/// @brief Toggle a socket between blocking and non-blocking I/O.
///
/// Uses `ioctlsocket(FIONBIO)` on Windows and `fcntl(O_NONBLOCK)`
/// elsewhere. Silently no-ops if `fcntl(F_GETFL)` fails.
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

/// @brief Apply a recv or send timeout to a socket.
///
/// Sets `SO_RCVTIMEO` or `SO_SNDTIMEO` (chosen by `is_recv`).
/// Windows takes a `DWORD` of milliseconds; POSIX takes a `struct timeval`.
static void tls_set_socket_timeout(socket_t sock, int timeout_ms, int is_recv) {
#ifdef _WIN32
    DWORD tv = (DWORD)timeout_ms;
    setsockopt(
        sock, SOL_SOCKET, is_recv ? SO_RCVTIMEO : SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, is_recv ? SO_RCVTIMEO : SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

/// @brief Free heap-allocated handshake scratch space hanging off a session.
///
/// Releases the buffered server certificate chain and any
/// HelloRetryRequest cookie, and resets the application data
/// reassembly buffer to empty. Safe to call on a half-initialised
/// session and idempotent — pointers are nulled after free.
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

static int tls_server_ctx_append_cert(
    rt_tls_server_ctx_t *ctx, const uint8_t *der, size_t der_len, int is_leaf) {
    size_t entry_len = 3 + der_len + 2;
    uint8_t *grown = NULL;
    if (!ctx || !der || der_len == 0 || der_len > 0xFFFFFF)
        return 0;

    grown = (uint8_t *)realloc(ctx->cert_list_entries, ctx->cert_list_entries_len + entry_len);
    if (!grown)
        return 0;
    ctx->cert_list_entries = grown;
    grown += ctx->cert_list_entries_len;
    grown[0] = (uint8_t)((der_len >> 16) & 0xFF);
    grown[1] = (uint8_t)((der_len >> 8) & 0xFF);
    grown[2] = (uint8_t)(der_len & 0xFF);
    memcpy(grown + 3, der, der_len);
    grown[3 + der_len] = 0;
    grown[3 + der_len + 1] = 0;
    ctx->cert_list_entries_len += entry_len;

    if (is_leaf) {
        ctx->leaf_cert_der = (uint8_t *)malloc(der_len);
        if (!ctx->leaf_cert_der)
            return 0;
        memcpy(ctx->leaf_cert_der, der, der_len);
        ctx->leaf_cert_der_len = der_len;
    }

    return 1;
}

void rt_tls_server_config_init(rt_tls_server_config_t *config) {
    if (!config)
        return;
    memset(config, 0, sizeof(*config));
    config->timeout_ms = 30000;
}

rt_tls_server_ctx_t *rt_tls_server_ctx_new(const rt_tls_server_config_t *config) {
    char *cert_pem = NULL;
    char *key_pem = NULL;
    const char *cursor = NULL;
    const char *body = NULL;
    const char *next = NULL;
    size_t body_len = 0;
    size_t file_len = 0;
    int cert_count = 0;
    int parsed_key_type = TLS_SERVER_KEY_NONE;
    int raw_key_loaded = 0;
    rt_tls_server_ctx_t *ctx = NULL;
    uint8_t cert_pub_x[32];
    uint8_t cert_pub_y[32];
    uint8_t key_pub_x[32];
    uint8_t key_pub_y[32];
    rt_rsa_key_t cert_rsa_key;

    rt_rsa_key_init(&cert_rsa_key);

    tls_set_server_last_error_msg(NULL);

    if (!config || !config->cert_file || !config->key_file) {
        tls_set_server_last_error_msg("TLS server: certificate and key files are required");
        return NULL;
    }

    ctx = (rt_tls_server_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) {
        tls_set_server_last_error_msg("TLS server: context allocation failed");
        return NULL;
    }

    ctx->timeout_ms = config->timeout_ms > 0 ? config->timeout_ms : 30000;
    if (config->alpn_protocol)
        strncpy(ctx->alpn_protocol, config->alpn_protocol, sizeof(ctx->alpn_protocol) - 1);

    cert_pem = tls_read_text_file(config->cert_file, &file_len);
    if (!cert_pem) {
        tls_set_server_last_error_msg("TLS server: failed to read certificate file");
        goto fail;
    }

    cursor = cert_pem;
    while (tls_find_pem_block(
        cursor, "-----BEGIN CERTIFICATE-----", "-----END CERTIFICATE-----", &body, &body_len, &next)) {
        size_t max_der = body_len;
        uint8_t *der = (uint8_t *)malloc(max_der);
        size_t der_len = 0;
        if (!der) {
            tls_set_server_last_error_msg("TLS server: certificate buffer allocation failed");
            goto fail;
        }
        der_len = tls_pem_base64_decode(body, body_len, der, max_der);
        if (der_len == 0 || !tls_server_ctx_append_cert(ctx, der, der_len, cert_count == 0)) {
            free(der);
            tls_set_server_last_error_msg("TLS server: failed to decode certificate chain");
            goto fail;
        }
        free(der);
        cert_count++;
        cursor = next;
    }

    if (cert_count == 0) {
        tls_set_server_last_error_msg("TLS server: certificate file does not contain a PEM certificate");
        goto fail;
    }

    parsed_key_type = tls_extract_cert_key_type(ctx->leaf_cert_der, ctx->leaf_cert_der_len);
    if (parsed_key_type == TLS_SERVER_KEY_NONE) {
        tls_set_server_last_error_msg(
            "TLS server: leaf certificate must use an RSA or P-256 ECDSA public key");
        goto fail;
    }

    key_pem = tls_read_text_file(config->key_file, &file_len);
    if (!key_pem) {
        tls_set_server_last_error_msg("TLS server: failed to read private key file");
        goto fail;
    }

    if (parsed_key_type == TLS_SERVER_KEY_ECDSA_P256) {
        if (tls_find_pem_block(
                key_pem, "-----BEGIN PRIVATE KEY-----", "-----END PRIVATE KEY-----", &body, &body_len, NULL)) {
            uint8_t *der = (uint8_t *)malloc(body_len);
            size_t der_len = 0;
            if (!der) {
                tls_set_server_last_error_msg("TLS server: private key buffer allocation failed");
                goto fail;
            }
            der_len = tls_pem_base64_decode(body, body_len, der, body_len);
            if (der_len > 0 && tls_parse_pkcs8_ec_private_key(der, der_len, ctx->private_key))
                raw_key_loaded = 1;
            free(der);
        } else if (tls_find_pem_block(key_pem,
                                      "-----BEGIN EC PRIVATE KEY-----",
                                      "-----END EC PRIVATE KEY-----",
                                      &body,
                                      &body_len,
                                      NULL)) {
            uint8_t *der = (uint8_t *)malloc(body_len);
            size_t der_len = 0;
            if (!der) {
                tls_set_server_last_error_msg("TLS server: private key buffer allocation failed");
                goto fail;
            }
            der_len = tls_pem_base64_decode(body, body_len, der, body_len);
            if (der_len > 0 && tls_parse_sec1_ec_private_key(der, der_len, ctx->private_key))
                raw_key_loaded = 1;
            free(der);
        }
    }

    if (parsed_key_type == TLS_SERVER_KEY_ECDSA_P256 && raw_key_loaded) {
        if (!tls_extract_cert_ec_pubkey(ctx->leaf_cert_der,
                                        ctx->leaf_cert_der_len,
                                        cert_pub_x,
                                        cert_pub_y)) {
            tls_set_server_last_error_msg(
                "TLS server: leaf certificate must use a P-256 ECDSA public key");
            goto fail;
        }

        if (ecdsa_p256_public_from_private(ctx->private_key, key_pub_x, key_pub_y) &&
            memcmp(cert_pub_x, key_pub_x, sizeof(cert_pub_x)) == 0 &&
            memcmp(cert_pub_y, key_pub_y, sizeof(cert_pub_y)) == 0) {
            ctx->key_type = TLS_SERVER_KEY_ECDSA_P256;
            free(cert_pem);
            free(key_pem);
            return ctx;
        }
    }

    if (parsed_key_type == TLS_SERVER_KEY_RSA_PSS_SHA256) {
        rt_rsa_key_t private_rsa_key;
        rt_rsa_key_init(&private_rsa_key);

        if (tls_find_pem_block(
                key_pem, "-----BEGIN PRIVATE KEY-----", "-----END PRIVATE KEY-----", &body, &body_len, NULL)) {
            uint8_t *der = (uint8_t *)malloc(body_len);
            size_t der_len = 0;
            if (!der) {
                tls_set_server_last_error_msg("TLS server: private key buffer allocation failed");
                rt_rsa_key_free(&private_rsa_key);
                goto fail;
            }
            der_len = tls_pem_base64_decode(body, body_len, der, body_len);
            if (der_len > 0)
                raw_key_loaded = rt_rsa_parse_private_key_pkcs8(der, der_len, &private_rsa_key);
            free(der);
        } else if (tls_find_pem_block(key_pem,
                                      "-----BEGIN RSA PRIVATE KEY-----",
                                      "-----END RSA PRIVATE KEY-----",
                                      &body,
                                      &body_len,
                                      NULL)) {
            uint8_t *der = (uint8_t *)malloc(body_len);
            size_t der_len = 0;
            if (!der) {
                tls_set_server_last_error_msg("TLS server: private key buffer allocation failed");
                rt_rsa_key_free(&private_rsa_key);
                goto fail;
            }
            der_len = tls_pem_base64_decode(body, body_len, der, body_len);
            if (der_len > 0)
                raw_key_loaded = rt_rsa_parse_private_key_pkcs1(der, der_len, &private_rsa_key);
            free(der);
        }

        if (raw_key_loaded &&
            tls_extract_cert_rsa_pubkey(ctx->leaf_cert_der, ctx->leaf_cert_der_len, &cert_rsa_key) &&
            rt_rsa_public_equals(&cert_rsa_key, &private_rsa_key)) {
            ctx->rsa_key = private_rsa_key;
            rt_rsa_key_init(&private_rsa_key);
            ctx->key_type = TLS_SERVER_KEY_RSA_PSS_SHA256;
            free(cert_pem);
            free(key_pem);
            rt_rsa_key_free(&cert_rsa_key);
            return ctx;
        }

        rt_rsa_key_free(&private_rsa_key);
    }

    if (parsed_key_type == TLS_SERVER_KEY_ECDSA_P256) {
        if (raw_key_loaded) {
            tls_set_server_last_error_msg("TLS server: certificate and private key do not match");
        } else {
            tls_set_server_last_error_msg(
                "TLS server: unsupported ECDSA private key format; expected an unencrypted P-256 PEM");
        }
    } else {
        if (raw_key_loaded)
            tls_set_server_last_error_msg("TLS server: certificate and private key do not match");
        else
            tls_set_server_last_error_msg(
                "TLS server: unsupported RSA private key format; expected an unencrypted PKCS#1 or PKCS#8 PEM");
    }
    goto fail;

fail:
    free(cert_pem);
    free(key_pem);
    rt_rsa_key_free(&cert_rsa_key);
    rt_tls_server_ctx_free(ctx);
    return NULL;
}

void rt_tls_server_ctx_free(rt_tls_server_ctx_t *ctx) {
    if (!ctx)
        return;
    rt_rsa_key_free(&ctx->rsa_key);
    free(ctx->cert_list_entries);
    free(ctx->leaf_cert_der);
    free(ctx);
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
#define TLS_EXT_ALPN 16
#define TLS_EXT_SUPPORTED_GROUPS 10
#define TLS_EXT_SIGNATURE_ALGORITHMS 13
#define TLS_EXT_COOKIE 44
#define TLS_EXT_SUPPORTED_VERSIONS 43
#define TLS_EXT_KEY_SHARE 51

static const uint8_t TLS_HELLO_RETRY_RANDOM[32] = {
    0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A, 0x61, 0x11, 0xBE, 0x1D, 0x8C, 0x02, 0x1E, 0x65, 0xB8, 0x91,
    0xC2, 0xA2, 0x11, 0x16, 0x7A, 0xBB, 0x8C, 0x5E, 0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C};

// Note: TLS_MAX_RECORD_SIZE, TLS_MAX_CIPHERTEXT, tls_state_t, traffic_keys_t,
// and struct rt_tls_session are defined in rt_tls_internal.h.

// ---------------------------------------------------------------------------
// Wire-format byte helpers. TLS uses network byte order (big-endian) for all
// length and version fields. The 24-bit width is required by the handshake
// header (RFC 8446 §4: `Handshake.length` is a `uint24`).
// ---------------------------------------------------------------------------

/// @brief Write `v` to `p` as a big-endian unsigned 16-bit integer.
static void write_u16(uint8_t *p, uint16_t v) {
    p[0] = (v >> 8) & 0xFF;
    p[1] = v & 0xFF;
}

/// @brief Write the low 24 bits of `v` to `p` as big-endian (`uint24` in TLS).
static void write_u24(uint8_t *p, uint32_t v) {
    p[0] = (v >> 16) & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = v & 0xFF;
}

/// @brief Read a big-endian unsigned 16-bit integer from `p`.
static uint16_t read_u16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

/// @brief Read a big-endian 24-bit value from `p` into the low 24 bits of a `uint32_t`.
static uint32_t read_u24(const uint8_t *p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static int tls_der_read_tlv(
    const uint8_t *buf, size_t buf_len, uint8_t *tag, size_t *val_len, size_t *hdr_len) {
    if (buf_len < 2)
        return -1;

    *tag = buf[0];
    if (buf[1] < 0x80) {
        *val_len = buf[1];
        *hdr_len = 2;
    } else {
        size_t num_len_bytes = buf[1] & 0x7F;
        size_t value = 0;
        if (num_len_bytes == 0 || num_len_bytes > 4 || 2 + num_len_bytes > buf_len)
            return -1;
        for (size_t i = 0; i < num_len_bytes; i++)
            value = (value << 8) | buf[2 + i];
        *val_len = value;
        *hdr_len = 2 + num_len_bytes;
    }

    return (*hdr_len + *val_len <= buf_len) ? 0 : -1;
}

static int tls_oid_matches(
    const uint8_t *buf, size_t buf_len, const uint8_t *oid, size_t oid_len) {
    return buf_len == oid_len && memcmp(buf, oid, oid_len) == 0;
}

static int tls_hostname_is_ip_literal(const char *hostname) {
    struct in_addr ipv4;
    struct in6_addr ipv6;
    if (!hostname || !*hostname)
        return 0;
    if (inet_pton(AF_INET, hostname, &ipv4) == 1)
        return 1;
    if (inet_pton(AF_INET6, hostname, &ipv6) == 1)
        return 1;
    return 0;
}

static int tls_parse_client_sni(rt_tls_session_t *session, const uint8_t *data, size_t len) {
    size_t pos = 0;
    int saw_host_name = 0;

    if (!session || !data || len < 2)
        return RT_TLS_ERROR_HANDSHAKE;

    {
        uint16_t list_len = read_u16(data);
        if ((size_t)list_len + 2 != len) {
            session->error = "ClientHello server_name length mismatch";
            return RT_TLS_ERROR_HANDSHAKE;
        }
        pos = 2;
        while (pos + 3 <= len) {
            uint8_t name_type = data[pos++];
            uint16_t name_len = read_u16(data + pos);
            pos += 2;
            if (pos + name_len > len) {
                session->error = "ClientHello server_name entry truncated";
                return RT_TLS_ERROR_HANDSHAKE;
            }
            if (name_type == 0) {
                if (saw_host_name) {
                    session->error = "ClientHello contains multiple host_name SNI entries";
                    return RT_TLS_ERROR_HANDSHAKE;
                }
                if (name_len == 0 || name_len >= sizeof(session->hostname)) {
                    session->error = "ClientHello host_name SNI is invalid";
                    return RT_TLS_ERROR_HANDSHAKE;
                }
                for (uint16_t i = 0; i < name_len; i++) {
                    unsigned char ch = data[pos + i];
                    if (ch <= 0x20u || ch >= 0x7Fu) {
                        session->error = "ClientHello host_name SNI contains invalid characters";
                        return RT_TLS_ERROR_HANDSHAKE;
                    }
                    session->hostname[i] =
                        (char)((ch >= 'A' && ch <= 'Z') ? (ch + ('a' - 'A')) : ch);
                }
                session->hostname[name_len] = '\0';
                if (tls_hostname_is_ip_literal(session->hostname))
                    session->hostname[0] = '\0';
                saw_host_name = 1;
            }
            pos += name_len;
        }
        if (pos != len) {
            session->error = "ClientHello server_name malformed";
            return RT_TLS_ERROR_HANDSHAKE;
        }
    }

    return RT_TLS_OK;
}

static int tls_server_name_matches_leaf_cert(const rt_tls_server_ctx_t *ctx, const char *hostname) {
    char san_names[32][256];
    char cn[256];
    int san_count = 0;

    if (!ctx || !ctx->leaf_cert_der || ctx->leaf_cert_der_len == 0 || !hostname || !*hostname)
        return 1;
    if (tls_hostname_is_ip_literal(hostname))
        return 1;

    san_count = tls_extract_san_names(ctx->leaf_cert_der, ctx->leaf_cert_der_len, san_names, 32);
    for (int i = 0; i < san_count; i++) {
        if (tls_match_hostname(san_names[i], hostname))
            return 1;
    }
    if (san_count > 0)
        return 0;

    if (tls_extract_cn(ctx->leaf_cert_der, ctx->leaf_cert_der_len, cn) && cn[0] != '\0')
        return tls_match_hostname(cn, hostname);

    return 0;
}

static char *tls_read_text_file(const char *path, size_t *len_out) {
    FILE *f = NULL;
    char *buf = NULL;
    long len = 0;

    if (len_out)
        *len_out = 0;
    if (!path || !*path)
        return NULL;

    f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0)
        goto fail;
    len = ftell(f);
    if (len < 0 || fseek(f, 0, SEEK_SET) != 0)
        goto fail;

    buf = (char *)malloc((size_t)len + 1);
    if (!buf)
        goto fail;
    if ((long)fread(buf, 1, (size_t)len, f) != len)
        goto fail;
    buf[len] = '\0';
    fclose(f);
    if (len_out)
        *len_out = (size_t)len;
    return buf;

fail:
    if (f)
        fclose(f);
    free(buf);
    return NULL;
}

static size_t tls_pem_base64_decode(
    const char *pem_b64, size_t b64_len, uint8_t *out_der, size_t max_der) {
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

    for (size_t i = 0; i < b64_len; i++) {
        unsigned char c = (unsigned char)pem_b64[i];
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t')
            continue;
        if (c == '=')
            break;
        if (b64tab[c] < 0)
            continue;
        acc = (acc << 6) | (uint32_t)b64tab[c];
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (out_len >= max_der)
                return 0;
            out_der[out_len++] = (uint8_t)((acc >> bits) & 0xFF);
        }
    }
    return out_len;
}

static int tls_find_pem_block(const char *pem,
                              const char *begin_marker,
                              const char *end_marker,
                              const char **body_out,
                              size_t *body_len_out,
                              const char **next_out) {
    const char *begin = NULL;
    const char *body = NULL;
    const char *end = NULL;
    if (!pem || !begin_marker || !end_marker || !body_out || !body_len_out)
        return 0;
    begin = strstr(pem, begin_marker);
    if (!begin)
        return 0;
    body = strchr(begin, '\n');
    if (!body)
        return 0;
    body++;
    end = strstr(body, end_marker);
    if (!end)
        return 0;
    *body_out = body;
    *body_len_out = (size_t)(end - body);
    if (next_out)
        *next_out = end + strlen(end_marker);
    return 1;
}

static int tls_copy_der_octets(
    const uint8_t *data, size_t len, uint8_t out[32], size_t *out_len) {
    size_t skip = 0;
    if (len == 0 || !out || !out_len)
        return 0;
    while (skip + 1 < len && data[skip] == 0x00)
        skip++;
    len -= skip;
    data += skip;
    if (len > 32)
        return 0;
    memset(out, 0, 32);
    memcpy(out + (32 - len), data, len);
    *out_len = len;
    return 1;
}

static int tls_parse_sec1_ec_private_key(const uint8_t *der,
                                         size_t der_len,
                                         uint8_t out_priv[32]) {
    uint8_t tag;
    size_t vl, hl;
    const uint8_t *p = der;
    size_t rem = der_len;
    size_t scalar_len = 0;

    if (tls_der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    p += hl;
    rem = vl;
    if (tls_der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x02)
        return 0;
    p += hl + vl;
    rem -= hl + vl;
    if (tls_der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x04)
        return 0;
    return tls_copy_der_octets(p + hl, vl, out_priv, &scalar_len);
}

static int tls_parse_pkcs8_ec_private_key(const uint8_t *der,
                                          size_t der_len,
                                          uint8_t out_priv[32]) {
    static const uint8_t OID_EC_PUBLIC_KEY[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01};
    static const uint8_t OID_PRIME256V1[] = {
        0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07};
    uint8_t tag;
    size_t vl, hl;
    const uint8_t *p = der;
    size_t rem = der_len;
    const uint8_t *alg = NULL;
    size_t alg_len = 0;

    if (tls_der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    p += hl;
    rem = vl;
    if (tls_der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x02)
        return 0;
    p += hl + vl;
    rem -= hl + vl;
    if (tls_der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    alg = p + hl;
    alg_len = vl;
    p += hl + vl;
    rem -= hl + vl;

    if (tls_der_read_tlv(alg, alg_len, &tag, &vl, &hl) != 0 || tag != 0x06 ||
        !tls_oid_matches(alg + hl, vl, OID_EC_PUBLIC_KEY, sizeof(OID_EC_PUBLIC_KEY))) {
        return 0;
    }
    alg += hl + vl;
    alg_len -= hl + vl;
    if (tls_der_read_tlv(alg, alg_len, &tag, &vl, &hl) != 0 || tag != 0x06 ||
        !tls_oid_matches(alg + hl, vl, OID_PRIME256V1, sizeof(OID_PRIME256V1))) {
        return 0;
    }

    if (tls_der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x04)
        return 0;
    return tls_parse_sec1_ec_private_key(p + hl, vl, out_priv);
}

static int tls_extract_cert_ec_pubkey(const uint8_t *cert_der,
                                      size_t cert_len,
                                      uint8_t x_out[32],
                                      uint8_t y_out[32]) {
    static const uint8_t OID_EC_PUBLIC_KEY[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01};
    static const uint8_t OID_PRIME256V1[] = {
        0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07};
    uint8_t tag;
    size_t vl, hl;
    const uint8_t *p = cert_der;
    size_t rem = cert_len;
    const uint8_t *tbs = NULL;
    size_t tbs_rem = 0;
    const uint8_t *spki = NULL;
    size_t spki_rem = 0;
    const uint8_t *algo = NULL;
    size_t algo_rem = 0;
    const uint8_t *bits = NULL;

    if (tls_der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    p += hl;
    rem = vl;
    if (tls_der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    tbs = p + hl;
    tbs_rem = vl;

    if (tls_der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0)
        return 0;
    if (tag == 0xA0) {
        tbs += hl + vl;
        tbs_rem -= hl + vl;
    }

    for (int i = 0; i < 5; i++) {
        if (tls_der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0)
            return 0;
        tbs += hl + vl;
        tbs_rem -= hl + vl;
    }

    if (tls_der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    spki = tbs + hl;
    spki_rem = vl;

    if (tls_der_read_tlv(spki, spki_rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    algo = spki + hl;
    algo_rem = vl;
    spki += hl + vl;
    spki_rem -= hl + vl;

    if (tls_der_read_tlv(algo, algo_rem, &tag, &vl, &hl) != 0 || tag != 0x06 ||
        !tls_oid_matches(algo + hl, vl, OID_EC_PUBLIC_KEY, sizeof(OID_EC_PUBLIC_KEY))) {
        return 0;
    }
    algo += hl + vl;
    algo_rem -= hl + vl;
    if (tls_der_read_tlv(algo, algo_rem, &tag, &vl, &hl) != 0 || tag != 0x06 ||
        !tls_oid_matches(algo + hl, vl, OID_PRIME256V1, sizeof(OID_PRIME256V1))) {
        return 0;
    }

    if (tls_der_read_tlv(spki, spki_rem, &tag, &vl, &hl) != 0 || tag != 0x03 || vl < 66)
        return 0;
    bits = spki + hl;
    if (bits[0] != 0x00 || bits[1] != 0x04)
        return 0;
    memcpy(x_out, bits + 2, 32);
    memcpy(y_out, bits + 34, 32);
    return 1;
}

static int tls_extract_cert_rsa_pubkey(
    const uint8_t *cert_der, size_t cert_len, rt_rsa_key_t *out) {
    static const uint8_t OID_RSA_ENCRYPTION[] = {
        0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01};
    uint8_t tag;
    size_t vl, hl;
    const uint8_t *p = cert_der;
    size_t rem = cert_len;
    const uint8_t *tbs = NULL;
    size_t tbs_rem = 0;
    const uint8_t *spki = NULL;
    size_t spki_rem = 0;
    const uint8_t *algo = NULL;
    size_t algo_rem = 0;
    const uint8_t *bits = NULL;

    if (!cert_der || cert_len == 0 || !out)
        return 0;
    rt_rsa_key_init(out);

    if (tls_der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    p += hl;
    rem = vl;
    if (tls_der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    tbs = p + hl;
    tbs_rem = vl;

    if (tls_der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0)
        return 0;
    if (tag == 0xA0) {
        tbs += hl + vl;
        tbs_rem -= hl + vl;
    }

    for (int i = 0; i < 5; i++) {
        if (tls_der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0)
            return 0;
        tbs += hl + vl;
        tbs_rem -= hl + vl;
    }

    if (tls_der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    spki = tbs + hl;
    spki_rem = vl;
    if (tls_der_read_tlv(spki, spki_rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    algo = spki + hl;
    algo_rem = vl;
    spki += hl + vl;
    spki_rem -= hl + vl;

    if (tls_der_read_tlv(algo, algo_rem, &tag, &vl, &hl) != 0 || tag != 0x06 ||
        !tls_oid_matches(algo + hl, vl, OID_RSA_ENCRYPTION, sizeof(OID_RSA_ENCRYPTION))) {
        return 0;
    }
    if (tls_der_read_tlv(spki, spki_rem, &tag, &vl, &hl) != 0 || tag != 0x03 || vl < 2)
        return 0;
    bits = spki + hl;
    if (bits[0] != 0x00)
        return 0;
    return rt_rsa_parse_public_key_pkcs1(bits + 1, vl - 1, out);
}

static int tls_extract_cert_key_type(const uint8_t *cert_der, size_t cert_len) {
    static const uint8_t OID_EC_PUBLIC_KEY[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01};
    static const uint8_t OID_PRIME256V1[] = {
        0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07};
    static const uint8_t OID_RSA_ENCRYPTION[] = {
        0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01};
    uint8_t tag;
    size_t vl, hl;
    const uint8_t *p = cert_der;
    size_t rem = cert_len;
    const uint8_t *tbs = NULL;
    size_t tbs_rem = 0;
    const uint8_t *spki = NULL;
    size_t spki_rem = 0;
    const uint8_t *algo = NULL;
    size_t algo_rem = 0;

    if (tls_der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return TLS_SERVER_KEY_NONE;
    p += hl;
    rem = vl;
    if (tls_der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return TLS_SERVER_KEY_NONE;
    tbs = p + hl;
    tbs_rem = vl;

    if (tls_der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0)
        return TLS_SERVER_KEY_NONE;
    if (tag == 0xA0) {
        tbs += hl + vl;
        tbs_rem -= hl + vl;
    }

    for (int i = 0; i < 5; i++) {
        if (tls_der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0)
            return TLS_SERVER_KEY_NONE;
        tbs += hl + vl;
        tbs_rem -= hl + vl;
    }

    if (tls_der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return TLS_SERVER_KEY_NONE;
    spki = tbs + hl;
    spki_rem = vl;

    if (tls_der_read_tlv(spki, spki_rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return TLS_SERVER_KEY_NONE;
    algo = spki + hl;
    algo_rem = vl;

    if (tls_der_read_tlv(algo, algo_rem, &tag, &vl, &hl) != 0 || tag != 0x06)
        return TLS_SERVER_KEY_NONE;
    if (tls_oid_matches(algo + hl, vl, OID_RSA_ENCRYPTION, sizeof(OID_RSA_ENCRYPTION)))
        return TLS_SERVER_KEY_RSA_PSS_SHA256;
    if (!tls_oid_matches(algo + hl, vl, OID_EC_PUBLIC_KEY, sizeof(OID_EC_PUBLIC_KEY)))
        return TLS_SERVER_KEY_NONE;

    algo += hl + vl;
    algo_rem -= hl + vl;
    if (tls_der_read_tlv(algo, algo_rem, &tag, &vl, &hl) != 0 || tag != 0x06)
        return TLS_SERVER_KEY_NONE;
    if (tls_oid_matches(algo + hl, vl, OID_PRIME256V1, sizeof(OID_PRIME256V1)))
        return TLS_SERVER_KEY_ECDSA_P256;
    return TLS_SERVER_KEY_NONE;
}

/// @brief Reset the transcript hash to the empty-handshake state.
///
/// TLS 1.3 keys are derived from a running SHA-256 hash over every
/// handshake message exchanged so far. This primes the rolling
/// context and seeds `transcript_hash` with `SHA-256("")` so that
/// HKDF helpers can be called even before the first message arrives.
static void transcript_init(rt_tls_session_t *session) {
    session->transcript_len = 0;
    rt_sha256_init(&session->transcript_ctx);
    rt_sha256(NULL, 0, session->transcript_hash);
}

static void tls_set_last_error_msg(const char *msg) {
    if (!msg || !*msg) {
        g_tls_last_error[0] = '\0';
        return;
    }
    snprintf(g_tls_last_error, sizeof(g_tls_last_error), "%s", msg);
}

static void tls_set_server_last_error_msg(const char *msg) {
    if (!msg || !*msg) {
        g_tls_server_last_error[0] = '\0';
        return;
    }
    snprintf(g_tls_server_last_error, sizeof(g_tls_server_last_error), "%s", msg);
}

const char *rt_tls_last_error(void) {
    return g_tls_last_error[0] ? g_tls_last_error : "no error";
}

const char *rt_tls_server_last_error(void) {
    return g_tls_server_last_error[0] ? g_tls_server_last_error : "no error";
}

/// @brief Append `len` bytes of handshake data to the transcript hash.
///
/// Maintains a snapshot in `session->transcript_hash` after each
/// update so callers can derive secrets at any handshake boundary
/// without finalising the live context. Sets `session->error` and
/// returns -1 if the cumulative length would overflow `size_t` or
/// the session is already in an error state.
/// @return 0 on success, -1 on overflow / pre-existing error.
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

/// @brief Derive TLS 1.3 handshake traffic keys from the ECDHE shared secret.
///
/// Implements the HKDF schedule from RFC 8446 §7.1:
///   early_secret    = HKDF-Extract(0, 0)
///   derived         = Derive-Secret(early_secret, "derived", "")
///   handshake_secret= HKDF-Extract(derived, shared_secret)
///   c_hs_traffic    = Derive-Secret(handshake_secret, "c hs traffic", H(handshake))
///   s_hs_traffic    = Derive-Secret(handshake_secret, "s hs traffic", H(handshake))
///
/// Then expands each traffic secret into an AEAD key (16 bytes for
/// AES-128-GCM, 32 bytes for ChaCha20-Poly1305) and a 12-byte IV.
/// The transcript hash must already cover ClientHello + ServerHello
/// before this function is called.
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

static void derive_handshake_keys_server(
    rt_tls_session_t *session, const uint8_t shared_secret[32]) {
    uint8_t zero_key[32] = {0};
    uint8_t early_secret[32];
    uint8_t derived[32];
    int key_len;

    rt_hkdf_extract(NULL, 0, zero_key, 32, early_secret);

    {
        uint8_t empty_hash[32];
        rt_sha256(NULL, 0, empty_hash);
        rt_hkdf_expand_label(early_secret, "derived", empty_hash, 32, derived, 32);
    }

    rt_hkdf_extract(derived, 32, shared_secret, 32, session->handshake_secret);

    rt_hkdf_expand_label(session->handshake_secret,
                         "c hs traffic",
                         session->transcript_hash,
                         32,
                         session->client_handshake_traffic_secret,
                         32);
    rt_hkdf_expand_label(session->handshake_secret,
                         "s hs traffic",
                         session->transcript_hash,
                         32,
                         session->server_handshake_traffic_secret,
                         32);

    key_len = (session->cipher_suite == TLS_AES_128_GCM_SHA256) ? 16 : 32;
    rt_hkdf_expand_label(
        session->client_handshake_traffic_secret, "key", NULL, 0, session->read_keys.key, key_len);
    rt_hkdf_expand_label(
        session->client_handshake_traffic_secret, "iv", NULL, 0, session->read_keys.iv, 12);
    session->read_keys.seq_num = 0;

    rt_hkdf_expand_label(
        session->server_handshake_traffic_secret, "key", NULL, 0, session->write_keys.key, key_len);
    rt_hkdf_expand_label(
        session->server_handshake_traffic_secret, "iv", NULL, 0, session->write_keys.iv, 12);
    session->write_keys.seq_num = 0;

    session->keys_established = 1;
}

static void derive_application_secrets(rt_tls_session_t *session) {
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
}

static void install_traffic_keys_from_secret(
    const uint8_t secret[32], traffic_keys_t *keys, uint16_t cipher_suite) {
    int key_len = (cipher_suite == TLS_AES_128_GCM_SHA256) ? 16 : 32;
    rt_hkdf_expand_label(secret, "key", NULL, 0, keys->key, key_len);
    rt_hkdf_expand_label(secret, "iv", NULL, 0, keys->iv, 12);
    keys->seq_num = 0;
}

static void install_client_application_read_keys(rt_tls_session_t *session) {
    install_traffic_keys_from_secret(
        session->server_application_traffic_secret, &session->read_keys, session->cipher_suite);
}

static void install_client_application_write_keys(rt_tls_session_t *session) {
    install_traffic_keys_from_secret(
        session->client_application_traffic_secret, &session->write_keys, session->cipher_suite);
}

static void install_server_application_read_keys(rt_tls_session_t *session) {
    install_traffic_keys_from_secret(
        session->client_application_traffic_secret, &session->read_keys, session->cipher_suite);
}

static void install_server_application_write_keys(rt_tls_session_t *session) {
    install_traffic_keys_from_secret(
        session->server_application_traffic_secret, &session->write_keys, session->cipher_suite);
}

static void update_application_secret_and_keys(uint8_t secret[32],
                                               traffic_keys_t *keys,
                                               uint16_t cipher_suite) {
    uint8_t next_secret[32];
    const int app_key_len = (cipher_suite == TLS_AES_128_GCM_SHA256) ? 16 : 32;

    rt_hkdf_expand_label(secret, "traffic upd", NULL, 0, next_secret, sizeof(next_secret));
    memcpy(secret, next_secret, sizeof(next_secret));

    rt_hkdf_expand_label(secret, "key", NULL, 0, keys->key, app_key_len);
    rt_hkdf_expand_label(secret, "iv", NULL, 0, keys->iv, sizeof(keys->iv));
    keys->seq_num = 0;
}

static void update_read_application_keys(rt_tls_session_t *session) {
    if (session->is_server) {
        update_application_secret_and_keys(
            session->client_application_traffic_secret, &session->read_keys, session->cipher_suite);
    } else {
        update_application_secret_and_keys(
            session->server_application_traffic_secret, &session->read_keys, session->cipher_suite);
    }
}

static void update_write_application_keys(rt_tls_session_t *session) {
    if (session->is_server) {
        update_application_secret_and_keys(
            session->server_application_traffic_secret, &session->write_keys, session->cipher_suite);
    } else {
        update_application_secret_and_keys(
            session->client_application_traffic_secret, &session->write_keys, session->cipher_suite);
    }
}

static int send_record(rt_tls_session_t *session,
                       uint8_t content_type,
                       const uint8_t *data,
                       size_t len);

static int send_key_update_record(rt_tls_session_t *session, uint8_t request_update) {
    uint8_t msg[5];
    msg[0] = 24; // KeyUpdate
    write_u24(msg + 1, 1);
    msg[4] = request_update ? 1 : 0;
    return send_record(session, TLS_CONTENT_HANDSHAKE, msg, sizeof(msg));
}

/// @brief Construct the per-record AEAD nonce per RFC 8446 §5.3.
///
/// The nonce is the static 12-byte IV XORed with the big-endian
/// 64-bit sequence number aligned to the rightmost 8 bytes. This
/// guarantees nonce uniqueness for every record under the same key
/// without sending an explicit nonce on the wire.
static void build_nonce(const uint8_t iv[12], uint64_t seq, uint8_t nonce[12]) {
    memcpy(nonce, iv, 12);
    for (int i = 0; i < 8; i++) {
        nonce[12 - 1 - i] ^= (seq >> (i * 8)) & 0xFF;
    }
}

/// @brief Frame, optionally encrypt, and transmit a single TLS record.
///
/// Behaviour depends on `session->keys_established`:
///   - Pre-handshake: writes a plaintext record of `content_type`
///     with `data` as payload.
///   - Post-handshake: emits an `application_data` (23) record whose
///     plaintext is `data || content_type`, AEAD-sealed under the
///     active write key with a sequence-number-derived nonce. The
///     5-byte record header serves as the AAD.
///
/// Selects AES-128-GCM or ChaCha20-Poly1305 based on the negotiated
/// cipher suite. Bumps the write sequence number on each encrypted
/// record and refuses to send when the counter would wrap (RFC 8446
/// §5.5 nonce-reuse guard). Loops on partial `send()` and treats
/// `EINTR` as retryable.
/// @return RT_TLS_OK, RT_TLS_ERROR (encryption / overflow), or
///         RT_TLS_ERROR_SOCKET (write failure).
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

/// @brief Read, decrypt, and unframe one TLS record from the wire.
///
/// Reads the 5-byte record header, validates the length is within
/// the spec maximum (`TLS_MAX_CIPHERTEXT`), then drains the
/// payload. Once keys are established and the record is
/// `application_data`, AEAD-decrypts under the read key with the
/// header as AAD, strips the inner content-type byte (RFC 8446
/// §5.2), and returns the unwrapped type via `*content_type`.
/// Loops on partial `recv()` and treats `EINTR`/`EAGAIN` as retryable.
/// Bumps the read sequence number on every successfully decrypted
/// record and trips an error if it wraps.
/// @param[out] content_type  Unwrapped TLS content type byte.
/// @param[out] data          Caller-provided payload buffer.
/// @param[out] data_len      Bytes written to `data` on success.
/// @return RT_TLS_OK, RT_TLS_ERROR (decrypt/length), RT_TLS_ERROR_SOCKET, or RT_TLS_ERROR_CLOSED.
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

/// @brief Construct and transmit a TLS 1.3 ClientHello message.
///
/// Lays out the legacy `client_version` (TLS 1.2 for compatibility),
/// 32-byte client random, empty session ID, the offered cipher
/// suites (`AES-128-GCM-SHA256` then `CHACHA20_POLY1305_SHA256`),
/// the null compression method, and the following extensions:
///   - `server_name` (when `session->hostname` is set, RFC 6066)
///   - `supported_versions` (TLS 1.3 only)
    ///   - `alpn` (optional single protocol, e.g. `http/1.1`)
    ///   - `supported_groups` (X25519 only — see RFC 7748)
///   - `signature_algorithms` (ECDSA P-256/P-384, RSA-PSS SHA-256/384/512)
///   - `cookie` (only when echoing back a HelloRetryRequest)
///   - `key_share` with a freshly generated X25519 public key
///
/// Wraps the body in a handshake header, mirrors it into the
/// transcript, snapshots `client_hello_hash` for HelloRetryRequest
/// folding, and hands the bytes to `send_record`. Advances the
/// session state to `TLS_STATE_CLIENT_HELLO_SENT` on success.
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
    if (session->hostname[0] != '\0' && !tls_hostname_is_ip_literal(session->hostname)) {
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

    // ALPN (optional single protocol)
    if (session->alpn_protocol[0] != '\0') {
        size_t proto_len = strlen(session->alpn_protocol);
        if (proto_len == 0 || proto_len > 255 || pos + proto_len + 7 > sizeof(msg) - 64) {
            session->error = "ClientHello: ALPN protocol too long";
            return RT_TLS_ERROR;
        }
        write_u16(msg + pos, TLS_EXT_ALPN);
        pos += 2;
        write_u16(msg + pos, (uint16_t)(proto_len + 3));
        pos += 2;
        write_u16(msg + pos, (uint16_t)(proto_len + 1));
        pos += 2;
        msg[pos++] = (uint8_t)proto_len;
        memcpy(msg + pos, session->alpn_protocol, proto_len);
        pos += proto_len;
    }

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
        if (session->hello_retry_cookie_len > 0xFFFF - 2 ||
            pos + 6 + session->hello_retry_cookie_len > sizeof(msg)) {
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
    // KeyShareClientHello wraps the KeyShareEntry in a length-prefixed vector:
    // 2 bytes vector length + (2 bytes group + 2 bytes key len + 32 bytes key).
    write_u16(msg + pos, 38);
    pos += 2;
    write_u16(msg + pos, 36); // client_shares vector length
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

/// @brief Parse a ServerHello (or HelloRetryRequest) and finish key-exchange.
///
/// Validates the message body field by field with strict bounds
/// checks (each advance is guarded so a malformed length cannot
/// overrun the record). Detects HelloRetryRequest by the magic
/// `random` value defined in RFC 8446 §4.1.3. Reads the chosen
/// cipher suite (must be AES-128-GCM-SHA256 or
/// CHACHA20-POLY1305-SHA256) and walks the extension block to find
/// `supported_versions` (must be TLS 1.3), `key_share` (X25519
/// public point), and a `cookie` for HRR.
///
/// On a HelloRetryRequest path: substitutes the synthetic
/// "message_hash" record into the transcript per RFC 8446 §4.4.1,
/// stashes the cookie, and instructs the caller to resend the
/// ClientHello.
///
/// On a real ServerHello: completes the X25519 ECDHE shared secret
/// using the client's stored private key, derives handshake traffic
/// keys via `derive_handshake_keys`, mixes the message into the
/// transcript, and advances the state machine.
/// @param data       Pointer to the ServerHello body (after handshake header).
/// @param len        Length of the body.
/// @param full_msg   Pointer to the full handshake message including header.
/// @param full_len   Length of the full message (used for transcript update).
/// @return RT_TLS_OK, RT_TLS_RETRY (caller should resend ClientHello),
///         or an error code on malformed / unsupported input.
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
    int is_hrr =
        memcmp(session->server_random, TLS_HELLO_RETRY_RANDOM, sizeof(TLS_HELLO_RETRY_RANDOM)) == 0;

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
            session->error =
                "TLS: missing initial ClientHello transcript hash for HelloRetryRequest";
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

static int process_encrypted_extensions(rt_tls_session_t *session, const uint8_t *data, size_t len) {
    if (len < 2) {
        session->error = "EncryptedExtensions too short";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    size_t ext_len = read_u16(data);
    if (ext_len != len - 2) {
        session->error = "EncryptedExtensions length mismatch";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    const uint8_t *p = data + 2;
    const uint8_t *end = data + len;
    while (p + 4 <= end) {
        uint16_t ext_type = read_u16(p);
        uint16_t one_ext_len = read_u16(p + 2);
        p += 4;
        if ((size_t)(end - p) < one_ext_len) {
            session->error = "EncryptedExtensions truncated";
            return RT_TLS_ERROR_HANDSHAKE;
        }

        if (ext_type == TLS_EXT_ALPN) {
            if (one_ext_len < 3) {
                session->error = "EncryptedExtensions ALPN too short";
                return RT_TLS_ERROR_HANDSHAKE;
            }
            size_t list_len = read_u16(p);
            if (list_len + 2 != one_ext_len || list_len < 1) {
                session->error = "EncryptedExtensions ALPN malformed";
                return RT_TLS_ERROR_HANDSHAKE;
            }
            uint8_t proto_len = p[2];
            if ((size_t)proto_len + 3 != one_ext_len || proto_len == 0) {
                session->error = "EncryptedExtensions ALPN protocol malformed";
                return RT_TLS_ERROR_HANDSHAKE;
            }
            if (session->alpn_protocol[0] != '\0') {
                size_t expected_len = strlen(session->alpn_protocol);
                if (expected_len != proto_len ||
                    memcmp(p + 3, session->alpn_protocol, expected_len) != 0) {
                    session->error = "EncryptedExtensions ALPN mismatch";
                    return RT_TLS_ERROR_HANDSHAKE;
                }
            }
        }

        p += one_ext_len;
    }

    if (p != end) {
        session->error = "EncryptedExtensions trailing bytes";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    session->state = TLS_STATE_WAIT_CERTIFICATE;
    return RT_TLS_OK;
}

static int process_post_handshake_message(rt_tls_session_t *session,
                                          uint8_t hs_type,
                                          const uint8_t *hs_data,
                                          size_t hs_len) {
    switch (hs_type) {
        case 4: // NewSessionTicket
            return RT_TLS_OK;

        case 24: { // KeyUpdate
            if (hs_len != 1 || hs_data[0] > 1) {
                session->error = "TLS: malformed KeyUpdate";
                return RT_TLS_ERROR_HANDSHAKE;
            }

            update_read_application_keys(session);
            if (hs_data[0] == 1) {
                int rc = send_key_update_record(session, 0);
                if (rc != RT_TLS_OK)
                    return rc;
                update_write_application_keys(session);
            }
            return RT_TLS_OK;
        }

        default:
            return RT_TLS_OK;
    }
}

/// @brief Compute and send the client Finished handshake message.
///
/// Derives the per-direction "finished_key" via HKDF-Expand-Label
/// from the client handshake traffic secret, MACs the current
/// transcript hash with HMAC-SHA256, and emits a Finished record.
/// The transcript is updated *before* sending so the application
/// keys derived afterwards mix in this message (RFC 8446 §4.4.4).
static int send_finished_with_secret(
    rt_tls_session_t *session, const uint8_t base_secret[32]) {
    uint8_t finished_key[32];
    rt_hkdf_expand_label(base_secret, "finished", NULL, 0, finished_key, 32);

    uint8_t verify_data[32];
    rt_hmac_sha256(finished_key, 32, session->transcript_hash, 32, verify_data);

    uint8_t msg[4 + 32];
    msg[0] = TLS_HS_FINISHED;
    write_u24(msg + 1, 32);
    memcpy(msg + 4, verify_data, 32);

    {
        int rc = send_record(session, TLS_CONTENT_HANDSHAKE, msg, 36);
        if (rc != RT_TLS_OK)
            return rc;
    }

    return transcript_update(session, msg, 36) == 0 ? RT_TLS_OK : RT_TLS_ERROR_HANDSHAKE;
}

static int send_finished(rt_tls_session_t *session) {
    return send_finished_with_secret(session, session->client_handshake_traffic_secret);
}

static int send_finished_server(rt_tls_session_t *session) {
    return send_finished_with_secret(session, session->server_handshake_traffic_secret);
}

/// @brief Constant-time byte comparison used for MAC and verify_data checks.
///
/// Walks all `n` bytes regardless of where they diverge so the
/// running time leaks no information about the position or count of
/// matching bytes. Required by H-9 to prevent timing side-channels
/// against the Finished MAC and any other secret-dependent compare.
/// @return 0 if `a` and `b` are byte-identical, non-zero otherwise.
static int ct_memcmp(const uint8_t *a, const uint8_t *b, size_t n) {
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++)
        diff |= a[i] ^ b[i];
    return diff != 0; // non-zero means unequal
}

/// @brief Validate the server's Finished verify_data against the transcript.
///
/// Recomputes HMAC-SHA256(finished_key, transcript_hash) using the
/// server handshake traffic secret, then compares it to the
/// received `data` in constant time. A mismatch means the handshake
/// has been tampered with or the server doesn't share our key
/// schedule.
/// @return RT_TLS_OK on match, RT_TLS_ERROR_HANDSHAKE on failure.
static int verify_finished_with_secret(
    rt_tls_session_t *session, const uint8_t base_secret[32], const uint8_t *data, size_t len) {
    if (len != 32) {
        session->error = "invalid Finished length";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    uint8_t finished_key[32];
    rt_hkdf_expand_label(base_secret, "finished", NULL, 0, finished_key, 32);

    uint8_t expected[32];
    rt_hmac_sha256(finished_key, 32, session->transcript_hash, 32, expected);

    if (ct_memcmp(data, expected, 32)) {
        session->error = "Finished verification failed";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    return RT_TLS_OK;
}

static int verify_finished(rt_tls_session_t *session, const uint8_t *data, size_t len) {
    return verify_finished_with_secret(session, session->server_handshake_traffic_secret, data, len);
}

static int verify_finished_server(rt_tls_session_t *session, const uint8_t *data, size_t len) {
    return verify_finished_with_secret(session, session->client_handshake_traffic_secret, data, len);
}

static int clienthello_offers_sig_scheme(
    const uint8_t *list, size_t list_len, uint16_t wanted_scheme) {
    if (list_len < 2 || (list_len & 1) != 0)
        return 0;
    for (size_t i = 0; i + 1 < list_len; i += 2) {
        if (read_u16(list + i) == wanted_scheme)
            return 1;
    }
    return 0;
}

static int clienthello_offers_alpn(
    const uint8_t *list, size_t list_len, const char *wanted_protocol) {
    size_t pos = 0;
    size_t wanted_len = wanted_protocol ? strlen(wanted_protocol) : 0;
    if (!wanted_protocol || wanted_len == 0)
        return 1;
    while (pos < list_len) {
        uint8_t one_len = list[pos++];
        if (pos + one_len > list_len)
            return 0;
        if (one_len == wanted_len && memcmp(list + pos, wanted_protocol, wanted_len) == 0)
            return 1;
        pos += one_len;
    }
    return 0;
}

static int parse_client_hello(rt_tls_session_t *session, const uint8_t *data, size_t len) {
    size_t pos = 0;
    int found_tls13 = 0;
    int found_key_share = 0;
    int found_sig_alg = 0;
    int offered_aes = 0;
    int offered_chacha = 0;
    int compression_ok = 0;
    uint16_t wanted_sig_scheme = 0;

    if (!session || !session->server_ctx) {
        return RT_TLS_ERROR_INVALID_ARG;
    }

    switch (session->server_ctx->key_type) {
        case TLS_SERVER_KEY_ECDSA_P256:
            wanted_sig_scheme = 0x0403;
            break;
        case TLS_SERVER_KEY_RSA_PSS_SHA256:
            wanted_sig_scheme = 0x0806;
            break;
        default:
            session->error = "TLS: server certificate key type is not configured";
            return RT_TLS_ERROR_HANDSHAKE;
    }

    if (len < 42) {
        session->error = "ClientHello too short";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    pos += 2; // legacy_version
    memcpy(session->client_random, data + pos, 32);
    pos += 32;

    session->legacy_session_id_len = data[pos++];
    if (session->legacy_session_id_len > sizeof(session->legacy_session_id) ||
        pos + session->legacy_session_id_len + 2 > len) {
        session->error = "ClientHello session id malformed";
        return RT_TLS_ERROR_HANDSHAKE;
    }
    memcpy(session->legacy_session_id, data + pos, session->legacy_session_id_len);
    pos += session->legacy_session_id_len;

    {
        uint16_t suites_len = read_u16(data + pos);
        pos += 2;
        if (pos + suites_len + 1 > len || suites_len < 2 || (suites_len & 1) != 0) {
            session->error = "ClientHello cipher_suites malformed";
            return RT_TLS_ERROR_HANDSHAKE;
        }
        for (size_t i = 0; i < suites_len; i += 2) {
            uint16_t suite = read_u16(data + pos + i);
            if (suite == TLS_AES_128_GCM_SHA256)
                offered_aes = 1;
            else if (suite == TLS_CHACHA20_POLY1305_SHA256)
                offered_chacha = 1;
        }
        pos += suites_len;
    }

    {
        uint8_t compression_len = data[pos++];
        if (pos + compression_len + 2 > len || compression_len == 0) {
            session->error = "ClientHello compression_methods malformed";
            return RT_TLS_ERROR_HANDSHAKE;
        }
        for (size_t i = 0; i < compression_len; i++) {
            if (data[pos + i] == 0)
                compression_ok = 1;
        }
        pos += compression_len;
    }

    if (!compression_ok) {
        session->error = "ClientHello missing null compression";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    {
        uint16_t ext_len = read_u16(data + pos);
        size_t ext_end;
        pos += 2;
        if (pos + ext_len != len) {
            session->error = "ClientHello extensions malformed";
            return RT_TLS_ERROR_HANDSHAKE;
        }
        ext_end = pos + ext_len;

        while (pos + 4 <= ext_end) {
            uint16_t ext_type = read_u16(data + pos);
            uint16_t one_len = read_u16(data + pos + 2);
            pos += 4;
            if (pos + one_len > ext_end) {
                session->error = "ClientHello extension truncated";
                return RT_TLS_ERROR_HANDSHAKE;
            }

            if (ext_type == TLS_EXT_SERVER_NAME) {
                int rc = tls_parse_client_sni(session, data + pos, one_len);
                if (rc != RT_TLS_OK)
                    return rc;
            } else if (ext_type == TLS_EXT_SUPPORTED_VERSIONS) {
                if (one_len < 3) {
                    session->error = "ClientHello supported_versions malformed";
                    return RT_TLS_ERROR_HANDSHAKE;
                }
                {
                    uint8_t list_len = data[pos];
                    if ((size_t)list_len + 1 != one_len || (list_len & 1) != 0) {
                        session->error = "ClientHello supported_versions length mismatch";
                        return RT_TLS_ERROR_HANDSHAKE;
                    }
                    for (size_t i = 0; i + 1 < list_len; i += 2) {
                        if (read_u16(data + pos + 1 + i) == TLS_VERSION_1_3)
                            found_tls13 = 1;
                    }
                }
            } else if (ext_type == TLS_EXT_SIGNATURE_ALGORITHMS) {
                if (one_len < 2) {
                    session->error = "ClientHello signature_algorithms malformed";
                    return RT_TLS_ERROR_HANDSHAKE;
                }
                {
                    uint16_t list_len = read_u16(data + pos);
                    if ((size_t)list_len + 2 != one_len) {
                        session->error = "ClientHello signature_algorithms length mismatch";
                        return RT_TLS_ERROR_HANDSHAKE;
                    }
                    if (session->server_ctx->key_type == TLS_SERVER_KEY_RSA_PSS_SHA256) {
                        if (clienthello_offers_sig_scheme(data + pos + 2, list_len, 0x0806))
                            session->server_sig_scheme = 0x0806;
                        else if (clienthello_offers_sig_scheme(data + pos + 2, list_len, 0x0805))
                            session->server_sig_scheme = 0x0805;
                        else if (clienthello_offers_sig_scheme(data + pos + 2, list_len, 0x0804))
                            session->server_sig_scheme = 0x0804;
                        else {
                            session->error = "ClientHello does not offer an RSA-PSS signature algorithm";
                            return RT_TLS_ERROR_HANDSHAKE;
                        }
                    } else if (clienthello_offers_sig_scheme(data + pos + 2, list_len, wanted_sig_scheme)) {
                        session->server_sig_scheme = wanted_sig_scheme;
                    } else {
                        session->error = "ClientHello does not offer ecdsa_secp256r1_sha256";
                        return RT_TLS_ERROR_HANDSHAKE;
                    }
                    found_sig_alg = 1;
                }
            } else if (ext_type == TLS_EXT_KEY_SHARE) {
                if (one_len < 2) {
                    session->error = "ClientHello key_share malformed";
                    return RT_TLS_ERROR_HANDSHAKE;
                }
                {
                    uint16_t list_len = read_u16(data + pos);
                    size_t share_pos = pos + 2;
                    size_t share_end = share_pos + list_len;
                    if ((size_t)list_len + 2 != one_len || share_end > ext_end) {
                        session->error = "ClientHello key_share length mismatch";
                        return RT_TLS_ERROR_HANDSHAKE;
                    }
                    while (share_pos + 4 <= share_end) {
                        uint16_t group = read_u16(data + share_pos);
                        uint16_t key_len = read_u16(data + share_pos + 2);
                        share_pos += 4;
                        if (share_pos + key_len > share_end) {
                            session->error = "ClientHello key_share truncated";
                            return RT_TLS_ERROR_HANDSHAKE;
                        }
                        if (group == 0x001D && key_len == 32) {
                            memcpy(session->server_public_key, data + share_pos, 32);
                            found_key_share = 1;
                        }
                        share_pos += key_len;
                    }
                }
            } else if (ext_type == TLS_EXT_ALPN && session->alpn_protocol[0] != '\0') {
                if (one_len < 3) {
                    session->error = "ClientHello ALPN malformed";
                    return RT_TLS_ERROR_HANDSHAKE;
                }
                {
                    uint16_t list_len = read_u16(data + pos);
                    if ((size_t)list_len + 2 != one_len ||
                        !clienthello_offers_alpn(data + pos + 2, list_len, session->alpn_protocol)) {
                        session->error = "ClientHello ALPN does not offer the configured protocol";
                        return RT_TLS_ERROR_HANDSHAKE;
                    }
                }
            }

            pos += one_len;
        }
    }

    if (!found_tls13) {
        session->error = "ClientHello does not offer TLS 1.3";
        return RT_TLS_ERROR_HANDSHAKE;
    }
    if (!found_sig_alg) {
        session->error = "ClientHello missing compatible signature algorithms";
        return RT_TLS_ERROR_HANDSHAKE;
    }
    if (!found_key_share) {
        session->error = "ClientHello missing X25519 key share";
        return RT_TLS_ERROR_HANDSHAKE;
    }
    if (session->hostname[0] != '\0' &&
        !tls_server_name_matches_leaf_cert(session->server_ctx, session->hostname)) {
        session->error = "ClientHello SNI does not match the configured certificate";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    if (offered_aes)
        session->cipher_suite = TLS_AES_128_GCM_SHA256;
    else if (offered_chacha)
        session->cipher_suite = TLS_CHACHA20_POLY1305_SHA256;
    else {
        session->error = "ClientHello does not offer a supported TLS 1.3 cipher suite";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    return RT_TLS_OK;
}

static int send_server_hello(rt_tls_session_t *session) {
    uint8_t body[256];
    uint8_t hs[4 + 256];
    size_t pos = 0;
    uint8_t shared_secret[32];

    write_u16(body + pos, TLS_VERSION_1_2);
    pos += 2;
    rt_crypto_random_bytes(session->server_random, sizeof(session->server_random));
    memcpy(body + pos, session->server_random, sizeof(session->server_random));
    pos += sizeof(session->server_random);

    body[pos++] = (uint8_t)session->legacy_session_id_len;
    memcpy(body + pos, session->legacy_session_id, session->legacy_session_id_len);
    pos += session->legacy_session_id_len;

    write_u16(body + pos, session->cipher_suite);
    pos += 2;
    body[pos++] = 0;

    {
        size_t ext_start = pos;
        pos += 2;
        rt_x25519_keygen(session->client_private_key, session->client_public_key);

        write_u16(body + pos, TLS_EXT_SUPPORTED_VERSIONS);
        pos += 2;
        write_u16(body + pos, 2);
        pos += 2;
        write_u16(body + pos, TLS_VERSION_1_3);
        pos += 2;

        write_u16(body + pos, TLS_EXT_KEY_SHARE);
        pos += 2;
        write_u16(body + pos, 36);
        pos += 2;
        write_u16(body + pos, 0x001D);
        pos += 2;
        write_u16(body + pos, 32);
        pos += 2;
        memcpy(body + pos, session->client_public_key, 32);
        pos += 32;

        write_u16(body + ext_start, (uint16_t)(pos - ext_start - 2));
    }

    hs[0] = TLS_HS_SERVER_HELLO;
    write_u24(hs + 1, (uint32_t)pos);
    memcpy(hs + 4, body, pos);

    if (transcript_update(session, hs, 4 + pos) != 0)
        return RT_TLS_ERROR_HANDSHAKE;

    rt_x25519(session->client_private_key, session->server_public_key, shared_secret);

    // ServerHello itself is still plaintext in TLS 1.3. Switch to handshake
    // traffic keys only after the record has been transmitted.
    {
        int rc = send_record(session, TLS_CONTENT_HANDSHAKE, hs, 4 + pos);
        if (rc != RT_TLS_OK)
            return rc;
    }

    derive_handshake_keys_server(session, shared_secret);
    return RT_TLS_OK;
}

static int send_encrypted_extensions_server(rt_tls_session_t *session) {
    uint8_t body[256];
    uint8_t hs[4 + 256];
    size_t pos = 0;
    size_t ext_start = 0;

    ext_start = pos;
    pos += 2;
    if (session->alpn_protocol[0] != '\0') {
        size_t proto_len = strlen(session->alpn_protocol);
        write_u16(body + pos, TLS_EXT_ALPN);
        pos += 2;
        write_u16(body + pos, (uint16_t)(proto_len + 3));
        pos += 2;
        write_u16(body + pos, (uint16_t)(proto_len + 1));
        pos += 2;
        body[pos++] = (uint8_t)proto_len;
        memcpy(body + pos, session->alpn_protocol, proto_len);
        pos += proto_len;
    }
    write_u16(body + ext_start, (uint16_t)(pos - ext_start - 2));

    hs[0] = TLS_HS_ENCRYPTED_EXTENSIONS;
    write_u24(hs + 1, (uint32_t)pos);
    memcpy(hs + 4, body, pos);
    if (transcript_update(session, hs, 4 + pos) != 0)
        return RT_TLS_ERROR_HANDSHAKE;
    return send_record(session, TLS_CONTENT_HANDSHAKE, hs, 4 + pos);
}

static int send_certificate_server(rt_tls_session_t *session) {
    size_t body_len = 1 + 3 + session->server_ctx->cert_list_entries_len;
    uint8_t *hs = (uint8_t *)malloc(4 + body_len);
    if (!hs) {
        session->error = "TLS: failed to allocate Certificate message";
        return RT_TLS_ERROR_MEMORY;
    }

    hs[0] = TLS_HS_CERTIFICATE;
    write_u24(hs + 1, (uint32_t)body_len);
    hs[4] = 0;
    write_u24(hs + 5, (uint32_t)session->server_ctx->cert_list_entries_len);
    memcpy(hs + 8,
           session->server_ctx->cert_list_entries,
           session->server_ctx->cert_list_entries_len);

    if (transcript_update(session, hs, 4 + body_len) != 0) {
        free(hs);
        return RT_TLS_ERROR_HANDSHAKE;
    }

    {
        int rc = send_record(session, TLS_CONTENT_HANDSHAKE, hs, 4 + body_len);
        free(hs);
        return rc;
    }
}

static size_t encode_ecdsa_signature_der(
    const uint8_t r[32], const uint8_t s[32], uint8_t out[80]) {
    const uint8_t *r_ptr = r;
    const uint8_t *s_ptr = s;
    size_t r_len = 32;
    size_t s_len = 32;
    size_t pos = 0;
    int r_pad = 0;
    int s_pad = 0;

    while (r_len > 1 && *r_ptr == 0) {
        r_ptr++;
        r_len--;
    }
    while (s_len > 1 && *s_ptr == 0) {
        s_ptr++;
        s_len--;
    }
    if (r_ptr[0] & 0x80)
        r_pad = 1;
    if (s_ptr[0] & 0x80)
        s_pad = 1;

    out[pos++] = 0x30;
    out[pos++] = (uint8_t)(2 + r_pad + r_len + 2 + s_pad + s_len);
    out[pos++] = 0x02;
    out[pos++] = (uint8_t)(r_pad + r_len);
    if (r_pad)
        out[pos++] = 0x00;
    memcpy(out + pos, r_ptr, r_len);
    pos += r_len;
    out[pos++] = 0x02;
    out[pos++] = (uint8_t)(s_pad + s_len);
    if (s_pad)
        out[pos++] = 0x00;
    memcpy(out + pos, s_ptr, s_len);
    pos += s_len;
    return pos;
}

static void build_server_cert_verify_message(
    const uint8_t transcript_hash[32], uint8_t out_content[130]) {
    static const char context_str[] = "TLS 1.3, server CertificateVerify";
    memset(out_content, 0x20, 64);
    memcpy(out_content + 64, context_str, 33);
    out_content[97] = 0x00;
    memcpy(out_content + 98, transcript_hash, 32);
}

static int send_certificate_verify_server(rt_tls_session_t *session) {
    uint8_t content[130];
    uint8_t digest[32];
    uint8_t sig_r[32];
    uint8_t sig_s[32];
    uint8_t sig_buf[1024];
    uint8_t hs[4 + 2 + 2 + 1024];
    size_t sig_der_len = 0;

    build_server_cert_verify_message(session->transcript_hash, content);
    rt_sha256(content, sizeof(content), digest);

    if (!session || !session->server_ctx || session->server_sig_scheme == 0) {
        session->error = "TLS: server signature scheme not negotiated";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    if (session->server_ctx->key_type == TLS_SERVER_KEY_ECDSA_P256) {
        if (!ecdsa_p256_sign(session->server_ctx->private_key, digest, sig_r, sig_s)) {
            session->error = "TLS: failed to sign CertificateVerify";
            return RT_TLS_ERROR_HANDSHAKE;
        }
        sig_der_len = encode_ecdsa_signature_der(sig_r, sig_s, sig_buf);
    } else if (session->server_ctx->key_type == TLS_SERVER_KEY_RSA_PSS_SHA256) {
        rt_rsa_hash_t hash_id = RT_RSA_HASH_SHA256;
        uint8_t hashed_content[64];
        size_t hash_len = 32;

        switch (session->server_sig_scheme) {
            case 0x0805:
                hash_id = RT_RSA_HASH_SHA384;
                rt_sha384(content, sizeof(content), hashed_content);
                hash_len = 48;
                break;
            case 0x0806:
                hash_id = RT_RSA_HASH_SHA512;
                rt_sha512(content, sizeof(content), hashed_content);
                hash_len = 64;
                break;
            case 0x0804:
            default:
                hash_id = RT_RSA_HASH_SHA256;
                memcpy(hashed_content, digest, sizeof(digest));
                hash_len = 32;
                break;
        }
        sig_der_len = sizeof(sig_buf);
        if (!rt_rsa_pss_sign(
                &session->server_ctx->rsa_key,
                hash_id,
                hashed_content,
                hash_len,
                sig_buf,
                &sig_der_len)) {
            session->error = "TLS: failed to sign CertificateVerify";
            return RT_TLS_ERROR_HANDSHAKE;
        }
    } else {
        session->error = "TLS: failed to sign CertificateVerify";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    hs[0] = TLS_HS_CERTIFICATE_VERIFY;
    write_u24(hs + 1, (uint32_t)(4 + sig_der_len));
    write_u16(hs + 4, session->server_sig_scheme);
    write_u16(hs + 6, (uint16_t)sig_der_len);
    memcpy(hs + 8, sig_buf, sig_der_len);

    if (transcript_update(session, hs, 8 + sig_der_len) != 0)
        return RT_TLS_ERROR_HANDSHAKE;
    return send_record(session, TLS_CONTENT_HANDSHAKE, hs, 8 + sig_der_len);
}

rt_tls_session_t *rt_tls_server_accept_socket(int socket_fd, const rt_tls_server_ctx_t *ctx) {
    rt_tls_session_t *session = NULL;
    uint8_t content_type = 0;
    uint8_t data[TLS_MAX_RECORD_SIZE];
    size_t data_len = 0;

    tls_set_server_last_error_msg(NULL);

    if (socket_fd < 0 || !ctx) {
        tls_set_server_last_error_msg("TLS server: invalid socket or context");
        return NULL;
    }

    session = rt_tls_new(socket_fd, NULL);
    if (!session) {
        tls_set_server_last_error_msg("TLS server: failed to allocate session");
        CLOSE_SOCKET((socket_t)socket_fd);
        return NULL;
    }
    session->is_server = 1;
    session->server_ctx = ctx;
    session->verify_cert = 0;
    session->timeout_ms = ctx->timeout_ms > 0 ? ctx->timeout_ms : 30000;
    if (ctx->alpn_protocol[0] != '\0')
        strncpy(session->alpn_protocol, ctx->alpn_protocol, sizeof(session->alpn_protocol) - 1);
    tls_set_socket_timeout((socket_t)session->socket_fd, session->timeout_ms, 1);
    tls_set_socket_timeout((socket_t)session->socket_fd, session->timeout_ms, 0);

    while (1) {
        size_t pos = 0;
        int rc = recv_record(session, &content_type, data, &data_len);
        if (rc != RT_TLS_OK)
            goto fail;
        if (content_type == TLS_CONTENT_CHANGE_CIPHER)
            continue;
        if (content_type != TLS_CONTENT_HANDSHAKE) {
            session->error = "TLS: expected ClientHello";
            goto fail;
        }

        while (pos + 4 <= data_len) {
            uint8_t hs_type = data[pos];
            uint32_t hs_len = read_u24(data + pos + 1);
            if (pos + 4 + hs_len > data_len) {
                session->error = "TLS: incomplete ClientHello record";
                goto fail;
            }
            if (hs_type != TLS_HS_CLIENT_HELLO) {
                session->error = "TLS: unexpected handshake message before ClientHello";
                goto fail;
            }
            if (parse_client_hello(session, data + pos + 4, hs_len) != RT_TLS_OK)
                goto fail;
            if (transcript_update(session, data + pos, 4 + hs_len) != 0)
                goto fail;

            if (send_server_hello(session) != RT_TLS_OK || send_encrypted_extensions_server(session) != RT_TLS_OK ||
                send_certificate_server(session) != RT_TLS_OK ||
                send_certificate_verify_server(session) != RT_TLS_OK ||
                send_finished_server(session) != RT_TLS_OK) {
                goto fail;
            }

            derive_application_secrets(session);
            install_server_application_write_keys(session);

            // Expect the client's Finished under the handshake read keys.
            while (1) {
                rc = recv_record(session, &content_type, data, &data_len);
                if (rc != RT_TLS_OK)
                    goto fail;
                if (content_type == TLS_CONTENT_CHANGE_CIPHER)
                    continue;
                if (content_type == TLS_CONTENT_ALERT) {
                    session->error = "TLS: received alert during handshake";
                    goto fail;
                }
                if (content_type != TLS_CONTENT_HANDSHAKE) {
                    session->error = "TLS: expected client Finished";
                    goto fail;
                }
                if (data_len < 4 || data[0] != TLS_HS_FINISHED ||
                    read_u24(data + 1) + 4 != data_len) {
                    session->error = "TLS: malformed client Finished";
                    goto fail;
                }
                if (verify_finished_server(session, data + 4, data_len - 4) != RT_TLS_OK)
                    goto fail;
                if (transcript_update(session, data, data_len) != 0)
                    goto fail;
                install_server_application_read_keys(session);
                session->state = TLS_STATE_CONNECTED;
                tls_set_server_last_error_msg(NULL);
                return session;
            }
        }
    }

fail:
    tls_set_server_last_error_msg(rt_tls_get_error(session));
    rt_tls_close(session);
    return NULL;
}

// Certificate validation functions (tls_parse_certificate_msg, tls_verify_hostname,
// tls_verify_chain, tls_verify_cert_verify) are implemented in rt_tls_verify.c.

//=============================================================================
// Public API
//=============================================================================

/// @brief Initialise a TLS config struct to safe defaults.
///
/// Zeros the structure, enables certificate validation
/// (chain + hostname + CertificateVerify), and sets a 30-second I/O
/// timeout. Callers may then override fields like `hostname` or
/// `verify_cert` before passing the config to `rt_tls_new`.
void rt_tls_config_init(rt_tls_config_t *config) {
    memset(config, 0, sizeof(*config));
    // CS-6 resolved: certificate validation now implemented (CS-1/CS-2/CS-3).
    // verify_cert=1 enables full chain validation + hostname verification + CertVerify.
    config->verify_cert = 1;
    config->timeout_ms = 30000;
}

/// @brief Allocate a fresh TLS session bound to an already-connected socket.
///
/// The session is heap-allocated through the GC (`rt_obj_new_i64`)
/// so it participates in the runtime's lifecycle. State is reset to
/// `TLS_STATE_INITIAL`, the transcript hash is primed for an empty
/// handshake, and the optional hostname is copied into a
/// fixed-size buffer for SNI / hostname verification.
/// @param socket_fd  Underlying TCP socket file descriptor.
/// @param config     Optional config; if NULL, defaults match `rt_tls_config_init`.
/// @return Pointer to the new session (never NULL — GC alloc cannot fail-soft).
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
    if (config && config->alpn_protocol) {
        strncpy(session->alpn_protocol, config->alpn_protocol, sizeof(session->alpn_protocol) - 1);
    }
    if (config && config->ca_file) {
        strncpy(session->ca_file, config->ca_file, sizeof(session->ca_file) - 1);
    }

    return session;
}

/// @brief Drive the TLS 1.3 handshake to completion.
///
/// Sends ClientHello, then loops reading records and dispatching
/// each parsed handshake message:
///   - ServerHello / HelloRetryRequest → key derivation
///   - EncryptedExtensions             → noted, advance state
///   - Certificate                     → parse + (optional) chain & hostname checks
///   - CertificateVerify               → (optional) signature check
///   - Finished                        → MAC verify, derive app keys, send our Finished
/// Updates the transcript hash before processing each message
/// (RFC 8446 §4.4.1) and aborts on transcript overflow. A
/// post-handshake state of `TLS_STATE_CONNECTED` indicates success.
/// @return RT_TLS_OK on completed handshake, an `RT_TLS_ERROR_*`
///         code otherwise; `session->error` carries a human message.
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

            if (hs_type != TLS_HS_FINISHED &&
                transcript_update(session, data + pos, 4 + hs_len) != 0) {
                return RT_TLS_ERROR_HANDSHAKE;
            }

            const uint8_t *hs_data = data + pos + 4;

            switch (hs_type) {
                case TLS_HS_SERVER_HELLO:
                    rc = process_server_hello(session, hs_data, hs_len, data + pos, 4 + hs_len);
                    if (rc != RT_TLS_OK)
                        return rc;
                    break;

                case TLS_HS_ENCRYPTED_EXTENSIONS:
                    rc = process_encrypted_extensions(session, hs_data, hs_len);
                    if (rc != RT_TLS_OK)
                        return rc;
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

                    if (transcript_update(session, data + pos, 4 + hs_len) != 0)
                        return RT_TLS_ERROR_HANDSHAKE;

                    // TLS 1.3 transitions read keys after server Finished, but
                    // the client Finished itself is still sent under handshake keys.
                    derive_application_secrets(session);
                    install_client_application_read_keys(session);

                    rc = send_finished(session);
                    if (rc != RT_TLS_OK)
                        return rc;

                    install_client_application_write_keys(session);

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

/// @brief Encrypt and send `len` bytes of application data.
///
/// Splits the payload into TLS records of at most
/// `TLS_MAX_RECORD_SIZE` bytes (16 KiB per RFC 8446 §5.1), each
/// AEAD-sealed with the active write key. Refuses if the session
/// is not in the `CONNECTED` state. A `len == 0` call is a no-op
/// that returns 0 without touching the wire.
/// @return Number of bytes accepted (always equals `len` on success)
///         or a negative `RT_TLS_ERROR_*` code on failure.
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

/// @brief Receive up to `len` decrypted application bytes.
///
/// First drains any leftover bytes from a previous record (the
/// per-session `app_buffer`) before pulling a new record off the
/// wire. Transparently skips post-handshake messages such as
/// `NewSessionTicket` and `KeyUpdate` (capped at 100 retries to
/// foil a malicious server that floods them). A received `Alert`
/// transitions the session to `CLOSED` and returns 0 (EOF).
/// @return Bytes copied into `buffer` (>0), 0 on clean close,
///         or a negative `RT_TLS_ERROR_*` code on failure.
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

    if (content_type == TLS_CONTENT_HANDSHAKE) {
        size_t pos = 0;
        while (pos + 4 <= data_len) {
            uint8_t hs_type = session->app_buffer[pos];
            uint32_t hs_len = read_u24(session->app_buffer + pos + 1);
            if (pos + 4 + hs_len > data_len) {
                session->error = "TLS: incomplete post-handshake message";
                return RT_TLS_ERROR_HANDSHAKE;
            }
            rc = process_post_handshake_message(
                session, hs_type, session->app_buffer + pos + 4, hs_len);
            if (rc != RT_TLS_OK)
                return rc;
            pos += 4 + hs_len;
        }
        if (pos != data_len) {
            session->error = "TLS: malformed post-handshake record";
            return RT_TLS_ERROR_HANDSHAKE;
        }
    } else if (content_type != TLS_CONTENT_APPLICATION) {
        // Ignore non-application records we don't use (e.g. CCS compatibility records).
    }

    if (content_type != TLS_CONTENT_APPLICATION) {
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

/// @brief Politely shut down a TLS session and release its resources.
///
/// If still connected, sends a `close_notify` warning alert and
/// drains records (capped at 32) until the peer's matching alert
/// arrives — required by RFC 5246 §7.2.1 to detect truncation
/// attacks. Then frees per-session scratch state, closes the
/// underlying socket, marks the session `CLOSED`, and decrements
/// its GC refcount; if the count hits zero, the session memory is
/// freed via `rt_obj_free`. Safe to call on NULL or an already
/// closed session.
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

/// @brief Last error message recorded by the session.
/// @return The most recent error string, "null session" if `session`
///         is NULL, or "no error" if no error has been recorded.
const char *rt_tls_get_error(rt_tls_session_t *session) {
    if (!session)
        return "null session";
    return session->error ? session->error : "no error";
}

/// @brief Test whether `rt_tls_recv` can satisfy a read without going to the wire.
/// @return 1 if the per-session app buffer holds undelivered bytes, 0 otherwise.
int rt_tls_has_buffered_data(rt_tls_session_t *session) {
    if (!session)
        return 0;
    return session->app_buffer_pos < session->app_buffer_len ? 1 : 0;
}

/// @brief Expose the underlying socket descriptor (for select/poll integration).
/// @return The socket FD, or -1 if `session` is NULL.
int rt_tls_get_socket(rt_tls_session_t *session) {
    if (!session)
        return -1;
    return (int)session->socket_fd;
}

/// @brief One-shot TCP connect + TLS handshake to `host:port`.
///
/// Resolves `host` via `getaddrinfo` (AF_UNSPEC accepts both IPv4
/// and IPv6), iterates the candidate addresses connecting to the
/// first that responds within the configured timeout
/// (non-blocking + `select`), applies recv/send timeouts to the
/// socket, then runs the TLS handshake. On any failure the socket
/// is closed and NULL is returned. Initialises Winsock once on
/// Windows. The host name is forwarded into the config for SNI and
/// hostname verification regardless of the caller-provided `hostname`.
/// @return Connected `rt_tls_session_t*` on success, NULL on
///         resolution / connect / handshake failure.
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
    tls_set_last_error_msg(NULL);
    if (!cfg.hostname)
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
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        tls_set_last_error_msg("TLS: host resolution failed");
        return NULL;
    }

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
    {
        tls_set_last_error_msg("TLS: TCP connect failed");
        return NULL;
    }

    if (cfg.timeout_ms > 0) {
        tls_set_socket_timeout(sock, cfg.timeout_ms, 1);
        tls_set_socket_timeout(sock, cfg.timeout_ms, 0);
    }

    rt_tls_session_t *session = rt_tls_new((int)sock, &cfg);
    if (!session) {
        CLOSE_SOCKET(sock);
        tls_set_last_error_msg("TLS: failed to allocate session");
        return NULL;
    }

    // Perform handshake
    if (rt_tls_handshake(session) != RT_TLS_OK) {
        tls_set_last_error_msg(rt_tls_get_error(session));
        rt_tls_close(session);
        return NULL;
    }

    tls_set_last_error_msg(NULL);
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
