#pragma once

/**
 * @file tls.hpp
 * @brief TLS 1.3 client session API for ViperOS.
 *
 * @details
 * This header defines the public interface for establishing and using a TLS 1.3
 * session over an existing TCP connection. The implementation targets
 * early-stage ViperOS networking (v0.2.0) and is intentionally focused on a
 * narrow set of features needed for HTTPS-style clients:
 *
 * - TLS 1.3 handshake with X25519 key exchange.
 * - ChaCha20-Poly1305 record protection.
 * - SHA-256 transcript hashing and HKDF-based key schedule.
 * - Basic X.509 parsing and optional certificate chain verification.
 *
 * This is not a production-quality TLS stack. Some verification steps are
 * simplified during bring-up (e.g., partial CertificateVerify/Finished handling)
 * and the supported cipher suites/extensions are limited.
 */

#include "../../include/types.hpp"
#include "crypto/sha256.hpp"
#include "crypto/x25519.hpp"
#include "record.hpp"

namespace viper::tls
{

/**
 * @brief TLS handshake message types.
 *
 * @details
 * Values correspond to the TLS handshake type byte as defined by RFC 8446.
 * The client uses these to parse and dispatch inbound handshake messages.
 */
enum class HandshakeType : u8
{
    ClientHello = 1,
    ServerHello = 2,
    NewSessionTicket = 4,
    EndOfEarlyData = 5,
    EncryptedExtensions = 8,
    Certificate = 11,
    CertificateRequest = 13,
    CertificateVerify = 15,
    Finished = 20,
    KeyUpdate = 24,
    MessageHash = 254,
};

/**
 * @brief TLS extension identifiers.
 *
 * @details
 * Used in ClientHello/ServerHello extensions. Only a subset required for the
 * current TLS 1.3 client implementation is listed here.
 */
enum class ExtensionType : u16
{
    ServerName = 0,
    SupportedGroups = 10,
    SignatureAlgorithms = 13,
    SupportedVersions = 43,
    KeyShare = 51,
};

/**
 * @brief High-level TLS session state.
 *
 * @details
 * Tracks progress through the TLS 1.3 handshake and whether application data
 * is available. The handshake loop advances this state as messages are sent and
 * received.
 */
enum class TlsState
{
    Initial,
    ClientHelloSent,
    ServerHelloReceived,
    WaitEncryptedExtensions,
    WaitCertificate,
    WaitCertificateVerify,
    WaitFinished,
    Connected,
    Error,
    Closed,
};

/**
 * @brief Configuration for a TLS session.
 *
 * @details
 * Configuration controls optional SNI and whether certificate verification is
 * performed.
 */
struct TlsConfig
{
    const char *hostname;     /**< Hostname used for SNI and hostname verification (may be null). */
    bool verify_certificates; /**< Whether to verify the server certificate chain. */
};

/**
 * @brief TLS 1.3 session state.
 *
 * @details
 * Stores all state required to perform a TLS 1.3 handshake and to protect
 * application data records:
 * - Underlying transport socket and record layer state.
 * - Handshake transcript hash and derived secrets.
 * - Ephemeral key exchange key pair (X25519).
 * - Cached server certificate key material used for CertificateVerify.
 *
 * The structure is designed to be stack-allocatable and does not perform
 * dynamic allocations.
 */
struct TlsSession
{
    // Socket
    i32 socket_fd; /**< Underlying connected TCP socket. */

    // Record layer
    RecordLayer record; /**< Record layer (framing + AEAD). */

    // Session state
    TlsState state;    /**< Current session state. */
    const char *error; /**< Last error message (may be null). */

    // Configuration
    TlsConfig config; /**< Session configuration (SNI, verification). */

    // Key exchange
    u8 client_private_key[32]; /**< Ephemeral X25519 private key (clamped). */
    u8 client_public_key[32];  /**< Ephemeral X25519 public key. */
    u8 server_public_key[32];  /**< Server's X25519 public key from ServerHello. */

    // Handshake transcript hash
    crypto::Sha256Context transcript; /**< SHA-256 transcript context (handshake messages). */

    // Derived secrets
    u8 handshake_secret[32];
    u8 client_handshake_traffic_secret[32];
    u8 server_handshake_traffic_secret[32];
    u8 master_secret[32];
    u8 client_application_traffic_secret[32];
    u8 server_application_traffic_secret[32];

    // Random values
    u8 client_random[32]; /**< ClientHello random value. */
    u8 server_random[32]; /**< ServerHello random value. */

    // Selected cipher suite
    CipherSuite cipher_suite; /**< Cipher suite selected by the server. */

    // Server certificate info (for CertificateVerify)
    bool cert_verified;            /**< Whether the certificate chain was verified successfully. */
    u8 server_cert_key_type;       /**< 0=RSA, 1=ECDSA/other (limited support). */
    const u8 *server_cert_modulus; /**< RSA modulus pointer (points into @ref server_cert_data). */
    usize server_cert_modulus_len;
    const u8
        *server_cert_exponent; /**< RSA exponent pointer (points into @ref server_cert_data). */
    usize server_cert_exponent_len;

    // Buffer for storing raw certificate data
    u8 server_cert_data[4096];
    usize server_cert_data_len;
};

/**
 * @brief Initialize a TLS session structure over an existing TCP socket.
 *
 * @details
 * Initializes the record layer, generates an ephemeral X25519 key pair and the
 * client random value, and prepares the handshake transcript hash.
 *
 * This function does not perform any network I/O. Call @ref tls_handshake to
 * perform the TLS negotiation.
 *
 * @param session Session structure to initialize.
 * @param socket_fd Connected TCP socket.
 * @param config Optional configuration (may be null for defaults).
 * @return `true` on success, otherwise `false`.
 */
bool tls_init(TlsSession *session, i32 socket_fd, const TlsConfig *config);

/**
 * @brief Perform the TLS 1.3 handshake.
 *
 * @details
 * Drives the TLS 1.3 handshake state machine:
 * - Sends ClientHello (plaintext record).
 * - Receives and parses ServerHello (plaintext record).
 * - Derives handshake keys and switches record layer to encrypted mode.
 * - Processes EncryptedExtensions, Certificate, CertificateVerify and Finished.
 * - Derives application traffic keys and marks the session connected.
 *
 * Some verification steps are simplified during bring-up; callers should treat
 * failures/successes as best-effort until the TLS stack is hardened.
 *
 * @param session Session to handshake.
 * @return `true` if the session reached @ref TlsState::Connected, otherwise `false`.
 */
bool tls_handshake(TlsSession *session);

/**
 * @brief Send TLS application data.
 *
 * @details
 * Encrypts `data` as ApplicationData records and transmits them via the record
 * layer. The session must be connected.
 *
 * @param session Connected TLS session.
 * @param data Application bytes to send.
 * @param len Length of `data` in bytes.
 * @return Bytes sent on success, or a negative value on error.
 */
i64 tls_send(TlsSession *session, const void *data, usize len);

/**
 * @brief Receive TLS application data.
 *
 * @details
 * Receives records via the record layer and returns decrypted ApplicationData
 * payload bytes. Non-application records may be ignored. Alerts transition the
 * session to Closed.
 *
 * @param session Connected TLS session.
 * @param buffer Output buffer for decrypted data.
 * @param max_len Output buffer capacity.
 * @return Bytes received (>0), 0 if a non-application record was skipped, or
 *         a negative value on error/closed.
 */
i64 tls_recv(TlsSession *session, void *buffer, usize max_len);

/**
 * @brief Close the TLS session gracefully.
 *
 * @details
 * Sends a `close_notify` alert and marks the session closed. This does not
 * close the underlying TCP socket; the caller is responsible for transport
 * teardown.
 *
 * @param session TLS session.
 */
void tls_close(TlsSession *session);

/**
 * @brief Get the last error message for a session.
 *
 * @param session TLS session.
 * @return NUL-terminated error string; never `nullptr`.
 */
const char *tls_error(TlsSession *session);

/**
 * @brief Check whether a session is connected and ready for application data.
 *
 * @param session TLS session.
 * @return `true` if connected, otherwise `false`.
 */
bool tls_is_connected(TlsSession *session);

/**
 * @brief Generate random bytes for TLS operations.
 *
 * @details
 * Uses the virtio-rng device if available, otherwise falls back to a timer-based
 * generator. This is a bring-up helper; high quality entropy should be ensured
 * before relying on TLS for security.
 *
 * @param buffer Output buffer to fill.
 * @param len Number of bytes to generate.
 */
void tls_random_bytes(u8 *buffer, usize len);

} // namespace viper::tls

// Include shared TLSInfo struct (outside namespace)
#include "../../../include/viperos/tls_info.hpp"

namespace viper::tls
{

/**
 * @brief Fill a @ref TLSInfo structure for syscall reporting.
 *
 * @details
 * Provides user-visible details such as protocol version, cipher suite,
 * verification status, connection status, and the configured hostname.
 *
 * @param session TLS session.
 * @param info Output info structure.
 * @return `true` on success, otherwise `false`.
 */
bool tls_get_info(TlsSession *session, ::TLSInfo *info);

} // namespace viper::tls
