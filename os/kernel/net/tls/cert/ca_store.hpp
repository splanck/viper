#pragma once

/**
 * @file ca_store.hpp
 * @brief Embedded root CA store for TLS certificate verification.
 *
 * @details
 * TLS certificate chain verification ultimately requires a set of trusted root
 * certificate authorities (CAs). ViperOS embeds a small curated list of root CA
 * public keys for use during HTTPS verification.
 *
 * The store provides lookup by subject common name and by Subject Key
 * Identifier (SKID). During bring-up this is sufficient for basic chain
 * building and verification.
 */

#include "../../../include/types.hpp"
#include "x509.hpp"

namespace viper::tls::cert
{

// Import types from x509 namespace
using viper::x509::Certificate;
using viper::x509::KeyType;

/**
 * @brief One embedded root CA entry.
 *
 * @details
 * Stores identifying metadata and a DER-encoded public key blob. The
 * certificate verifier uses this information as a trust anchor.
 */
struct RootCaEntry
{
    const char *name;       // Human-readable name
    const char *subject_cn; // Subject Common Name for matching
    KeyType key_type;       // RSA or ECDSA
    const u8 *public_key;   // DER-encoded public key
    usize public_key_len;   // Public key length
    const u8 *key_id;       // Subject Key Identifier (SHA-1 of public key)
};

/**
 * @brief Initialize the CA store.
 *
 * @details
 * The CA store is statically initialized, so this function is currently a
 * no-op. It exists to preserve an explicit initialization point for future
 * dynamic stores.
 */
void ca_store_init();

/**
 * @brief Find a root CA by subject common name.
 *
 * @details
 * Performs a case-insensitive comparison against each embedded root CA subject
 * CN and returns the first match.
 *
 * @param subject_cn Subject common name to look up.
 * @return Pointer to a root CA entry, or `nullptr` if not found.
 */
const RootCaEntry *ca_store_find_by_subject(const char *subject_cn);

/**
 * @brief Find a root CA by subject key identifier (SKID).
 *
 * @details
 * Compares the first 20 bytes of `key_id` against stored key identifiers.
 *
 * @param key_id Pointer to SKID bytes.
 * @param len Length of `key_id` in bytes.
 * @return Pointer to a root CA entry, or `nullptr` if not found.
 */
const RootCaEntry *ca_store_find_by_key_id(const u8 *key_id, usize len);

/**
 * @brief Get the number of embedded root CAs.
 *
 * @return Root CA count.
 */
usize ca_store_count();

/**
 * @brief Get a root CA entry by index.
 *
 * @param index Index in the range `[0, ca_store_count())`.
 * @return Root CA entry pointer, or `nullptr` if out of range.
 */
const RootCaEntry *ca_store_get(usize index);

/**
 * @brief Check whether a certificate corresponds to a trusted root.
 *
 * @details
 * Performs a lightweight trust check:
 * - The issuer CN must match a known embedded root subject.
 * - The certificate must be self-issued (subject CN equals issuer CN).
 *
 * @param cert Parsed certificate.
 * @return `true` if trusted root, otherwise `false`.
 */
bool ca_store_is_trusted_root(const Certificate *cert);

} // namespace viper::tls::cert
