//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_tls.h
// Purpose: TLS 1.3 client for secure HTTPS connections using ChaCha20-Poly1305 AEAD and X25519 key exchange, implemented in pure C without external TLS libraries.
//
// Key invariants:
//   - Implements TLS 1.3 handshake with X25519 key exchange.
//   - Uses ChaCha20-Poly1305 for all record encryption.
//   - Certificate verification is performed against the system trust store.
//   - Only client mode is supported; server-side TLS is not implemented.
//
// Ownership/Lifetime:
//   - TLS connection objects are heap-allocated; caller must close and free.
//   - Returned data buffers are newly allocated; caller must release.
//
// Links: src/runtime/network/rt_tls.c (implementation), src/runtime/network/rt_crypto.h, src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#ifndef VIPER_RT_TLS_H
#define VIPER_RT_TLS_H

#include "rt_string.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// TLS error codes
#define RT_TLS_OK 0
#define RT_TLS_ERROR -1
#define RT_TLS_ERROR_SOCKET -2
#define RT_TLS_ERROR_HANDSHAKE -3
#define RT_TLS_ERROR_CERTIFICATE -4
#define RT_TLS_ERROR_CLOSED -5
#define RT_TLS_ERROR_TIMEOUT -6
#define RT_TLS_ERROR_MEMORY -7
#define RT_TLS_ERROR_INVALID_ARG -8

    /// @brief Opaque TLS session handle.
    typedef struct rt_tls_session rt_tls_session_t;

    /// @brief TLS configuration.
    typedef struct rt_tls_config
    {
        const char *hostname; // Server hostname for SNI
        int verify_cert;      // 1 = verify certificate (default), 0 = skip
        int timeout_ms;       // Connection timeout in ms (0 = default 30s)
    } rt_tls_config_t;

    /// @brief Initialize default TLS configuration.
    void rt_tls_config_init(rt_tls_config_t *config);

    /// @brief Create TLS session over existing socket.
    /// @param socket_fd Connected TCP socket.
    /// @param config TLS configuration (NULL for defaults).
    /// @return TLS session or NULL on error.
    rt_tls_session_t *rt_tls_new(int socket_fd, const rt_tls_config_t *config);

    /// @brief Perform TLS 1.3 handshake.
    /// @return RT_TLS_OK on success, negative error code on failure.
    int rt_tls_handshake(rt_tls_session_t *session);

    /// @brief Send data over TLS connection.
    /// @return Bytes sent on success, negative error code on failure.
    long rt_tls_send(rt_tls_session_t *session, const void *data, size_t len);

    /// @brief Receive data from TLS connection.
    /// @return Bytes received on success, 0 on EOF, negative on failure.
    long rt_tls_recv(rt_tls_session_t *session, void *buffer, size_t len);

    /// @brief Close TLS session and free resources.
    void rt_tls_close(rt_tls_session_t *session);

    /// @brief Get last error message.
    const char *rt_tls_get_error(rt_tls_session_t *session);

    /// @brief Convenience: connect, handshake, return session.
    /// @param host Hostname to connect to.
    /// @param port Port number.
    /// @param config TLS configuration (NULL for defaults).
    /// @return Connected TLS session or NULL on error.
    rt_tls_session_t *rt_tls_connect(const char *host,
                                     uint16_t port,
                                     const rt_tls_config_t *config);

    /// @brief Get underlying socket file descriptor.
    int rt_tls_get_socket(rt_tls_session_t *session);

    //=========================================================================
    // Viper API wrappers (Viper.Crypto.Tls)
    //=========================================================================

    /// @brief Connect to host:port with TLS, default timeout.
    void *rt_viper_tls_connect(rt_string host, int64_t port);

    /// @brief Connect to host:port with TLS, custom timeout.
    void *rt_viper_tls_connect_for(rt_string host, int64_t port, int64_t timeout_ms);

    /// @brief Get connected hostname.
    rt_string rt_viper_tls_host(void *obj);

    /// @brief Get connected port.
    int64_t rt_viper_tls_port(void *obj);

    /// @brief Check if connection is open.
    int8_t rt_viper_tls_is_open(void *obj);

    /// @brief Send Bytes data over TLS.
    int64_t rt_viper_tls_send(void *obj, void *data);

    /// @brief Send String data over TLS.
    int64_t rt_viper_tls_send_str(void *obj, rt_string text);

    /// @brief Receive up to max_bytes as Bytes.
    void *rt_viper_tls_recv(void *obj, int64_t max_bytes);

    /// @brief Receive up to max_bytes as String.
    rt_string rt_viper_tls_recv_str(void *obj, int64_t max_bytes);

    /// @brief Close the TLS connection.
    void rt_viper_tls_close(void *obj);

    /// @brief Get last error message.
    rt_string rt_viper_tls_error(void *obj);

    //=========================================================================
    // Internal functions exposed for unit testing (CS-1/CS-2/CS-3)
    //=========================================================================

    /// @brief Match a hostname pattern against a target hostname (RFC 6125).
    /// @return 1 if match, 0 otherwise.
    int tls_match_hostname(const char *pattern, const char *hostname);

    /// @brief Extract SubjectAltName DNS names from a certificate DER.
    /// @return Number of names found.
    int tls_extract_san_names(const uint8_t *der, size_t der_len,
                               char san_out[][256], int max_names);

    /// @brief Extract CommonName from a certificate DER Subject.
    /// @return 1 if found, 0 otherwise.
    int tls_extract_cn(const uint8_t *der, size_t der_len, char cn_out[256]);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_TLS_H
