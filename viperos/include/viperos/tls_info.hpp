//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viperos/tls_info.hpp
// Purpose: TLS session information structure for SYS_TLS_INFO syscall.
// Key invariants: ABI-stable; cipher IDs match IANA assignments.
// Ownership/Lifetime: Shared; included by kernel and user-space.
// Links: kernel/net/tls.hpp, user/libtls
//
//===----------------------------------------------------------------------===//

#pragma once

/**
 * @file tls_info.hpp
 * @brief Shared TLS session information returned by `SYS_TLS_INFO`.
 *
 * @details
 * This header defines a small, fixed-layout structure (@ref TLSInfo) used to
 * query the kernel's view of a TLS session. User-space can call `SYS_TLS_INFO`
 * (via the convenience wrapper `sys::tls_info`) to retrieve a snapshot of:
 * - Negotiated protocol version.
 * - Negotiated cipher suite.
 * - Whether the peer was verified (if verification is enabled).
 * - The hostname associated with the session (SNI / verification name).
 *
 * The information is intended for diagnostics and UI (e.g., showing the cipher
 * used by an HTTPS request). It is not meant to be a complete transcript of
 * the handshake and does not expose key material.
 */

/** @name TLS Protocol Versions
 *  @brief Values stored in @ref TLSInfo::protocol_version.
 *
 *  @details
 *  These values follow the TLS wire encoding for `ProtocolVersion` where
 *  TLS 1.0 is `0x0301`, TLS 1.2 is `0x0303`, and TLS 1.3 is `0x0304`.
 *  @{
 */
#define TLS_VERSION_1_0 0x0301 /**< TLS 1.0 (legacy). */
#define TLS_VERSION_1_2 0x0303 /**< TLS 1.2. */
#define TLS_VERSION_1_3 0x0304 /**< TLS 1.3. */
/** @} */

/** @name TLS Cipher Suites
 *  @brief Values stored in @ref TLSInfo::cipher_suite.
 *
 *  @details
 *  The numeric IDs are the IANA TLS cipher suite identifiers as transmitted on
 *  the wire. ViperOS currently focuses on modern AEAD suites typically used by
 *  TLS 1.3.
 *  @{
 */
#define TLS_CIPHER_AES_128_GCM_SHA256 0x1301       /**< TLS_AES_128_GCM_SHA256. */
#define TLS_CIPHER_AES_256_GCM_SHA384 0x1302       /**< TLS_AES_256_GCM_SHA384. */
#define TLS_CIPHER_CHACHA20_POLY1305_SHA256 0x1303 /**< TLS_CHACHA20_POLY1305_SHA256. */
/** @} */

/**
 * @brief Maximum number of bytes stored in @ref TLSInfo::hostname.
 *
 * @details
 * This buffer is used for the session's configured hostname (SNI and/or the
 * name used for certificate verification). The kernel truncates longer inputs
 * to ensure the structure has a stable fixed size.
 */
#define TLS_INFO_HOSTNAME_MAX 128

/**
 * @brief Kernel-provided summary of a TLS session.
 *
 * @details
 * This structure is filled by the kernel when `SYS_TLS_INFO` is invoked.
 * Fields represent the kernel's current view of the session. They are intended
 * for display and debugging:
 *
 * - `protocol_version` and `cipher_suite` are only meaningful after a
 *   successful handshake.
 * - `verified` is set when certificate verification was requested and the
 *   kernel considered the peer identity valid for the configured hostname.
 *   When verification is disabled (e.g., for bring-up), this field typically
 *   remains false.
 * - `connected` indicates whether the session is active/usable.
 * - `hostname` is the session hostname as known to the TLS layer (SNI / verify
 *   target).
 */
struct TLSInfo
{
    unsigned short protocol_version;      /**< Negotiated TLS version (e.g., `TLS_VERSION_1_3`). */
    unsigned short cipher_suite;          /**< Negotiated cipher suite ID (`TLS_CIPHER_*`). */
    unsigned char verified;               /**< Non-zero if the peer was verified for `hostname`. */
    unsigned char connected;              /**< Non-zero if the session is currently connected. */
    unsigned char _reserved[2];           /**< Reserved/padding for alignment; set to 0. */
    char hostname[TLS_INFO_HOSTNAME_MAX]; /**< Session hostname (SNI / verification name). */
};
