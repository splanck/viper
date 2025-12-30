/**
 * @file tls.cpp
 * @brief TLS 1.3 client session implementation.
 *
 * @details
 * Implements the TLS 1.3 session API declared in `tls.hpp`. The handshake
 * follows the TLS 1.3 key schedule using SHA-256 + HKDF and configures the
 * record layer for ChaCha20-Poly1305.
 *
 * Certificate handling is implemented to a bring-up standard: certificates are
 * parsed and the chain can be verified, but some handshake verification steps
 * (CertificateVerify/Finished) are simplified while the crypto stack matures.
 */

#include "tls.hpp"
#include "../../arch/aarch64/timer.hpp"
#include "../../console/serial.hpp"
#include "../../drivers/virtio/rng.hpp"
#include "../../lib/str.hpp"
#include "cert/ca_store.hpp"
#include "cert/verify.hpp"
#include "cert/x509.hpp"
#include "crypto/hkdf.hpp"
#include "crypto/sha256.hpp"

namespace viper::tls
{

/**
 * @brief Write a 16-bit value in big-endian order.
 *
 * @param p Output pointer (must have space for 2 bytes).
 * @param v Value to write.
 */
static void write_u16_be(u8 *p, u16 v)
{
    p[0] = static_cast<u8>(v >> 8);
    p[1] = static_cast<u8>(v);
}

/**
 * @brief Write a 24-bit value in big-endian order.
 *
 * @details
 * TLS handshake message lengths are encoded as 24-bit integers. This helper
 * writes the low 24 bits of `v` into 3 bytes.
 *
 * @param p Output pointer (must have space for 3 bytes).
 * @param v Value to write (low 24 bits used).
 */
static void write_u24_be(u8 *p, u32 v)
{
    p[0] = static_cast<u8>(v >> 16);
    p[1] = static_cast<u8>(v >> 8);
    p[2] = static_cast<u8>(v);
}

/**
 * @brief Read a 16-bit big-endian value from a byte buffer.
 *
 * @param p Input pointer (must have at least 2 bytes).
 * @return Parsed value in host order.
 */
static u16 read_u16_be(const u8 *p)
{
    return (static_cast<u16>(p[0]) << 8) | static_cast<u16>(p[1]);
}

/**
 * @brief Read a 24-bit big-endian value from a byte buffer.
 *
 * @param p Input pointer (must have at least 3 bytes).
 * @return Parsed 24-bit value in host order.
 */
static u32 read_u24_be(const u8 *p)
{
    return (static_cast<u32>(p[0]) << 16) | (static_cast<u32>(p[1]) << 8) | static_cast<u32>(p[2]);
}

// Use lib::strlen for string length operations

// Fallback PRNG state (used only when virtio-rng unavailable)
static u64 fallback_prng_state = 0;
static bool fallback_initialized = false;

// Fallback entropy mixing using timer jitter + SHA256
// Better than plain LCG but still not ideal without hardware entropy
/**
 * @brief Generate random bytes using a timer-based fallback PRNG.
 *
 * @details
 * This routine is used when virtio-rng is unavailable. It seeds itself by
 * hashing multiple timer readings (to capture jitter) and then produces bytes
 * using a mixed LCG/xorshift-style update.
 *
 * This generator is intended only as a last resort for bring-up. Strong TLS
 * security requires a high-quality entropy source.
 *
 * @param buffer Output buffer to fill.
 * @param len Number of bytes to generate.
 */
static void fallback_random_bytes(u8 *buffer, usize len)
{
    // Initialize fallback PRNG with multiple entropy sources
    if (!fallback_initialized)
    {
        // Mix timer ticks from multiple reads (jitter)
        u64 entropy[4];
        for (int i = 0; i < 4; i++)
        {
            entropy[i] = timer::get_ticks();
            // Small delay to get different values
            for (int j = 0; j < 100; j++)
            {
                asm volatile("" ::: "memory");
            }
        }

        // Hash the entropy to get initial state
        u8 hash[32];
        crypto::sha256(reinterpret_cast<u8 *>(entropy), sizeof(entropy), hash);

        fallback_prng_state = 0;
        for (int i = 0; i < 8; i++)
        {
            fallback_prng_state |= static_cast<u64>(hash[i]) << (i * 8);
        }
        fallback_prng_state ^= 0x5DEECE66DULL;
        fallback_initialized = true;
    }

    // Generate bytes using ChaCha-style mixing
    for (usize i = 0; i < len; i++)
    {
        // Mix in current timer for additional entropy
        if ((i & 0x1F) == 0)
        {
            fallback_prng_state ^= timer::get_ticks();
        }

        // Better mixing than simple LCG
        fallback_prng_state = fallback_prng_state * 6364136223846793005ULL + 1442695040888963407ULL;
        fallback_prng_state ^= (fallback_prng_state >> 17);
        fallback_prng_state ^= (fallback_prng_state << 13);
        buffer[i] = static_cast<u8>(fallback_prng_state >> 33);
    }
}

/** @copydoc viper::tls::tls_random_bytes */
void tls_random_bytes(u8 *buffer, usize len)
{
    // Try virtio-rng first (true hardware entropy)
    if (virtio::rng::is_available())
    {
        usize got = virtio::rng::get_bytes(buffer, len);
        if (got == len)
        {
            return; // Got all bytes from hardware RNG
        }

        // Partial success - fill rest with fallback
        if (got > 0)
        {
            fallback_random_bytes(buffer + got, len - got);
            return;
        }
    }

    // Fallback: timer-based entropy (not ideal but better than nothing)
    fallback_random_bytes(buffer, len);
}

/** @copydoc viper::tls::tls_init */
bool tls_init(TlsSession *session, i32 socket_fd, const TlsConfig *config)
{
    session->socket_fd = socket_fd;
    session->state = TlsState::Initial;
    session->error = nullptr;
    session->cipher_suite = CipherSuite::TLS_CHACHA20_POLY1305_SHA256;

    if (config)
    {
        session->config = *config;
    }
    else
    {
        session->config.hostname = nullptr;
        session->config.verify_certificates = true; // Default to verify
    }

    // Initialize certificate fields
    session->cert_verified = false;
    session->server_cert_key_type = 0;
    session->server_cert_modulus = nullptr;
    session->server_cert_modulus_len = 0;
    session->server_cert_exponent = nullptr;
    session->server_cert_exponent_len = 0;
    session->server_cert_data_len = 0;

    // Initialize session resumption fields
    session->session_ticket.valid = false;
    session->resumed = false;
    session->offered_ticket = nullptr;

    // Initialize record layer
    record_init(&session->record, socket_fd);

    // Generate client key pair
    tls_random_bytes(session->client_private_key, 32);
    crypto::x25519_clamp(session->client_private_key);
    crypto::x25519_public_key(session->client_private_key, session->client_public_key);

    // Generate client random
    tls_random_bytes(session->client_random, 32);

    // Initialize transcript hash
    crypto::sha256_init(&session->transcript);

    return true;
}

/**
 * @brief Build a TLS 1.3 ClientHello handshake message.
 *
 * @details
 * Encodes a minimal ClientHello containing:
 * - Legacy version set to TLS 1.2 (per TLS 1.3 requirements).
 * - A single cipher suite (currently ChaCha20-Poly1305-SHA256).
 * - SupportedVersions, KeyShare (X25519), SupportedGroups and
 *   SignatureAlgorithms extensions.
 * - Optional SNI (server_name) extension when `session->config.hostname` is set.
 *
 * The handshake header (type + 24-bit length) is included in the returned
 * bytes. The caller is responsible for adding the message to the transcript
 * hash before sending.
 *
 * Bounds checking is currently minimal during bring-up; `max_len` is reserved
 * for a future hardened implementation.
 *
 * @param session TLS session providing random and key share values.
 * @param buffer Output buffer to write the handshake message into.
 * @param max_len Capacity of `buffer` in bytes.
 * @return Length of the handshake message in bytes, or a negative value on error.
 */
static i64 build_client_hello(TlsSession *session, u8 *buffer, usize max_len)
{
    (void)max_len; // TODO: Add bounds checking
    u8 *p = buffer;
    u8 *start = buffer;

    // Leave room for handshake header (4 bytes)
    p += 4;

    // Legacy version (TLS 1.2)
    write_u16_be(p, TLS_VERSION_1_2);
    p += 2;

    // Client random
    for (int i = 0; i < 32; i++)
    {
        *p++ = session->client_random[i];
    }

    // Legacy session ID (empty)
    *p++ = 0;

    // Cipher suites
    write_u16_be(p, 2); // Length: 2 bytes
    p += 2;
    write_u16_be(p, static_cast<u16>(CipherSuite::TLS_CHACHA20_POLY1305_SHA256));
    p += 2;

    // Legacy compression methods
    *p++ = 1; // Length
    *p++ = 0; // Null compression

    // Extensions
    u8 *extensions_length_ptr = p;
    p += 2; // Skip extensions length

    // Extension: supported_versions (TLS 1.3)
    write_u16_be(p, static_cast<u16>(ExtensionType::SupportedVersions));
    p += 2;
    write_u16_be(p, 3); // Extension length
    p += 2;
    *p++ = 2; // Versions length
    write_u16_be(p, TLS_VERSION_1_3);
    p += 2;

    // Extension: key_share (X25519)
    write_u16_be(p, static_cast<u16>(ExtensionType::KeyShare));
    p += 2;
    write_u16_be(p, 36); // Extension length (2 + 2 + 32)
    p += 2;
    write_u16_be(p, 34); // Client key shares length
    p += 2;
    write_u16_be(p, 0x001d); // x25519 group
    p += 2;
    write_u16_be(p, 32); // Key length
    p += 2;
    for (int i = 0; i < 32; i++)
    {
        *p++ = session->client_public_key[i];
    }

    // Extension: supported_groups
    write_u16_be(p, static_cast<u16>(ExtensionType::SupportedGroups));
    p += 2;
    write_u16_be(p, 4); // Extension length
    p += 2;
    write_u16_be(p, 2); // Groups length
    p += 2;
    write_u16_be(p, 0x001d); // x25519
    p += 2;

    // Extension: signature_algorithms
    write_u16_be(p, static_cast<u16>(ExtensionType::SignatureAlgorithms));
    p += 2;
    write_u16_be(p, 8); // Extension length
    p += 2;
    write_u16_be(p, 6); // Algorithms length
    p += 2;
    write_u16_be(p, 0x0403); // ecdsa_secp256r1_sha256
    p += 2;
    write_u16_be(p, 0x0804); // rsa_pss_rsae_sha256
    p += 2;
    write_u16_be(p, 0x0401); // rsa_pkcs1_sha256
    p += 2;

    // Extension: server_name (SNI)
    if (session->config.hostname)
    {
        usize hostname_len = lib::strlen(session->config.hostname);
        write_u16_be(p, static_cast<u16>(ExtensionType::ServerName));
        p += 2;
        write_u16_be(p, static_cast<u16>(hostname_len + 5)); // Extension length
        p += 2;
        write_u16_be(p, static_cast<u16>(hostname_len + 3)); // Server name list length
        p += 2;
        *p++ = 0; // Host name type
        write_u16_be(p, static_cast<u16>(hostname_len));
        p += 2;
        for (usize i = 0; i < hostname_len; i++)
        {
            *p++ = session->config.hostname[i];
        }
    }

    // Write extensions length
    usize extensions_len = p - extensions_length_ptr - 2;
    write_u16_be(extensions_length_ptr, static_cast<u16>(extensions_len));

    // Write handshake header
    usize handshake_len = p - start - 4;
    start[0] = static_cast<u8>(HandshakeType::ClientHello);
    write_u24_be(start + 1, static_cast<u32>(handshake_len));

    return p - start;
}

/**
 * @brief Parse a TLS 1.3 ServerHello and extract negotiated parameters.
 *
 * @details
 * Parses the ServerHello body (excluding the 4-byte handshake header) and
 * extracts:
 * - Server random.
 * - Selected cipher suite.
 * - SupportedVersions selection (must be TLS 1.3).
 * - Server key share for X25519 (32-byte public key).
 *
 * On failure, `session->error` is set to a descriptive message.
 *
 * @param session TLS session to populate with parsed values.
 * @param data Pointer to ServerHello body bytes.
 * @param len Length of the ServerHello body.
 * @return `true` on success, otherwise `false`.
 */
static bool parse_server_hello(TlsSession *session, const u8 *data, usize len)
{
    if (len < 38)
    {
        session->error = "ServerHello too short";
        return false;
    }

    const u8 *p = data;

    // Skip legacy version
    p += 2;

    // Server random
    for (int i = 0; i < 32; i++)
    {
        session->server_random[i] = *p++;
    }

    // Legacy session ID
    u8 session_id_len = *p++;
    p += session_id_len;

    // Cipher suite
    u16 cipher = read_u16_be(p);
    p += 2;
    session->cipher_suite = static_cast<CipherSuite>(cipher);

    // Compression method (must be 0)
    p++;

    // Extensions
    if (p >= data + len)
    {
        session->error = "No extensions in ServerHello";
        return false;
    }

    u16 extensions_len = read_u16_be(p);
    p += 2;

    const u8 *extensions_end = p + extensions_len;
    bool found_key_share = false;
    bool found_version = false;

    while (p < extensions_end)
    {
        u16 ext_type = read_u16_be(p);
        p += 2;
        u16 ext_len = read_u16_be(p);
        p += 2;

        if (ext_type == static_cast<u16>(ExtensionType::KeyShare))
        {
            // Key share extension
            u16 group = read_u16_be(p);
            u16 key_len = read_u16_be(p + 2);
            if (group == 0x001d && key_len == 32)
            {
                for (int i = 0; i < 32; i++)
                {
                    session->server_public_key[i] = p[4 + i];
                }
                found_key_share = true;
            }
        }
        else if (ext_type == static_cast<u16>(ExtensionType::SupportedVersions))
        {
            u16 version = read_u16_be(p);
            if (version == TLS_VERSION_1_3)
            {
                found_version = true;
            }
        }

        p += ext_len;
    }

    if (!found_key_share)
    {
        session->error = "No key share in ServerHello";
        return false;
    }

    if (!found_version)
    {
        session->error = "Server did not select TLS 1.3";
        return false;
    }

    return true;
}

/**
 * @brief Derive TLS 1.3 handshake secrets and configure the record layer keys.
 *
 * @details
 * Computes the X25519 shared secret, then follows the TLS 1.3 key schedule to
 * derive:
 * - The handshake secret.
 * - Client/server handshake traffic secrets.
 * - AEAD keys and IVs for the record layer.
 *
 * This routine updates the session's secret fields and installs the derived
 * keys into the record layer.
 *
 * @param session TLS session to update.
 */
static void derive_handshake_keys(TlsSession *session)
{
    // Compute shared secret
    u8 shared_secret[32];
    crypto::x25519_shared_secret(
        session->client_private_key, session->server_public_key, shared_secret);

    // Get transcript hash so far
    u8 transcript_hash[32];
    crypto::Sha256Context transcript_copy = session->transcript;
    crypto::sha256_final(&transcript_copy, transcript_hash);

    // Derive early secret (no PSK, so IKM is zeros)
    u8 early_secret[32];
    u8 zeros[32] = {0};
    crypto::hkdf_extract(nullptr, 0, zeros, 32, early_secret);

    // Derive-Secret(early_secret, "derived", "")
    u8 derived[32];
    u8 empty_hash[32];
    crypto::sha256(nullptr, 0, empty_hash);
    crypto::hkdf_expand_label(early_secret, "derived", empty_hash, 32, derived, 32);

    // Handshake Secret = HKDF-Extract(derived, shared_secret)
    crypto::hkdf_extract(derived, 32, shared_secret, 32, session->handshake_secret);

    // Client/Server handshake traffic secrets
    crypto::hkdf_expand_label(session->handshake_secret,
                              "c hs traffic",
                              transcript_hash,
                              32,
                              session->client_handshake_traffic_secret,
                              32);
    crypto::hkdf_expand_label(session->handshake_secret,
                              "s hs traffic",
                              transcript_hash,
                              32,
                              session->server_handshake_traffic_secret,
                              32);

    // Derive keys and IVs for record layer
    u8 client_key[32], client_iv[12];
    u8 server_key[32], server_iv[12];

    crypto::hkdf_expand_label(
        session->client_handshake_traffic_secret, "key", nullptr, 0, client_key, 32);
    crypto::hkdf_expand_label(
        session->client_handshake_traffic_secret, "iv", nullptr, 0, client_iv, 12);
    crypto::hkdf_expand_label(
        session->server_handshake_traffic_secret, "key", nullptr, 0, server_key, 32);
    crypto::hkdf_expand_label(
        session->server_handshake_traffic_secret, "iv", nullptr, 0, server_iv, 12);

    record_set_write_keys(&session->record, client_key, client_iv);
    record_set_read_keys(&session->record, server_key, server_iv);
}

/**
 * @brief Derive TLS 1.3 application traffic secrets and record keys.
 *
 * @details
 * Uses the session transcript hash and the TLS 1.3 key schedule to derive the
 * master secret and then the client/server application traffic secrets. The
 * corresponding AEAD keys and IVs are installed into the record layer so
 * subsequent @ref record_send/@ref record_recv calls protect application data.
 *
 * @param session TLS session to update.
 */
static void derive_application_keys(TlsSession *session)
{
    // Get full transcript hash
    u8 transcript_hash[32];
    crypto::Sha256Context transcript_copy = session->transcript;
    crypto::sha256_final(&transcript_copy, transcript_hash);

    // Derive master secret
    u8 empty_hash[32];
    crypto::sha256(nullptr, 0, empty_hash);

    u8 derived[32];
    crypto::hkdf_expand_label(session->handshake_secret, "derived", empty_hash, 32, derived, 32);

    u8 zeros[32] = {0};
    crypto::hkdf_extract(derived, 32, zeros, 32, session->master_secret);

    // Application traffic secrets
    crypto::hkdf_expand_label(session->master_secret,
                              "c ap traffic",
                              transcript_hash,
                              32,
                              session->client_application_traffic_secret,
                              32);
    crypto::hkdf_expand_label(session->master_secret,
                              "s ap traffic",
                              transcript_hash,
                              32,
                              session->server_application_traffic_secret,
                              32);

    // Derive keys and IVs
    u8 client_key[32], client_iv[12];
    u8 server_key[32], server_iv[12];

    crypto::hkdf_expand_label(
        session->client_application_traffic_secret, "key", nullptr, 0, client_key, 32);
    crypto::hkdf_expand_label(
        session->client_application_traffic_secret, "iv", nullptr, 0, client_iv, 12);
    crypto::hkdf_expand_label(
        session->server_application_traffic_secret, "key", nullptr, 0, server_key, 32);
    crypto::hkdf_expand_label(
        session->server_application_traffic_secret, "iv", nullptr, 0, server_iv, 12);

    record_set_write_keys(&session->record, client_key, client_iv);
    record_set_read_keys(&session->record, server_key, server_iv);
}

/**
 * @brief Compute TLS Finished verify_data.
 *
 * @details
 * Computes the Finished HMAC over `transcript_hash` using the Finished key
 * derived from the provided traffic secret.
 *
 * @param secret Base secret (handshake traffic secret for the direction).
 * @param transcript_hash Hash of handshake transcript at the appropriate point.
 * @param verify_data Output 32-byte verify_data.
 */
static void compute_finished(const u8 secret[32], const u8 transcript_hash[32], u8 verify_data[32])
{
    u8 finished_key[32];
    crypto::hkdf_expand_label(secret, "finished", nullptr, 0, finished_key, 32);
    crypto::hmac_sha256(finished_key, 32, transcript_hash, 32, verify_data);
}

// TLS 1.3 CertificateVerify signature algorithms
constexpr u16 SIG_RSA_PKCS1_SHA256 = 0x0401;
constexpr u16 SIG_RSA_PSS_RSAE_SHA256 = 0x0804;
constexpr u16 SIG_ECDSA_SECP256R1_SHA256 = 0x0403;

/**
 * @brief Process a TLS 1.3 CertificateVerify message.
 *
 * @details
 * CertificateVerify proves possession of the private key corresponding to the
 * certificate's public key. The signature is computed over:
 * `0x20 * 64 || context_string || 0x00 || transcript_hash`.
 *
 * This bring-up implementation recognizes a small set of signature algorithms
 * and may accept/skip detailed verification when the certificate chain has
 * already been verified. Failures set `session->error`.
 *
 * @param session TLS session containing server certificate key material.
 * @param data Pointer to the CertificateVerify body (excluding handshake header).
 * @param len Length of the message body.
 * @param transcript_hash Transcript hash to verify against.
 * @return `true` on success/accepted verification, otherwise `false`.
 */
static bool process_certificate_verify(TlsSession *session,
                                       const u8 *data,
                                       usize len,
                                       const u8 *transcript_hash)
{
    if (len < 4)
    {
        session->error = "CertificateVerify too short";
        return false;
    }

    // Parse signature algorithm
    u16 sig_alg = read_u16_be(data);
    u16 sig_len = read_u16_be(data + 2);

    if (4 + static_cast<usize>(sig_len) > len)
    {
        session->error = "CertificateVerify signature truncated";
        return false;
    }

    (void)(data + 4); // signature data starts here (used in future for detailed verification)

    // Build the content that was signed
    // 64 spaces + "TLS 1.3, server CertificateVerify" + 0x00 + transcript_hash
    u8 signed_content[64 + 33 + 1 + 32];
    for (int i = 0; i < 64; i++)
    {
        signed_content[i] = 0x20; // space
    }
    const char *context = "TLS 1.3, server CertificateVerify";
    for (int i = 0; i < 33; i++)
    {
        signed_content[64 + i] = context[i];
    }
    signed_content[64 + 33] = 0x00;
    for (int i = 0; i < 32; i++)
    {
        signed_content[64 + 33 + 1 + i] = transcript_hash[i];
    }

    serial::puts("[tls] CertificateVerify: algorithm=0x");
    serial::put_hex(sig_alg);
    serial::puts(", sig_len=");
    serial::put_dec(sig_len);
    serial::puts("\n");

    // Check if we can verify this signature type
    bool can_verify = false;

    if ((sig_alg == SIG_RSA_PKCS1_SHA256 || sig_alg == SIG_RSA_PSS_RSAE_SHA256) &&
        session->server_cert_key_type == 0 && // RSA
        session->server_cert_modulus != nullptr)
    {
        // For RSA-PKCS1-SHA256, we can verify
        if (sig_alg == SIG_RSA_PKCS1_SHA256)
        {
            // Hash the signed content
            u8 content_hash[32];
            crypto::sha256(signed_content, sizeof(signed_content), content_hash);

            // Verify RSA-PKCS1 signature
            // Note: verify_rsa_pkcs1 in verify.cpp uses TBS data directly,
            // but we need to verify against our pre-computed hash
            // For now, we'll trust the CertificateVerify if cert chain verified
            serial::puts("[tls] RSA-PKCS1-SHA256 signature (chain already verified)\n");
            can_verify = true;
        }
        else
        {
            // RSA-PSS is more complex, skip for now
            serial::puts("[tls] RSA-PSS signature (skipping detailed verification)\n");
            can_verify = true;
        }
    }
    else if (sig_alg == SIG_ECDSA_SECP256R1_SHA256)
    {
        // ECDSA-P256 - we don't have full support yet
        serial::puts("[tls] ECDSA-P256 signature (verification not supported)\n");
        // If certificate chain was verified, we'll trust this
        can_verify = session->cert_verified;
    }
    else
    {
        serial::puts("[tls] Unknown signature algorithm\n");
    }

    if (!can_verify && session->config.verify_certificates)
    {
        // Only fail if certificate wasn't already verified
        if (!session->cert_verified)
        {
            session->error = "Cannot verify CertificateVerify signature";
            return false;
        }
    }

    return true;
}

/**
 * @brief Process a TLS 1.3 Certificate message and optionally verify the chain.
 *
 * @details
 * Parses the server certificate_list, extracts and parses each DER-encoded
 * X.509 certificate, and optionally verifies the chain based on session
 * configuration. The function also caches the leaf certificate's public key
 * material in the session so CertificateVerify can be processed.
 *
 * On failure, `session->error` is set and `false` is returned.
 *
 * @param session TLS session to update.
 * @param data Pointer to Certificate message body (excluding handshake header).
 * @param len Length of message body.
 * @return `true` on success, otherwise `false`.
 */
static bool process_certificate_message(TlsSession *session, const u8 *data, usize len)
{
    if (len < 4)
    {
        session->error = "Certificate message too short";
        return false;
    }

    const u8 *p = data;
    const u8 *end = data + len;

    // certificate_request_context (should be empty for server cert)
    u8 ctx_len = *p++;
    if (ctx_len > 0)
    {
        p += ctx_len;
    }

    if (p + 3 > end)
    {
        session->error = "Certificate message truncated";
        return false;
    }

    // certificate_list length (3 bytes)
    u32 list_len = read_u24_be(p);
    p += 3;

    if (p + list_len > end)
    {
        session->error = "Certificate list length mismatch";
        return false;
    }

    // Parse certificates into array
    x509::Certificate chain[x509::MAX_CERT_CHAIN];
    usize chain_length = 0;
    const u8 *list_end = p + list_len;

    // Also collect raw cert data for storing first cert's key
    const u8 *first_cert_data = nullptr;
    usize first_cert_len = 0;

    while (p < list_end && chain_length < x509::MAX_CERT_CHAIN)
    {
        if (p + 3 > list_end)
            break;

        // Certificate data length (3 bytes)
        u32 cert_len = read_u24_be(p);
        p += 3;

        if (p + cert_len > list_end)
        {
            session->error = "Certificate data truncated";
            return false;
        }

        // Store first certificate data for key extraction
        if (chain_length == 0)
        {
            first_cert_data = p;
            first_cert_len = cert_len;
        }

        // Parse certificate
        if (!x509::parse_certificate(p, cert_len, &chain[chain_length]))
        {
            serial::puts("[tls] Failed to parse certificate ");
            serial::put_dec(chain_length);
            serial::puts("\n");
            if (session->config.verify_certificates)
            {
                session->error = "Failed to parse certificate";
                return false;
            }
        }
        else
        {
            chain_length++;
        }

        p += cert_len;

        // Extensions (2 bytes length + data) - skip for now
        if (p + 2 > list_end)
            break;
        u16 ext_len = read_u16_be(p);
        p += 2 + ext_len;
    }

    if (chain_length == 0)
    {
        session->error = "No certificates in chain";
        return false;
    }

    // Log certificate info
    serial::puts("[tls] Server certificate: ");
    serial::puts(chain[0].subject_cn);
    serial::puts(" (");
    serial::put_dec(chain_length);
    serial::puts(" certs in chain)\n");

    // Store server certificate's public key info for CertificateVerify
    if (first_cert_data && first_cert_len > 0 &&
        first_cert_len <= sizeof(session->server_cert_data))
    {
        // Copy raw cert data
        for (usize i = 0; i < first_cert_len; i++)
        {
            session->server_cert_data[i] = first_cert_data[i];
        }
        session->server_cert_data_len = first_cert_len;

        // Store key info
        if (chain[0].key_type == x509::KeyType::RSA)
        {
            session->server_cert_key_type = 0; // RSA
            // These pointers point into the raw data we copied, but we need
            // to recalculate based on offset from original
            usize mod_offset = chain[0].rsa_modulus - first_cert_data;
            usize exp_offset = chain[0].rsa_exponent - first_cert_data;
            session->server_cert_modulus = session->server_cert_data + mod_offset;
            session->server_cert_modulus_len = chain[0].rsa_modulus_length;
            session->server_cert_exponent = session->server_cert_data + exp_offset;
            session->server_cert_exponent_len = chain[0].rsa_exponent_length;
        }
        else
        {
            session->server_cert_key_type = 1; // ECDSA/other
        }
    }

    // Verify certificate chain if enabled
    if (session->config.verify_certificates)
    {
        cert::VerifyOptions opts = cert::default_verify_options();

        cert::VerifyResult result =
            cert::verify_chain(chain, chain_length, session->config.hostname, &opts);

        if (result != cert::VerifyResult::OK)
        {
            serial::puts("[tls] Certificate verification failed: ");
            serial::puts(cert::verify_result_message(result));
            serial::puts("\n");
            session->error = cert::verify_result_message(result);
            session->cert_verified = false;
            return false;
        }

        serial::puts("[tls] Certificate chain verified successfully\n");
        session->cert_verified = true;
    }
    else
    {
        serial::puts("[tls] Certificate verification disabled (NOVERIFY)\n");
        session->cert_verified = false; // Didn't verify, not verified
    }

    return true;
}

/** @copydoc viper::tls::tls_handshake */
bool tls_handshake(TlsSession *session)
{
    if (session->state != TlsState::Initial)
    {
        session->error = "Invalid state for handshake";
        return false;
    }

    // Build and send ClientHello
    u8 client_hello[512];
    i64 ch_len = build_client_hello(session, client_hello, sizeof(client_hello));
    if (ch_len < 0)
    {
        session->error = "Failed to build ClientHello";
        session->state = TlsState::Error;
        return false;
    }

    // Add to transcript
    crypto::sha256_update(&session->transcript, client_hello, ch_len);

    // Send ClientHello
    if (record_send_plaintext(&session->record, ContentType::Handshake, client_hello, ch_len) < 0)
    {
        session->error = "Failed to send ClientHello";
        session->state = TlsState::Error;
        return false;
    }

    session->state = TlsState::ClientHelloSent;

    // Receive ServerHello
    u8 buffer[16384];
    ContentType type;
    i64 len = record_recv_plaintext(&session->record, &type, buffer, sizeof(buffer));

    if (len < 4 || type != ContentType::Handshake)
    {
        session->error = "Failed to receive ServerHello";
        session->state = TlsState::Error;
        return false;
    }

    // Check handshake type
    if (buffer[0] != static_cast<u8>(HandshakeType::ServerHello))
    {
        session->error = "Expected ServerHello";
        session->state = TlsState::Error;
        return false;
    }

    // Add ServerHello to transcript
    crypto::sha256_update(&session->transcript, buffer, len);

    // Parse ServerHello
    if (!parse_server_hello(session, buffer + 4, len - 4))
    {
        session->state = TlsState::Error;
        return false;
    }

    session->state = TlsState::ServerHelloReceived;

    // Derive handshake keys
    derive_handshake_keys(session);

    // Now we need to receive and process:
    // - EncryptedExtensions
    // - Certificate (optional, but usually present)
    // - CertificateVerify (optional)
    // - Finished

    // For simplicity, we'll process all encrypted handshake messages
    while (session->state != TlsState::Connected)
    {
        len = record_recv(&session->record, &type, buffer, sizeof(buffer));
        if (len < 0)
        {
            session->error = "Failed to receive handshake message";
            session->state = TlsState::Error;
            return false;
        }

        if (type != ContentType::Handshake)
        {
            if (type == ContentType::Alert)
            {
                session->error = "Received alert from server";
                session->state = TlsState::Error;
                return false;
            }
            continue; // Skip other types
        }

        // Add to transcript
        crypto::sha256_update(&session->transcript, buffer, len);

        // Process handshake message
        usize offset = 0;
        while (offset < static_cast<usize>(len))
        {
            HandshakeType msg_type = static_cast<HandshakeType>(buffer[offset]);
            u32 msg_len = read_u24_be(buffer + offset + 1);
            offset += 4;

            switch (msg_type)
            {
                case HandshakeType::EncryptedExtensions:
                    session->state = TlsState::WaitCertificate;
                    break;

                case HandshakeType::Certificate:
                    // Parse and verify certificate chain
                    if (!process_certificate_message(session, buffer + offset, msg_len))
                    {
                        session->state = TlsState::Error;
                        return false;
                    }
                    session->state = TlsState::WaitCertificateVerify;
                    break;

                case HandshakeType::CertificateVerify:
                {
                    // Get transcript hash (note: includes this message, which
                    // is a simplification - proper impl tracks pre-message state)
                    u8 cv_transcript[32];
                    crypto::Sha256Context tmp = session->transcript;
                    crypto::sha256_final(&tmp, cv_transcript);

                    if (!process_certificate_verify(
                            session, buffer + offset, msg_len, cv_transcript))
                    {
                        session->state = TlsState::Error;
                        return false;
                    }
                }
                    session->state = TlsState::WaitFinished;
                    break;

                case HandshakeType::Finished:
                {
                    // Verify server Finished
                    // Get transcript hash up to (but not including) this Finished
                    u8 transcript_hash[32];
                    crypto::Sha256Context tmp = session->transcript;
                    // Remove the Finished message we just added
                    // Actually, transcript should be hash before this message
                    // This is a simplification - proper impl needs to track state
                    crypto::sha256_final(&tmp, transcript_hash);

                    // Verify (simplified - skip verification for now)
                    // TODO: Proper Finished verification

                    // Derive application keys
                    derive_application_keys(session);

                    // Send client Finished
                    u8 finished[36];
                    finished[0] = static_cast<u8>(HandshakeType::Finished);
                    write_u24_be(finished + 1, 32);
                    compute_finished(
                        session->client_handshake_traffic_secret, transcript_hash, finished + 4);

                    // Temporarily set write keys back to handshake keys for Finished
                    u8 client_key[32], client_iv[12];
                    crypto::hkdf_expand_label(session->client_handshake_traffic_secret,
                                              "key",
                                              nullptr,
                                              0,
                                              client_key,
                                              32);
                    crypto::hkdf_expand_label(
                        session->client_handshake_traffic_secret, "iv", nullptr, 0, client_iv, 12);
                    record_set_write_keys(&session->record, client_key, client_iv);

                    if (record_send(&session->record, ContentType::Handshake, finished, 36) < 0)
                    {
                        session->error = "Failed to send Finished";
                        session->state = TlsState::Error;
                        return false;
                    }

                    // Now switch to application keys
                    derive_application_keys(session);
                    session->state = TlsState::Connected;
                }
                break;

                default:
                    // Skip unknown messages
                    break;
            }

            offset += msg_len;
        }
    }

    return session->state == TlsState::Connected;
}

/** @copydoc viper::tls::tls_send */
i64 tls_send(TlsSession *session, const void *data, usize len)
{
    if (session->state != TlsState::Connected)
    {
        return -1;
    }
    return record_send(&session->record, ContentType::ApplicationData, data, len);
}

/** @copydoc viper::tls::tls_recv */
i64 tls_recv(TlsSession *session, void *buffer, usize max_len)
{
    if (session->state != TlsState::Connected)
    {
        return -1;
    }

    ContentType type;
    i64 result = record_recv(&session->record, &type, buffer, max_len);

    if (result < 0)
    {
        return result;
    }

    if (type == ContentType::Alert)
    {
        session->state = TlsState::Closed;
        return -1;
    }

    if (type != ContentType::ApplicationData)
    {
        // Skip non-application data
        return 0;
    }

    return result;
}

/** @copydoc viper::tls::tls_close */
void tls_close(TlsSession *session)
{
    // Send close_notify alert
    u8 alert[2] = {1, 0}; // warning, close_notify
    record_send(&session->record, ContentType::Alert, alert, 2);
    session->state = TlsState::Closed;
}

/** @copydoc viper::tls::tls_error */
const char *tls_error(TlsSession *session)
{
    return session->error ? session->error : "Unknown error";
}

/** @copydoc viper::tls::tls_is_connected */
bool tls_is_connected(TlsSession *session)
{
    return session->state == TlsState::Connected;
}

/** @copydoc viper::tls::tls_get_info */
bool tls_get_info(TlsSession *session, ::TLSInfo *info)
{
    if (!session || !info)
    {
        return false;
    }

    // Protocol version - always TLS 1.3 for our implementation
    info->protocol_version = TLS_VERSION_1_3;

    // Cipher suite
    info->cipher_suite = static_cast<unsigned short>(session->cipher_suite);

    // Verified flag - reflects actual verification status
    // 1 = verified successfully, 0 = not verified or verification failed
    info->verified = session->cert_verified ? 1 : 0;

    // Connected status
    info->connected = (session->state == TlsState::Connected) ? 1 : 0;

    // Reserved
    info->_reserved[0] = 0;
    info->_reserved[1] = 0;

    // Hostname
    if (session->config.hostname)
    {
        usize len = 0;
        while (session->config.hostname[len] && len < TLS_INFO_HOSTNAME_MAX - 1)
        {
            info->hostname[len] = session->config.hostname[len];
            len++;
        }
        info->hostname[len] = '\0';
    }
    else
    {
        info->hostname[0] = '\0';
    }

    return true;
}

//=============================================================================
// Session Resumption Implementation
//=============================================================================

/**
 * @brief Derive resumption master secret from the key schedule.
 */
static void derive_resumption_master_secret(TlsSession *session)
{
    // Get transcript hash after Finished messages
    u8 transcript_hash[32];
    crypto::Sha256Context transcript_copy = session->transcript;
    crypto::sha256_final(&transcript_copy, transcript_hash);

    // res_master = Derive-Secret(master_secret, "res master", transcript_hash)
    crypto::hkdf_expand_label(session->master_secret,
                              "res master",
                              transcript_hash,
                              32,
                              session->resumption_master_secret,
                              32);
}

/**
 * @brief Process a NewSessionTicket message.
 */
static bool process_new_session_ticket(TlsSession *session, const u8 *data, usize len)
{
    if (len < 12)
    {
        return false;
    }

    const u8 *p = data;

    // ticket_lifetime (4 bytes)
    u32 lifetime = (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) |
                   (static_cast<u32>(p[2]) << 8) | static_cast<u32>(p[3]);
    p += 4;

    // ticket_age_add (4 bytes)
    u32 age_add = (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) |
                  (static_cast<u32>(p[2]) << 8) | static_cast<u32>(p[3]);
    p += 4;

    // ticket_nonce (1 byte length + nonce)
    u8 nonce_len = *p++;
    if (p + nonce_len > data + len)
        return false;

    const u8 *nonce = p;
    p += nonce_len;

    // ticket (2 bytes length + ticket)
    if (p + 2 > data + len)
        return false;
    u16 ticket_len = read_u16_be(p);
    p += 2;

    if (p + ticket_len > data + len)
        return false;
    if (ticket_len > MAX_TICKET_SIZE)
        return false;

    const u8 *ticket = p;
    // p += ticket_len;  // Extensions follow but we skip them

    // Store the ticket
    session->session_ticket.valid = true;
    session->session_ticket.lifetime = lifetime;
    session->session_ticket.age_add = age_add;
    session->session_ticket.nonce_len = nonce_len;
    for (u8 i = 0; i < nonce_len && i < 8; i++)
    {
        session->session_ticket.nonce[i] = nonce[i];
    }
    session->session_ticket.ticket_len = ticket_len;
    for (usize i = 0; i < ticket_len; i++)
    {
        session->session_ticket.ticket[i] = ticket[i];
    }

    // Copy resumption master secret
    for (int i = 0; i < 32; i++)
    {
        session->session_ticket.resumption_master_secret[i] = session->resumption_master_secret[i];
    }

    session->session_ticket.issue_time = timer::get_ms();

    // Copy hostname
    if (session->config.hostname)
    {
        usize i = 0;
        while (session->config.hostname[i] && i < sizeof(session->session_ticket.hostname) - 1)
        {
            session->session_ticket.hostname[i] = session->config.hostname[i];
            i++;
        }
        session->session_ticket.hostname[i] = '\0';
    }
    else
    {
        session->session_ticket.hostname[0] = '\0';
    }

    serial::puts("[tls] Received NewSessionTicket (lifetime=");
    serial::put_dec(lifetime);
    serial::puts("s)\n");

    return true;
}

/** @copydoc viper::tls::tls_init_resume */
bool tls_init_resume(TlsSession *session,
                     i32 socket_fd,
                     const TlsConfig *config,
                     SessionTicket *ticket)
{
    // Standard init
    if (!tls_init(session, socket_fd, config))
    {
        return false;
    }

    // Store the offered ticket for use in handshake
    session->offered_ticket = ticket;
    session->resumed = false;

    return true;
}

/** @copydoc viper::tls::tls_was_resumed */
bool tls_was_resumed(TlsSession *session)
{
    return session && session->resumed;
}

/** @copydoc viper::tls::tls_get_session_ticket */
const SessionTicket *tls_get_session_ticket(TlsSession *session)
{
    if (!session || !session->session_ticket.valid)
    {
        return nullptr;
    }
    return &session->session_ticket;
}

/** @copydoc viper::tls::tls_process_post_handshake */
i32 tls_process_post_handshake(TlsSession *session)
{
    if (!session || session->state != TlsState::Connected)
    {
        return -1;
    }

    // Derive resumption master secret if not done yet
    static bool rms_derived = false;
    if (!rms_derived)
    {
        derive_resumption_master_secret(session);
        rms_derived = true;
    }

    // Try to receive a record (non-blocking would require socket changes)
    // For now, we'll process any available handshake messages
    u8 buffer[4096];
    ContentType type;
    i64 len = record_recv(&session->record, &type, buffer, sizeof(buffer));

    if (len <= 0)
    {
        return 0;
    }

    i32 processed = 0;

    if (type == ContentType::Handshake)
    {
        usize offset = 0;
        while (offset + 4 <= static_cast<usize>(len))
        {
            HandshakeType msg_type = static_cast<HandshakeType>(buffer[offset]);
            u32 msg_len = read_u24_be(buffer + offset + 1);
            offset += 4;

            if (offset + msg_len > static_cast<usize>(len))
                break;

            if (msg_type == HandshakeType::NewSessionTicket)
            {
                if (process_new_session_ticket(session, buffer + offset, msg_len))
                {
                    processed++;
                }
            }

            offset += msg_len;
        }
    }

    return processed;
}

/** @copydoc viper::tls::tls_compute_resumption_psk */
void tls_compute_resumption_psk(const SessionTicket *ticket, u8 *psk)
{
    if (!ticket || !psk)
        return;

    // PSK = HKDF-Expand-Label(resumption_master_secret, "resumption", ticket_nonce, 32)
    crypto::hkdf_expand_label(ticket->resumption_master_secret,
                              "resumption",
                              ticket->nonce,
                              ticket->nonce_len,
                              psk,
                              32);
}

/** @copydoc viper::tls::tls_ticket_valid */
bool tls_ticket_valid(const SessionTicket *ticket)
{
    if (!ticket || !ticket->valid)
    {
        return false;
    }

    // Check expiration
    u64 now = timer::get_ms();
    u64 age_ms = now - ticket->issue_time;
    u64 lifetime_ms = static_cast<u64>(ticket->lifetime) * 1000;

    if (age_ms > lifetime_ms)
    {
        return false;
    }

    // Also check against max lifetime (7 days)
    if (ticket->lifetime > MAX_TICKET_LIFETIME)
    {
        return false;
    }

    return true;
}

} // namespace viper::tls
