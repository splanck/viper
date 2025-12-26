#pragma once

/**
 * @file verify.hpp
 * @brief Certificate chain verification helpers for TLS.
 *
 * @details
 * Provides certificate verification routines used by the TLS client:
 * - Signature verification for selected algorithms (RSA with SHA-256 in v0.2.0).
 * - Chain validation from leaf to a trusted root in the embedded CA store.
 * - Hostname checking against SAN/CN and optional validity-time checks.
 *
 * The verifier is designed for bring-up and is intentionally limited; it may
 * skip some checks or support only a subset of algorithms as the crypto stack
 * matures.
 */

#include "../../../include/types.hpp"
#include "ca_store.hpp"
#include "x509.hpp"

namespace viper::tls::cert
{

/**
 * @brief Certificate verification result codes.
 *
 * @details
 * Used to report detailed reasons for chain verification failures.
 */
enum class VerifyResult
{
    OK,                      // Certificate chain is valid
    INVALID_SIGNATURE,       // Signature verification failed
    EXPIRED,                 // Certificate has expired
    NOT_YET_VALID,           // Certificate not yet valid
    UNKNOWN_ISSUER,          // Issuer not found in chain or CA store
    CHAIN_TOO_LONG,          // Certificate chain exceeds maximum length
    HOSTNAME_MISMATCH,       // Certificate doesn't match hostname
    INVALID_CERTIFICATE,     // Certificate parsing failed
    INVALID_CHAIN,           // Chain is not properly formed
    SELF_SIGNED_NOT_TRUSTED, // Self-signed cert not in CA store
};

/**
 * @brief Options controlling which verification checks are performed.
 *
 * @details
 * These options allow callers to selectively disable certain checks during
 * bring-up (e.g., time validity when no wall-clock time is available).
 */
struct VerifyOptions
{
    bool verify_hostname;   // Check hostname against SAN/CN
    bool verify_time;       // Check validity dates (requires system time)
    bool verify_chain;      // Build and verify chain to root
    bool allow_self_signed; // Allow self-signed certs (for testing)
};

/**
 * @brief Default verification options suitable for HTTPS.
 *
 * @details
 * Enables hostname and chain verification. Time verification is disabled until
 * the kernel has a reliable wall-clock time source.
 *
 * @return Default verification options.
 */
inline VerifyOptions default_verify_options()
{
    return {
        .verify_hostname = true,
        .verify_time = false, // Disabled until we have system time
        .verify_chain = true,
        .allow_self_signed = false,
    };
}

/**
 * @brief Verify a certificate signature using an issuer certificate.
 *
 * @details
 * Verifies that `cert` was signed by `issuer` using the issuer's public key.
 * The v0.2.0 implementation focuses on RSA-based signatures.
 *
 * @param cert Certificate whose signature should be verified.
 * @param issuer Issuer certificate providing the public key.
 * @return `true` if the signature is accepted, otherwise `false`.
 */
bool verify_certificate_signature(const x509::Certificate *cert, const x509::Certificate *issuer);

/**
 * @brief Verify a certificate signature using an embedded root CA entry.
 *
 * @details
 * Uses the trust anchor's public key (from the embedded CA store) to verify
 * the signature on `cert`.
 *
 * @param cert Certificate whose signature should be verified.
 * @param ca Embedded root CA entry providing the public key.
 * @return `true` if the signature is accepted, otherwise `false`.
 */
bool verify_certificate_signature_with_ca(const x509::Certificate *cert, const RootCaEntry *ca);

/**
 * @brief Verify a parsed certificate chain.
 *
 * @details
 * Expects `chain[0]` to be the leaf (server) certificate and subsequent entries
 * to be intermediates. The function:
 * - Optionally checks hostname matching on the leaf.
 * - Optionally checks time validity for each certificate.
 * - Verifies that each certificate is issued by the next certificate in the chain.
 * - Verifies signatures along the chain.
 * - Attempts to anchor the chain against a trusted root CA in the store.
 *
 * @param chain Parsed certificate array.
 * @param chain_length Number of certificates in `chain`.
 * @param hostname Hostname to verify against the leaf certificate (may be null).
 * @param options Verification options (must not be null).
 * @return Verification result indicating success or failure reason.
 */
VerifyResult verify_chain(const x509::Certificate *chain,
                          usize chain_length,
                          const char *hostname,
                          const VerifyOptions *options);

/**
 * @brief Parse and verify a concatenated DER certificate chain.
 *
 * @details
 * Parses a chain from `data` (assumed to contain concatenated DER-encoded
 * certificates) into a temporary array and then calls @ref verify_chain.
 *
 * @param data Pointer to concatenated DER certificates.
 * @param length Total length of `data` in bytes.
 * @param hostname Optional hostname for leaf verification.
 * @param options Verification options (must not be null).
 * @return Verification result indicating success/failure reason.
 */
VerifyResult verify_chain_der(const void *data,
                              usize length,
                              const char *hostname,
                              const VerifyOptions *options);

/**
 * @brief Convert a verification result to a human-readable message.
 *
 * @param result Verification result code.
 * @return NUL-terminated static message string.
 */
const char *verify_result_message(VerifyResult result);

} // namespace viper::tls::cert
