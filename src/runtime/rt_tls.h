//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file rt_tls.h
/// @brief TLS 1.3 client for secure HTTPS connections.
///
/// Implements TLS 1.3 using ChaCha20-Poly1305 AEAD and X25519 key exchange.
//
//===----------------------------------------------------------------------===//

#ifndef VIPER_RT_TLS_H
#define VIPER_RT_TLS_H

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
    rt_tls_session_t *rt_tls_connect(const char *host, uint16_t port, const rt_tls_config_t *config);

    /// @brief Get underlying socket file descriptor.
    int rt_tls_get_socket(rt_tls_session_t *session);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_TLS_H
