//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_tls_verify_internal.h
// Purpose: Shared includes, macros, and cert/ASN.1 helper declarations for the
//   TLS certificate-verification runtime, whose implementation is split into
//   rt_tls_verify_common.c (platform-neutral DER/SAN/hostname parsing),
//   rt_tls_verify_win.c (CryptoAPI chain + CertificateVerify), and
//   rt_tls_verify_posix.c (native macOS Security.framework / Linux manual path).
//
// Key invariants:
//   - The seven cert/ASN.1 helpers below are defined once in the common TU and
//     called from the platform TUs; cert_allows_tls_server_auth is implemented
//     per-platform and forward-declared here.
//   - On Windows, winsock2.h (which pulls windows.h) must precede wincrypt.h.
//
// Links: rt_tls_verify_common.c, rt_tls_verify_win.c, rt_tls_verify_posix.c,
//        rt_tls_internal.h
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_crypto.h"
#include "rt_ecdsa_p256.h"
#include "rt_network_time.inc"
#include "rt_rsa.h"
#include "rt_tls_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <strings.h>
#endif

// Platform-specific trust-store and crypto APIs (CS-1/CS-2/CS-3)
#if defined(_WIN32)
#ifndef _WINDOWS_
#error "windows.h must be included before wincrypt.h"
#endif
#include <bcrypt.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")
#endif

#if defined(__GNUC__) || defined(__clang__)
#define RT_TLS_MAYBE_UNUSED __attribute__((unused))
#else
#define RT_TLS_MAYBE_UNUSED
#endif

#define TLS_MAX_INTERMEDIATE_CERTS 16

/// @brief Forward declaration: check whether a certificate's Extended Key Usage allows TLS Web
/// Server Authentication.
/// @details Real implementation later in the file. Marked MAYBE_UNUSED so
///          builds that strip platform-specific verification paths still
///          link. Used by chain validation to reject leaf certs that don't
///          carry the id-kp-serverAuth EKU (1.3.6.1.5.5.7.3.1).
static RT_TLS_MAYBE_UNUSED int cert_allows_tls_server_auth(const uint8_t *cert_der,
                                                           size_t cert_len);

// Common cert/ASN.1 helpers defined in rt_tls_verify_common.c and shared with the
// platform-specific verification paths (rt_tls_verify_win.c / rt_tls_verify_posix.c).
int tls_next_certificate_entry(const uint8_t *list,
                               size_t list_len,
                               size_t *pos,
                               const uint8_t **cert_der,
                               size_t *cert_len);
int der_read_tlv(const uint8_t *buf, size_t buf_len, uint8_t *tag, size_t *val_len, size_t *hdr_len);
int der_params_absent_or_null(const uint8_t *params, size_t params_len);
const uint8_t *cert_get_subject(const uint8_t *cert_der, size_t cert_len, size_t *subject_len);
int cert_has_unsupported_critical_extension(const uint8_t *cert_der, size_t cert_len);
void build_cert_verify_message(const uint8_t transcript_hash[32], uint8_t out_content[130]);
size_t sig_scheme_hash_len(uint16_t sig_scheme);
