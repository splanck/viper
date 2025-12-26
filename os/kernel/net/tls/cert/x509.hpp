#pragma once

/**
 * @file x509.hpp
 * @brief Minimal X.509 certificate parsing utilities for TLS.
 *
 * @details
 * Implements a small X.509 certificate parser suitable for TLS client
 * verification in v0.2.0. The parser focuses on extracting:
 * - Subject and issuer names (CN/O).
 * - Validity dates (parsed but time checking may be disabled until a time source exists).
 * - Subject public key information (RSA modulus/exponent or ECDSA curve OID).
 * - Selected extensions (SubjectAltName and BasicConstraints).
 *
 * The parsed @ref Certificate structure primarily holds pointers into the
 * original DER buffer; the caller must keep the certificate bytes alive for as
 * long as the parsed structure is used.
 */

#include "../../../include/types.hpp"
#include "../asn1/asn1.hpp"

namespace viper::x509
{

/** @name Parser limits used for fixed-size buffers */
///@{
constexpr usize MAX_NAME_LENGTH = 256;
constexpr usize MAX_SAN_ENTRIES = 16;
constexpr usize MAX_CERT_CHAIN = 8;
///@}

/**
 * @brief Supported certificate signature algorithms.
 *
 * @details
 * These values are derived from OIDs encountered in the certificate.
 */
enum class SignatureAlgorithm
{
    Unknown,
    SHA256_RSA,
    SHA384_RSA,
    SHA256_ECDSA,
    SHA384_ECDSA,
    ED25519,
};

/**
 * @brief Public key type extracted from SubjectPublicKeyInfo.
 *
 * @details
 * The parser identifies RSA keys and a small set of EC curves by OID.
 */
enum class KeyType
{
    Unknown,
    RSA,
    ECDSA_P256,
    ECDSA_P384,
    ED25519,
};

/**
 * @brief Subject Alternative Name (SAN) entry.
 *
 * @details
 * SAN entries are extracted from the SubjectAltName extension. This structure
 * stores a small subset of SAN types; current hostname matching primarily uses
 * DNS entries.
 */
struct SanEntry
{
    enum Type : u8
    {
        DNS = 2,
        URI = 6,
        IP = 7,
    } type;

    char value[128];
};

/**
 * @brief Certificate validity timestamps.
 *
 * @details
 * Time values are parsed from UTCTime or GeneralizedTime. Time verification is
 * optional and may be disabled until the kernel has a reliable wall-clock time
 * source.
 */
struct Validity
{
    u16 not_before_year;
    u8 not_before_month;
    u8 not_before_day;
    u8 not_before_hour;
    u8 not_before_minute;
    u8 not_before_second;

    u16 not_after_year;
    u8 not_after_month;
    u8 not_after_day;
    u8 not_after_hour;
    u8 not_after_minute;
    u8 not_after_second;
};

/**
 * @brief Parsed X.509 certificate view.
 *
 * @details
 * The parser fills this structure with a mixture of extracted strings and
 * pointers into the original DER buffer. The `raw`/`raw_length` fields capture
 * the full certificate bytes; individual pointers (serial number, public key,
 * signature, etc.) refer into that buffer.
 */
struct Certificate
{
    // Version (0 = v1, 1 = v2, 2 = v3)
    i32 version;

    // Serial number (raw bytes)
    const u8 *serial_number;
    usize serial_number_length;

    // Signature algorithm
    SignatureAlgorithm signature_algorithm;

    // Issuer name (Common Name)
    char issuer_cn[MAX_NAME_LENGTH];
    char issuer_org[MAX_NAME_LENGTH];

    // Subject name (Common Name)
    char subject_cn[MAX_NAME_LENGTH];
    char subject_org[MAX_NAME_LENGTH];

    // Validity
    Validity validity;

    // Public key
    KeyType key_type;
    const u8 *public_key;
    usize public_key_length;

    // For RSA: modulus and exponent
    const u8 *rsa_modulus;
    usize rsa_modulus_length;
    const u8 *rsa_exponent;
    usize rsa_exponent_length;

    // Subject Alternative Names
    SanEntry san[MAX_SAN_ENTRIES];
    usize san_count;

    // Basic Constraints
    bool is_ca;
    i32 path_length; // -1 if not specified

    // Key Usage (bit flags)
    u16 key_usage;

    // TBS (To Be Signed) certificate for signature verification
    const u8 *tbs_certificate;
    usize tbs_certificate_length;

    // Signature
    const u8 *signature;
    usize signature_length;

    // Raw certificate data
    const u8 *raw;
    usize raw_length;
};

/**
 * @brief Parse a DER-encoded X.509 certificate.
 *
 * @details
 * Parses the certificate structure and populates `cert` with extracted fields
 * and pointers into `data`. The parser focuses on fields commonly needed for
 * TLS verification and diagnostics.
 *
 * @param data Pointer to DER-encoded certificate bytes.
 * @param length Length of the certificate in bytes.
 * @param cert Output parsed certificate structure.
 * @return `true` on success, otherwise `false`.
 */
bool parse_certificate(const void *data, usize length, Certificate *cert);

/**
 * @brief Verify a certificate signature using an issuer certificate.
 *
 * @details
 * Intended to verify that `cert` was signed by `issuer`. The current v0.2.0
 * implementation may be incomplete and may return `true` as a placeholder.
 *
 * @param cert Certificate to verify.
 * @param issuer Issuer certificate containing the public key.
 * @return `true` if the signature is accepted, otherwise `false`.
 */
bool verify_signature(const Certificate *cert, const Certificate *issuer);

/**
 * @brief Check whether a certificate matches a hostname.
 *
 * @details
 * Performs hostname matching using SAN dNSName entries when present, otherwise
 * falls back to the subject common name. Wildcard patterns of the form
 * `*.example.com` are supported.
 *
 * @param cert Parsed certificate.
 * @param hostname Hostname to test.
 * @return `true` if the hostname is accepted, otherwise `false`.
 */
bool matches_hostname(const Certificate *cert, const char *hostname);

/**
 * @brief Check whether a certificate is currently time-valid.
 *
 * @details
 * Compares the certificate validity period against the system time. In v0.2.0
 * this may be a stub until the kernel has reliable timekeeping.
 *
 * @param cert Parsed certificate.
 * @return `true` if valid (or time checking is not implemented), otherwise `false`.
 */
bool is_time_valid(const Certificate *cert);

/**
 * @brief Check whether a certificate is issued by a given issuer (name match).
 *
 * @details
 * Performs a simplified issuer check by comparing the certificate's issuer name
 * against the issuer certificate's subject name. This is used as a lightweight
 * chain-building sanity check.
 *
 * @param cert Certificate whose issuer should be checked.
 * @param issuer Issuer certificate.
 * @return `true` if the issuer names match, otherwise `false`.
 */
bool is_issued_by(const Certificate *cert, const Certificate *issuer);

} // namespace viper::x509
