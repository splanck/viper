/**
 * @file tls.h
 * @brief User-space TLS 1.3 client library for ViperOS.
 *
 * Provides TLS 1.3 client functionality using ChaCha20-Poly1305 AEAD
 * and X25519 key exchange. Uses libc sockets (routed through netd).
 */

#ifndef VIPER_TLS_H
#define VIPER_TLS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TLS error codes */
#define TLS_OK 0
#define TLS_ERROR -1
#define TLS_ERROR_SOCKET -2
#define TLS_ERROR_HANDSHAKE -3
#define TLS_ERROR_CERTIFICATE -4
#define TLS_ERROR_CLOSED -5
#define TLS_ERROR_TIMEOUT -6
#define TLS_ERROR_MEMORY -7
#define TLS_ERROR_INVALID_ARG -8

/* TLS session handle (opaque) */
typedef struct tls_session tls_session_t;

/* TLS configuration */
typedef struct tls_config
{
    const char *hostname;  /* Server hostname for SNI and verification */
    int verify_cert;       /* 1 = verify certificate chain (default), 0 = skip */
    int timeout_ms;        /* Connection/handshake timeout in ms (0 = default) */
} tls_config_t;

/* TLS session info */
typedef struct tls_info
{
    uint16_t protocol_version; /* e.g., 0x0304 for TLS 1.3 */
    uint16_t cipher_suite;     /* Negotiated cipher suite */
    int verified;              /* 1 if certificate verified */
    int connected;             /* 1 if session is active */
    char hostname[128];        /* SNI hostname */
} tls_info_t;

/**
 * @brief Initialize default TLS configuration.
 * @param config Configuration to initialize.
 */
void tls_config_init(tls_config_t *config);

/**
 * @brief Create a new TLS session over an existing socket.
 * @param socket_fd Connected TCP socket file descriptor.
 * @param config TLS configuration (may be NULL for defaults).
 * @return New TLS session or NULL on error.
 */
tls_session_t *tls_new(int socket_fd, const tls_config_t *config);

/**
 * @brief Perform TLS 1.3 handshake.
 * @param session TLS session.
 * @return TLS_OK on success, negative error code on failure.
 */
int tls_handshake(tls_session_t *session);

/**
 * @brief Send data over TLS connection.
 * @param session TLS session.
 * @param data Data to send.
 * @param len Length of data.
 * @return Bytes sent on success, negative error code on failure.
 */
long tls_send(tls_session_t *session, const void *data, size_t len);

/**
 * @brief Receive data from TLS connection.
 * @param session TLS session.
 * @param buffer Buffer to receive data.
 * @param len Maximum bytes to receive.
 * @return Bytes received on success, 0 on EOF, negative error code on failure.
 */
long tls_recv(tls_session_t *session, void *buffer, size_t len);

/**
 * @brief Close TLS session and free resources.
 * @param session TLS session.
 */
void tls_close(tls_session_t *session);

/**
 * @brief Get TLS session information.
 * @param session TLS session.
 * @param info Output info structure.
 * @return TLS_OK on success, negative error code on failure.
 */
int tls_get_info(tls_session_t *session, tls_info_t *info);

/**
 * @brief Get error message for last error.
 * @param session TLS session.
 * @return Error message string (static or session-owned).
 */
const char *tls_get_error(tls_session_t *session);

/**
 * @brief Convenience function: connect, handshake, and return session.
 * @param host Hostname to connect to.
 * @param port Port number.
 * @param config TLS configuration (may be NULL).
 * @return Connected TLS session or NULL on error.
 */
tls_session_t *tls_connect(const char *host, uint16_t port, const tls_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* VIPER_TLS_H */
