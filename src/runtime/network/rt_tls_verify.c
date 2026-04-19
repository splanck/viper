//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_tls_verify.c
// Purpose: X.509 certificate parsing, chain validation, hostname verification,
//          and CertificateVerify signature verification for TLS 1.3.
// Key invariants:
//   - Certificate chain validation on macOS/Linux uses the configured PEM CA
//     bundle override or the system PEM bundle, without platform TLS libraries.
//   - Hostname verification follows RFC 6125 (SAN takes precedence over CN).
//   - CertificateVerify and X.509 signature verification are native/in-tree on
//     macOS/Linux; Windows continues to use CryptoAPI.
// Ownership/Lifetime:
//   - All functions operate on the session's internal buffers; no heap allocation
//     except platform API temporaries that are released before return.
// Links: rt_tls_internal.h (shared struct), rt_tls.c (caller), rt_crypto.h
//
//===----------------------------------------------------------------------===//

#include "rt_crypto.h"
#include "rt_ecdsa_p256.h"
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
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")
#endif

#if defined(__GNUC__) || defined(__clang__)
#define RT_TLS_MAYBE_UNUSED __attribute__((unused))
#else
#define RT_TLS_MAYBE_UNUSED
#endif

#if !defined(_WIN32)
extern time_t timegm(struct tm *);
#endif

//=============================================================================
// Certificate Validation — CS-1, CS-2, CS-3
//=============================================================================

// ---------------------------------------------------------------------------
// B.2 — TLS 1.3 Certificate message parser
// ---------------------------------------------------------------------------

/// @brief Advance through one entry of a TLS 1.3 certificate_list.
/// @details Each entry is `[3-byte length][DER cert][2-byte ext length][extensions]`.
///          Bounds checks fire in three layers (each protecting the next):
///          1. The 5-byte header itself must fit (cert-length + ext-length
///             prefixes, before reading either).
///          2. The DER cert body must fit, *and* leave room for the 2-byte
///             extensions length.
///          3. The extensions body must fit within the list.
///          A zero `der_len` is rejected — TLS 1.3 forbids empty certificate
///          entries, and treating an empty entry as valid would let an attacker
///          push the parser past structural integrity for the entries that
///          follow. Updates `*pos` to point past the entry and writes the DER
///          pointer + length out-parameters on success.
/// @return 0 on success; -1 on any structural error (length underflow,
///         extensions past end of list, zero-length cert, null arguments).
static int tls_next_certificate_entry(
    const uint8_t *list, size_t list_len, size_t *pos, const uint8_t **cert_der, size_t *cert_len) {
    if (!list || !pos || !cert_der || !cert_len || *pos + 5 > list_len)
        return -1;

    size_t entry_pos = *pos;
    size_t der_len = ((size_t)list[entry_pos] << 16) | ((size_t)list[entry_pos + 1] << 8) |
                     (size_t)list[entry_pos + 2];
    entry_pos += 3;
    if (der_len == 0 || entry_pos + der_len + 2 > list_len)
        return -1;

    *cert_der = list + entry_pos;
    *cert_len = der_len;
    entry_pos += der_len;

    size_t ext_len = ((size_t)list[entry_pos] << 8) | (size_t)list[entry_pos + 1];
    entry_pos += 2;
    if (entry_pos + ext_len > list_len)
        return -1;

    *pos = entry_pos + ext_len;
    return 0;
}

/// @brief Parse the TLS 1.3 Certificate handshake message, store the leaf
///        certificate DER, and retain the full certificate_list entries.
///
/// Certificate message format (RFC 8446 §4.4.2):
///   1 byte:  certificate_request_context length
///   N bytes: context (ignored by client)
///   3 bytes: certificate_list total length
///   For each entry:
///     3 bytes: cert_data length
///     N bytes: DER-encoded certificate
///     2 bytes: extensions length
///     N bytes: certificate extensions (ignored)
int tls_parse_certificate_msg(rt_tls_session_t *session, const uint8_t *data, size_t len) {
    if (!data || len < 4) {
        session->error = "TLS: Certificate message too short";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    size_t pos = 0;

    // Skip certificate_request_context
    uint8_t ctx_len = data[pos++];
    if (pos + ctx_len > len) {
        session->error = "TLS: Certificate context overflows message";
        return RT_TLS_ERROR_HANDSHAKE;
    }
    pos += ctx_len;

    // Read certificate_list length (3-byte big-endian)
    if (pos + 3 > len) {
        session->error = "TLS: Certificate list length missing";
        return RT_TLS_ERROR_HANDSHAKE;
    }
    size_t list_len = ((size_t)data[pos] << 16) | ((size_t)data[pos + 1] << 8) | data[pos + 2];
    pos += 3;

    if (pos + list_len > len) {
        session->error = "TLS: Certificate list overflows message";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    if (list_len < 5) {
        session->error = "TLS: Certificate list too short for one entry";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    const uint8_t *list = data + pos;
    size_t list_pos = 0;
    const uint8_t *leaf_der = NULL;
    size_t leaf_len = 0;
    size_t cert_count = 0;

    while (list_pos < list_len) {
        const uint8_t *cert_der = NULL;
        size_t cert_len = 0;
        if (tls_next_certificate_entry(list, list_len, &list_pos, &cert_der, &cert_len) != 0) {
            session->error = "TLS: malformed certificate chain entry";
            return RT_TLS_ERROR_HANDSHAKE;
        }
        if (cert_count == 0) {
            leaf_der = cert_der;
            leaf_len = cert_len;
        }
        cert_count++;
    }

    if (!leaf_der || leaf_len == 0 || cert_count == 0) {
        session->error = "TLS: Certificate message contained no leaf certificate";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    if (leaf_len > sizeof(session->server_cert_der)) {
        session->error = "TLS: Certificate DER too large for validation buffer";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    uint8_t *list_copy = (uint8_t *)malloc(list_len);
    if (!list_copy) {
        session->error = "TLS: memory allocation failed for certificate chain";
        return RT_TLS_ERROR_MEMORY;
    }
    memcpy(list_copy, list, list_len);

    if (session->server_cert_list) {
        free(session->server_cert_list);
        session->server_cert_list = NULL;
        session->server_cert_list_len = 0;
        session->server_cert_count = 0;
    }

    memcpy(session->server_cert_der, leaf_der, leaf_len);
    session->server_cert_der_len = leaf_len;
    session->server_cert_list = list_copy;
    session->server_cert_list_len = list_len;
    session->server_cert_count = cert_count;
    return RT_TLS_OK;
}

// ---------------------------------------------------------------------------
// B.3 — ASN.1 DER helpers and X.509 hostname extraction
// ---------------------------------------------------------------------------

/// @brief Read one ASN.1 TLV (tag-length-value) header.
/// @param buf     Buffer to parse.
/// @param buf_len Remaining bytes in buf.
/// @param tag     Output: the tag byte.
/// @param val_len Output: the length of the value field.
/// @param hdr_len Output: the total header length (tag + length octets).
/// @return 0 on success, -1 on error.
static int der_read_tlv(
    const uint8_t *buf, size_t buf_len, uint8_t *tag, size_t *val_len, size_t *hdr_len) {
    if (buf_len < 2)
        return -1;

    *tag = buf[0];

    uint8_t l0 = buf[1];
    if (l0 < 0x80) {
        *val_len = l0;
        *hdr_len = 2;
    } else {
        size_t num_len_bytes = l0 & 0x7F;
        if (num_len_bytes == 0 || num_len_bytes > 4 || 2 + num_len_bytes > buf_len)
            return -1;

        size_t v = 0;
        for (size_t i = 0; i < num_len_bytes; i++)
            v = (v << 8) | buf[2 + i];

        *val_len = v;
        *hdr_len = 2 + num_len_bytes;
    }

    if (*hdr_len + *val_len > buf_len)
        return -1;

    return 0;
}

/// @brief Compare buf[0..oid_len-1] to the encoded OID bytes.
static int oid_matches(const uint8_t *buf,
                       size_t buf_len,
                       const uint8_t *oid_val,
                       size_t oid_val_len) {
    return buf_len == oid_val_len && memcmp(buf, oid_val, oid_val_len) == 0;
}

// Known OID encoded values (value bytes only, after the OID tag+length)
static const uint8_t OID_COMMON_NAME[] = {0x55, 0x04, 0x03};      // 2.5.4.3
static const uint8_t OID_SUBJECT_ALT_NAME[] = {0x55, 0x1d, 0x11}; // 2.5.29.17

/// @brief Extract DNS names from SubjectAltName extension value.
/// @param ext_val Points to the value bytes of the SAN extension (after the OCTET STRING wrapper).
/// @param ext_len Length of ext_val.
/// @param san_out Pre-allocated array for DNS name strings.
/// @param max_names Maximum number of names to return.
/// @param count Output: number of names found.
static void extract_san_from_ext_value(
    const uint8_t *ext_val, size_t ext_len, char san_out[][256], int max_names, int *count) {
    *count = 0;

    // SubjectAltName is an OCTET STRING containing a GeneralNames SEQUENCE
    uint8_t t;
    size_t vl, hl;
    if (der_read_tlv(ext_val, ext_len, &t, &vl, &hl) != 0 || t != 0x04 /* OCTET STRING */)
        return;

    const uint8_t *inner = ext_val + hl;
    size_t inner_len = vl;

    // GeneralNames SEQUENCE
    if (der_read_tlv(inner, inner_len, &t, &vl, &hl) != 0 || t != 0x30 /* SEQUENCE */)
        return;

    const uint8_t *names = inner + hl;
    size_t names_len = vl;
    size_t pos = 0;

    while (pos < names_len && *count < max_names) {
        if (der_read_tlv(names + pos, names_len - pos, &t, &vl, &hl) != 0)
            break;

        // dNSName is context tag [2] = 0x82
        if (t == 0x82 && vl > 0 && vl < 256) {
            memcpy(san_out[*count], names + pos + hl, vl);
            san_out[*count][vl] = '\0';
            (*count)++;
        }

        pos += hl + vl;
    }
}

/// @brief Detect IPv4 / IPv6 literal hostnames and copy into binary form.
///
/// Returns 1 if the hostname is an IP literal (with the binary
/// representation in `ip_out` and its length 4 or 16), 0 otherwise.
/// Used so SAN matching can compare against `iPAddress` entries
/// instead of (incorrectly) trying DNS-name matching.
static int tls_hostname_is_ip_literal(const char *hostname, uint8_t ip_out[16], size_t *ip_len) {
    struct in_addr ipv4;
    struct in6_addr ipv6;

    if (!hostname || !ip_out || !ip_len)
        return 0;

    if (inet_pton(AF_INET, hostname, &ipv4) == 1) {
        memcpy(ip_out, &ipv4, 4);
        *ip_len = 4;
        return 1;
    }

    if (inet_pton(AF_INET6, hostname, &ipv6) == 1) {
        memcpy(ip_out, &ipv6, 16);
        *ip_len = 16;
        return 1;
    }

    return 0;
}

/// @brief Walk a SAN extension value looking for an `iPAddress` entry that matches.
///
/// SAN entries are tagged: 0x82 = dNSName, 0x87 = iPAddress. This
/// helper iterates entries, sets `*saw_ip_san=1` if any IP-typed entry
/// is present, and returns 1 if any entry's bytes equal `expected_ip`.
static int san_ext_has_ip_match(const uint8_t *ext_val,
                                size_t ext_len,
                                const uint8_t *expected_ip,
                                size_t expected_ip_len,
                                int *saw_ip_san) {
    uint8_t t;
    size_t vl, hl;
    if (saw_ip_san)
        *saw_ip_san = 0;

    if (der_read_tlv(ext_val, ext_len, &t, &vl, &hl) != 0 || t != 0x04)
        return 0;

    const uint8_t *inner = ext_val + hl;
    size_t inner_len = vl;
    if (der_read_tlv(inner, inner_len, &t, &vl, &hl) != 0 || t != 0x30)
        return 0;

    const uint8_t *names = inner + hl;
    size_t names_len = vl;
    size_t pos = 0;
    while (pos < names_len) {
        if (der_read_tlv(names + pos, names_len - pos, &t, &vl, &hl) != 0)
            break;
        if (t == 0x87) {
            if (saw_ip_san)
                *saw_ip_san = 1;
            if (vl == expected_ip_len &&
                memcmp(names + pos + hl, expected_ip, expected_ip_len) == 0)
                return 1;
        }
        pos += hl + vl;
    }
    return 0;
}

/// @brief Top-level X.509 walker: find the SAN extension and run the IP match.
///
/// Drills through Certificate → TBSCertificate → Extensions context-tag
/// 3 ([3] EXPLICIT) → individual Extension SEQUENCEs → SAN OID match
/// → SAN value. Uses iterative DER parsing (no recursion) to bound
/// stack use on adversarial inputs.
static int tls_cert_has_matching_ip_san(const uint8_t *der,
                                        size_t der_len,
                                        const uint8_t *expected_ip,
                                        size_t expected_ip_len,
                                        int *saw_ip_san) {
    uint8_t t;
    size_t vl, hl;
    if (saw_ip_san)
        *saw_ip_san = 0;

    if (der_read_tlv(der, der_len, &t, &vl, &hl) != 0 || t != 0x30)
        return 0;

    const uint8_t *cert_val = der + hl;
    if (der_read_tlv(cert_val, vl, &t, &vl, &hl) != 0 || t != 0x30)
        return 0;

    const uint8_t *tbs_val = cert_val + hl;
    size_t tbs_len = vl;
    size_t pos = 0;

    while (pos < tbs_len) {
        if (der_read_tlv(tbs_val + pos, tbs_len - pos, &t, &vl, &hl) != 0)
            break;
        if (t == 0xA3) {
            const uint8_t *exts_wrap = tbs_val + pos + hl;
            uint8_t t2;
            size_t vl2, hl2;
            if (der_read_tlv(exts_wrap, vl, &t2, &vl2, &hl2) != 0 || t2 != 0x30)
                break;

            const uint8_t *exts = exts_wrap + hl2;
            size_t exts_len = vl2;
            size_t ep = 0;
            while (ep < exts_len) {
                uint8_t t3;
                size_t vl3, hl3;
                if (der_read_tlv(exts + ep, exts_len - ep, &t3, &vl3, &hl3) != 0)
                    break;
                if (t3 == 0x30) {
                    const uint8_t *ext = exts + ep + hl3;
                    uint8_t t4;
                    size_t vl4, hl4;
                    if (der_read_tlv(ext, vl3, &t4, &vl4, &hl4) == 0 && t4 == 0x06 &&
                        oid_matches(ext + hl4, vl4, OID_SUBJECT_ALT_NAME, 3)) {
                        size_t after_oid = hl4 + vl4;
                        if (after_oid < vl3) {
                            uint8_t nt;
                            size_t nvl, nhl;
                            if (der_read_tlv(ext + after_oid, vl3 - after_oid, &nt, &nvl, &nhl) ==
                                0) {
                                if (nt == 0x01)
                                    after_oid += nhl + nvl;
                                if (after_oid < vl3) {
                                    return san_ext_has_ip_match(ext + after_oid,
                                                                vl3 - after_oid,
                                                                expected_ip,
                                                                expected_ip_len,
                                                                saw_ip_san);
                                }
                            }
                        }
                    }
                }
                ep += hl3 + vl3;
            }
            break;
        }
        pos += hl + vl;
    }

    return 0;
}

/// @brief Extract SubjectAltName dNSName entries from a certificate DER.
/// @details Walks the ASN.1 tree: Certificate → TBSCertificate → Extensions
///          ([3] EXPLICIT) → looking for the SubjectAltName extension
///          (OID 2.5.29.17). Within SAN, only the dNSName variant ([2] IMPLICIT
///          IA5String) is extracted; iPAddress and other GeneralName variants
///          are skipped. Each name is copied null-terminated into `san_out[i]`,
///          truncated to 255 bytes (SAN entries longer than that are skipped
///          silently — a name that doesn't fit in the buffer can't be safely
///          compared against a hostname anyway).
///
///          SAN is the canonical hostname list per RFC 6125 § 6.4; modern
///          certificate authorities are required to populate it. CN-based
///          matching is fallback only and should not be consulted when SAN
///          is present (caller's responsibility — `tls_verify_hostname` enforces
///          this ordering).
/// @param der Pointer to the certificate's DER bytes.
/// @param der_len Length of the DER buffer.
/// @param san_out Output array of fixed-size 256-byte name buffers.
/// @param max_names Capacity of `san_out`.
/// @return Number of dNSName entries written (0 if SAN extension absent or
///         no dNSName variants present).
int tls_extract_san_names(const uint8_t *der, size_t der_len, char san_out[][256], int max_names) {
    int count = 0;

    // Certificate SEQUENCE
    uint8_t t;
    size_t vl, hl;
    if (der_read_tlv(der, der_len, &t, &vl, &hl) != 0 || t != 0x30)
        return 0;

    // TBSCertificate SEQUENCE (first child of Certificate)
    const uint8_t *cert_val = der + hl;
    if (der_read_tlv(cert_val, vl, &t, &vl, &hl) != 0 || t != 0x30)
        return 0;

    // Walk TBSCertificate to find extensions [3] EXPLICIT context tag
    const uint8_t *tbs_val = cert_val + hl;
    size_t tbs_len = vl;
    size_t pos = 0;

    while (pos < tbs_len) {
        if (der_read_tlv(tbs_val + pos, tbs_len - pos, &t, &vl, &hl) != 0)
            break;

        // Extensions is [3] EXPLICIT = 0xA3
        if (t == 0xA3) {
            // Inside [3]: one SEQUENCE of Extensions
            const uint8_t *exts_wrap = tbs_val + pos + hl;
            uint8_t t2;
            size_t vl2, hl2;
            if (der_read_tlv(exts_wrap, vl, &t2, &vl2, &hl2) != 0 || t2 != 0x30)
                break;

            const uint8_t *exts = exts_wrap + hl2;
            size_t exts_len = vl2;
            size_t ep = 0;

            while (ep < exts_len && count < max_names) {
                // Each Extension is a SEQUENCE { OID, [BOOL], OCTET STRING }
                uint8_t t3;
                size_t vl3, hl3;
                if (der_read_tlv(exts + ep, exts_len - ep, &t3, &vl3, &hl3) != 0)
                    break;

                if (t3 == 0x30) {
                    const uint8_t *ext = exts + ep + hl3;
                    uint8_t t4;
                    size_t vl4, hl4;

                    // OID
                    if (der_read_tlv(ext, vl3, &t4, &vl4, &hl4) == 0 && t4 == 0x06) {
                        if (oid_matches(ext + hl4, vl4, OID_SUBJECT_ALT_NAME, 3)) {
                            // Skip optional BOOLEAN (critical flag)
                            size_t after_oid = hl4 + vl4;
                            if (after_oid < vl3) {
                                uint8_t nt;
                                size_t nvl, nhl;
                                if (der_read_tlv(
                                        ext + after_oid, vl3 - after_oid, &nt, &nvl, &nhl) == 0) {
                                    if (nt == 0x01) // BOOLEAN (critical flag) — skip
                                        after_oid += nhl + nvl;
                                    // Now should be at OCTET STRING
                                    if (after_oid < vl3)
                                        extract_san_from_ext_value(ext + after_oid,
                                                                   vl3 - after_oid,
                                                                   san_out,
                                                                   max_names,
                                                                   &count);
                                }
                            }
                        }
                    }
                }

                ep += hl3 + vl3;
            }
            break; // Extensions found and processed
        }

        pos += hl + vl;
    }

    return count;
}

/// @brief Extract the CommonName (CN) attribute from the certificate Subject.
/// @details Walks the ASN.1 tree: Certificate → TBSCertificate → looking for
///          a SEQUENCE whose children include a SET containing an
///          AttributeTypeAndValue with OID 2.5.4.3 (commonName). The first
///          matching CN value is copied null-terminated into `cn_out`,
///          truncated at 255 bytes.
///
///          **CN-based hostname matching is a legacy fallback** per RFC 6125
///          § 6.4.4 and is deprecated in favour of SubjectAltName dNSName
///          entries. Callers should only consult the CN when no SAN extension
///          is present — `tls_verify_hostname` enforces that ordering. CAs
///          that comply with the CA/Browser Forum Baseline Requirements no
///          longer issue certificates that rely on CN for hostname identity,
///          but this function is retained for compatibility with older
///          internal CAs and self-signed certificates.
/// @param der Pointer to the certificate's DER bytes.
/// @param der_len Length of the DER buffer.
/// @param cn_out Output 256-byte buffer for the CN string.
/// @return 1 if a CN was found and written, 0 if no CN is present or it
///         would not fit in the buffer.
int tls_extract_cn(const uint8_t *der, size_t der_len, char cn_out[256]) {
    // Certificate SEQUENCE
    uint8_t t;
    size_t vl, hl;
    if (der_read_tlv(der, der_len, &t, &vl, &hl) != 0 || t != 0x30)
        return 0;

    // TBSCertificate SEQUENCE
    const uint8_t *cert_val = der + hl;
    if (der_read_tlv(cert_val, vl, &t, &vl, &hl) != 0 || t != 0x30)
        return 0;

    const uint8_t *tbs_val = cert_val + hl;
    size_t tbs_len = vl;
    size_t pos = 0;

    while (pos < tbs_len) {
        if (der_read_tlv(tbs_val + pos, tbs_len - pos, &t, &vl, &hl) != 0)
            break;

        // Subject is a SEQUENCE at index 5 in TBS (0:version, 1:serial, 2:alg, 3:issuer,
        // 4:validity, 5:subject) Walk until we find two consecutive SEQUENCEs — the second is
        // Subject (Actually, both Issuer and Subject are SEQUENCE/SET, so we need to count.
        //  Simpler: find any SEQUENCE whose children contain OID 2.5.4.3)
        if (t == 0x30) // SEQUENCE — could be Issuer or Subject
        {
            const uint8_t *seq_val = tbs_val + pos + hl;
            size_t seq_len = vl;
            size_t sp = 0;

            // Each child of Issuer/Subject is a SET containing AttributeTypeAndValue
            while (sp < seq_len) {
                uint8_t ts;
                size_t vls, hls;
                if (der_read_tlv(seq_val + sp, seq_len - sp, &ts, &vls, &hls) != 0)
                    break;

                if (ts == 0x31) // SET
                {
                    // AttributeTypeAndValue SEQUENCE
                    uint8_t ta;
                    size_t vla, hla;
                    if (der_read_tlv(seq_val + sp + hls, vls, &ta, &vla, &hla) == 0 && ta == 0x30) {
                        const uint8_t *atv = seq_val + sp + hls + hla;

                        // OID
                        uint8_t to;
                        size_t vlo, hlo;
                        if (der_read_tlv(atv, vla, &to, &vlo, &hlo) == 0 && to == 0x06) {
                            if (oid_matches(atv + hlo, vlo, OID_COMMON_NAME, 3)) {
                                // Value: UTF8String (0x0C), PrintableString (0x13), IA5String
                                // (0x16), etc.
                                const uint8_t *val_start = atv + hlo + vlo;
                                size_t val_remaining = vla - hlo - vlo;
                                uint8_t tv;
                                size_t vlv, hlv;
                                if (der_read_tlv(val_start, val_remaining, &tv, &vlv, &hlv) == 0) {
                                    if (vlv > 0 && vlv < 256) {
                                        memcpy(cn_out, val_start + hlv, vlv);
                                        cn_out[vlv] = '\0';
                                        return 1;
                                    }
                                }
                            }
                        }
                    }
                }

                sp += hls + vls;
            }
        }

        pos += hl + vl;
    }

    return 0;
}

/// @brief Match a hostname against a certificate name pattern (RFC 6125 § 6.4).
/// @details Two cases are supported:
///          - **Exact match**: `"example.com"` vs `"example.com"` (case-insensitive
///            via `strcasecmp`).
///          - **Single-label wildcard**: `"*.example.com"` vs `"foo.example.com"`.
///            The `*` must be the entire leftmost label (so `"foo*.example.com"`
///            and `"*foo.example.com"` are intentionally rejected — partial-label
///            wildcards are a CA/Browser Forum prohibition because they enable
///            attacks like a cert for `"*.com"`). The wildcard label matches
///            exactly one DNS label and never matches `.` itself, so
///            `"*.example.com"` does not match `"a.b.example.com"`.
///
///          Returns 0 on any structural mismatch, including null inputs and
///          the case where the hostname has no `.` (a hostname with no dots
///          can't match a `*.X` pattern by construction).
/// @param pattern Pattern from a certificate dNSName or CN.
/// @param hostname User-supplied hostname being verified.
/// @return 1 on match, 0 otherwise.
int tls_match_hostname(const char *pattern, const char *hostname) {
    if (!pattern || !hostname)
        return 0;

    if (pattern[0] == '*' && pattern[1] == '.') {
        // Wildcard: *.example.com
        const char *suffix = pattern + 2; // "example.com"
        const char *dot = strchr(hostname, '.');
        if (!dot)
            return 0; // hostname has no dot — can't match *.X

        // hostname suffix after first dot must match pattern suffix
        const char *host_suffix = dot + 1;
        if (strcasecmp(host_suffix, suffix) != 0)
            return 0;

        // The wildcard label must not contain a dot (one label only)
        // Already guaranteed since we took the first dot.
        return 1;
    }

    return strcasecmp(pattern, hostname) == 0 ? 1 : 0;
}

// Maximum number of SAN DNS names to check
#define TLS_MAX_SAN_NAMES 64

/// @brief Verify that the session's intended hostname matches the leaf
///        certificate (RFC 6125 § 6).
/// @details The verification ordering is load-bearing for security:
///          1. **IP literals** (e.g., `"192.0.2.1"`, `"::1"`) match exclusively
///             against the certificate's iPAddress SANs. Falling back to dNSName
///             SAN or CN matching for an IP literal would be a bypass —
///             `"192.0.2.1"` should never match a cert issued for the literal
///             string `"192.0.2.1"` as a DNS name.
///          2. **DNS hostnames** check dNSName SAN entries first via
///             `tls_extract_san_names` + `tls_match_hostname` against each one.
///          3. **CN fallback** is consulted *only* when no SAN extension is
///             present at all (per RFC 6125 § 6.4.4 — CN must be ignored when
///             SAN is present, even if SAN doesn't list any dNSName entries).
///          On mismatch the function sets `session->error` to a diagnostic
///          message and returns RT_TLS_ERROR_HANDSHAKE so the caller can
///          surface the underlying reason via `rt_tls_last_error()`.
/// @param session Active TLS session with `server_cert_der` populated.
/// @return 0 on match, RT_TLS_ERROR_HANDSHAKE on mismatch or missing data.
int tls_verify_hostname(rt_tls_session_t *session) {
    if (!session->server_cert_der_len) {
        session->error = "TLS: no certificate stored for hostname verification";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    const char *host = session->hostname;
    uint8_t ip_literal[16];
    size_t ip_literal_len = 0;

    if (tls_hostname_is_ip_literal(host, ip_literal, &ip_literal_len)) {
        int saw_ip_san = 0;
        if (tls_cert_has_matching_ip_san(session->server_cert_der,
                                         session->server_cert_der_len,
                                         ip_literal,
                                         ip_literal_len,
                                         &saw_ip_san)) {
            return RT_TLS_OK;
        }
        session->error = saw_ip_san ? "TLS: certificate IP SAN mismatch"
                                    : "TLS: certificate does not contain an IP SubjectAltName";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Try SubjectAltName first (RFC 6125 §6.4: SAN takes precedence over CN)
    char san_names[TLS_MAX_SAN_NAMES][256];
    int san_count = tls_extract_san_names(
        session->server_cert_der, session->server_cert_der_len, san_names, TLS_MAX_SAN_NAMES);

    if (san_count > 0) {
        for (int i = 0; i < san_count; i++) {
            if (tls_match_hostname(san_names[i], host))
                return RT_TLS_OK;
        }
        session->error = "TLS: certificate hostname mismatch (SAN did not match)";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Fall back to CommonName if no SAN
    char cn[256] = {0};
    if (tls_extract_cn(session->server_cert_der, session->server_cert_der_len, cn)) {
        if (tls_match_hostname(cn, host))
            return RT_TLS_OK;
        session->error = "TLS: certificate hostname mismatch (CN did not match)";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    session->error = "TLS: certificate contains no SAN or CN for hostname verification";
    return RT_TLS_ERROR_HANDSHAKE;
}

// ---------------------------------------------------------------------------
// B.4 — OS trust store chain validation
// ---------------------------------------------------------------------------

#if defined(_WIN32)

int tls_verify_chain(rt_tls_session_t *session) {
    if (!session->server_cert_der_len) {
        session->error = "TLS: no certificate to validate";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    PCCERT_CONTEXT cert_ctx = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                                           session->server_cert_der,
                                                           (DWORD)session->server_cert_der_len);

    if (!cert_ctx) {
        session->error = "TLS: could not parse DER certificate (Windows)";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    CERT_CHAIN_PARA chain_para = {0};
    chain_para.cbSize = sizeof(chain_para);

    HCERTSTORE extra_store =
        CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, NULL);
    if (!extra_store) {
        CertFreeCertificateContext(cert_ctx);
        session->error = "TLS: could not create intermediate certificate store";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    if (session->server_cert_list && session->server_cert_list_len > 0) {
        size_t list_pos = 0;
        size_t cert_index = 0;
        while (list_pos < session->server_cert_list_len) {
            const uint8_t *cert_der = NULL;
            size_t cert_len = 0;
            if (tls_next_certificate_entry(session->server_cert_list,
                                           session->server_cert_list_len,
                                           &list_pos,
                                           &cert_der,
                                           &cert_len) != 0) {
                CertCloseStore(extra_store, 0);
                CertFreeCertificateContext(cert_ctx);
                session->error = "TLS: malformed certificate chain";
                return RT_TLS_ERROR_HANDSHAKE;
            }
            if (cert_index++ == 0)
                continue;
            if (!CertAddEncodedCertificateToStore(extra_store,
                                                  X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                                  cert_der,
                                                  (DWORD)cert_len,
                                                  CERT_STORE_ADD_ALWAYS,
                                                  NULL)) {
                CertCloseStore(extra_store, 0);
                CertFreeCertificateContext(cert_ctx);
                session->error = "TLS: could not add intermediate certificate to store";
                return RT_TLS_ERROR_HANDSHAKE;
            }
        }
    }

    PCCERT_CHAIN_CONTEXT chain_ctx = NULL;
    BOOL ok = CertGetCertificateChain(
        NULL, cert_ctx, NULL, extra_store, &chain_para, 0, NULL, &chain_ctx);
    CertCloseStore(extra_store, 0);

    if (!ok || !chain_ctx) {
        CertFreeCertificateContext(cert_ctx);
        session->error = "TLS: CertGetCertificateChain failed";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Convert hostname to wide string for SSL policy
    wchar_t whostname[256] = {0};
    size_t host_len = strlen(session->hostname);
    if (host_len >= 255)
        host_len = 255;
    for (size_t i = 0; i < host_len; i++)
        whostname[i] = (wchar_t)(unsigned char)session->hostname[i];

    SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl_policy = {0};
    ssl_policy.cbSize = sizeof(ssl_policy);
    ssl_policy.dwAuthType = AUTHTYPE_SERVER;
    ssl_policy.fdwChecks = 0;
    ssl_policy.pwszServerName = whostname;

    CERT_CHAIN_POLICY_PARA policy_para = {0};
    policy_para.cbSize = sizeof(policy_para);
    policy_para.pvExtraPolicyPara = &ssl_policy;

    CERT_CHAIN_POLICY_STATUS policy_status = {0};
    policy_status.cbSize = sizeof(policy_status);

    ok = CertVerifyCertificateChainPolicy(
        CERT_CHAIN_POLICY_SSL, chain_ctx, &policy_para, &policy_status);

    CertFreeCertificateChain(chain_ctx);
    CertFreeCertificateContext(cert_ctx);

    if (!ok || policy_status.dwError != 0) {
        session->error = "TLS: certificate chain validation failed (Windows)";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    return RT_TLS_OK;
}

#else // Native macOS/Linux

static RT_TLS_MAYBE_UNUSED const char *find_ca_bundle(void) {
    static const char *bundles[] = {"/etc/ssl/certs/ca-certificates.crt",
                                    "/etc/pki/tls/certs/ca-bundle.crt",
                                    "/etc/ssl/ca-bundle.pem",
                                    "/etc/ssl/cert.pem",
                                    NULL};
    for (int i = 0; bundles[i]; i++) {
        FILE *f = fopen(bundles[i], "rb");
        if (f) {
            fclose(f);
            return bundles[i];
        }
    }
    return NULL;
}

static RT_TLS_MAYBE_UNUSED size_t pem_decode_cert(const char *pem_b64,
                                                  size_t b64_len,
                                                  uint8_t *out_der,
                                                  size_t max_der) {
    static const int8_t b64tab[256] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62,
        -1, -1, -1, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1, -1, 0,
        1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
        23, 24, 25, -1, -1, -1, -1, -1, -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
        39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    size_t out_len = 0;
    uint32_t acc = 0;
    int bits = 0;

    for (size_t i = 0; i < b64_len; i++) {
        unsigned char c = (unsigned char)pem_b64[i];
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t')
            continue;
        if (c == '=')
            break;
        if (b64tab[c] < 0)
            continue;
        acc = (acc << 6) | (uint32_t)b64tab[c];
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (out_len >= max_der)
                return 0;
            out_der[out_len++] = (uint8_t)((acc >> bits) & 0xFF);
        }
    }
    return out_len;
}

static RT_TLS_MAYBE_UNUSED int der_names_equal(const uint8_t *a_der,
                                               size_t a_len,
                                               const uint8_t *b_der,
                                               size_t b_len) {
    return a_len == b_len && memcmp(a_der, b_der, a_len) == 0;
}

static RT_TLS_MAYBE_UNUSED const uint8_t *cert_get_subject(const uint8_t *cert_der,
                                                           size_t cert_len,
                                                           size_t *subject_len) {
    uint8_t t;
    size_t vl, hl, cert_hl, tbs_hl;
    const uint8_t *tbs = NULL;
    size_t tbs_len = 0;
    size_t pos = 0;

    if (!subject_len || der_read_tlv(cert_der, cert_len, &t, &vl, &cert_hl) != 0 || t != 0x30)
        return NULL;
    if (der_read_tlv(cert_der + cert_hl, vl, &t, &tbs_len, &tbs_hl) != 0 || t != 0x30)
        return NULL;
    tbs = cert_der + cert_hl + tbs_hl;

    if (der_read_tlv(tbs + pos, tbs_len - pos, &t, &vl, &hl) != 0)
        return NULL;
    if (t == 0xA0)
        pos += hl + vl;

    for (int i = 0; i < 4; i++) {
        if (der_read_tlv(tbs + pos, tbs_len - pos, &t, &vl, &hl) != 0)
            return NULL;
        pos += hl + vl;
    }
    if (der_read_tlv(tbs + pos, tbs_len - pos, &t, &vl, &hl) != 0 || t != 0x30)
        return NULL;
    *subject_len = hl + vl;
    return tbs + pos;
}

static RT_TLS_MAYBE_UNUSED const uint8_t *cert_get_issuer(const uint8_t *cert_der,
                                                          size_t cert_len,
                                                          size_t *issuer_len) {
    uint8_t t;
    size_t vl, hl, cert_hl, tbs_hl;
    const uint8_t *tbs = NULL;
    size_t tbs_len = 0;
    size_t pos = 0;

    if (!issuer_len || der_read_tlv(cert_der, cert_len, &t, &vl, &cert_hl) != 0 || t != 0x30)
        return NULL;
    if (der_read_tlv(cert_der + cert_hl, vl, &t, &tbs_len, &tbs_hl) != 0 || t != 0x30)
        return NULL;
    tbs = cert_der + cert_hl + tbs_hl;

    if (der_read_tlv(tbs + pos, tbs_len - pos, &t, &vl, &hl) != 0)
        return NULL;
    if (t == 0xA0)
        pos += hl + vl;

    for (int i = 0; i < 2; i++) {
        if (der_read_tlv(tbs + pos, tbs_len - pos, &t, &vl, &hl) != 0)
            return NULL;
        pos += hl + vl;
    }
    if (der_read_tlv(tbs + pos, tbs_len - pos, &t, &vl, &hl) != 0 || t != 0x30)
        return NULL;
    *issuer_len = hl + vl;
    return tbs + pos;
}

static time_t parse_der_time(const uint8_t *data, size_t len, uint8_t tag) {
    struct tm tm_val;
    const char *s = (const char *)data;
    int pos = 0;

    memset(&tm_val, 0, sizeof(tm_val));
    if (tag == 0x17 && len >= 12) {
        int yy = (s[0] - '0') * 10 + (s[1] - '0');
        tm_val.tm_year = (yy >= 50) ? yy : (100 + yy);
        pos = 2;
    } else if (tag == 0x18 && len >= 14) {
        int yyyy = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
        tm_val.tm_year = yyyy - 1900;
        pos = 4;
    } else {
        return (time_t)-1;
    }

    tm_val.tm_mon = (s[pos] - '0') * 10 + (s[pos + 1] - '0') - 1;
    tm_val.tm_mday = (s[pos + 2] - '0') * 10 + (s[pos + 3] - '0');
    tm_val.tm_hour = (s[pos + 4] - '0') * 10 + (s[pos + 5] - '0');
    tm_val.tm_min = (s[pos + 6] - '0') * 10 + (s[pos + 7] - '0');
    tm_val.tm_sec = (s[pos + 8] - '0') * 10 + (s[pos + 9] - '0');
    return timegm(&tm_val);
}

static RT_TLS_MAYBE_UNUSED int cert_check_expiry(const uint8_t *cert_der, size_t cert_len) {
    uint8_t t;
    size_t vl, hl, cert_hl, tbs_hl;
    const uint8_t *tbs = NULL;
    size_t tbs_len = 0;
    size_t pos = 0;

    if (der_read_tlv(cert_der, cert_len, &t, &vl, &cert_hl) != 0 || t != 0x30) {
        return -1;
    }
    if (der_read_tlv(cert_der + cert_hl, vl, &t, &tbs_len, &tbs_hl) != 0 || t != 0x30) {
        return -1;
    }
    tbs = cert_der + cert_hl + tbs_hl;

    if (der_read_tlv(tbs + pos, tbs_len - pos, &t, &vl, &hl) != 0) {
        return -1;
    }
    if (t == 0xA0)
        pos += hl + vl;

    for (int i = 0; i < 3; i++) {
        if (der_read_tlv(tbs + pos, tbs_len - pos, &t, &vl, &hl) != 0) {
            return -1;
        }
        pos += hl + vl;
    }

    if (der_read_tlv(tbs + pos, tbs_len - pos, &t, &vl, &hl) != 0 || t != 0x30) {
        return -1;
    }
    {
        const uint8_t *validity = tbs + pos + hl;
        size_t validity_len = vl;
        size_t vpos = 0;
        time_t not_before;
        time_t not_after;
        if (der_read_tlv(validity + vpos, validity_len - vpos, &t, &vl, &hl) != 0) {
            return -1;
        }
        not_before = parse_der_time(validity + vpos + hl, vl, t);
        vpos += hl + vl;
        if (der_read_tlv(validity + vpos, validity_len - vpos, &t, &vl, &hl) != 0) {
            return -1;
        }
        not_after = parse_der_time(validity + vpos + hl, vl, t);
        if (not_before == (time_t)-1 || not_after == (time_t)-1) {
            return -1;
        }
        if (time(NULL) < not_before || time(NULL) > not_after) {
            return -1;
        }
        return 0;
    }
}

typedef enum {
    TLS_CERT_SIG_NONE = 0,
    TLS_CERT_SIG_RSA_PKCS1 = 1,
    TLS_CERT_SIG_RSA_PSS = 2,
    TLS_CERT_SIG_ECDSA_P256 = 3,
} tls_cert_sig_kind_t;

typedef struct {
    tls_cert_sig_kind_t kind;
    rt_rsa_hash_t hash_id;
} tls_cert_sig_alg_t;

static RT_TLS_MAYBE_UNUSED size_t hash_len_from_id(rt_rsa_hash_t hash_id);

static RT_TLS_MAYBE_UNUSED int cert_is_self_signed(const uint8_t *cert_der, size_t cert_len) {
    size_t subject_len = 0;
    size_t issuer_len = 0;
    const uint8_t *subject = cert_get_subject(cert_der, cert_len, &subject_len);
    const uint8_t *issuer = cert_get_issuer(cert_der, cert_len, &issuer_len);
    return subject && issuer && der_names_equal(subject, subject_len, issuer, issuer_len);
}

static RT_TLS_MAYBE_UNUSED int cert_is_ca(const uint8_t *cert_der, size_t cert_len) {
    static const uint8_t OID_BASIC_CONSTRAINTS[] = {0x55, 0x1d, 0x13};
    static const uint8_t OID_KEY_USAGE[] = {0x55, 0x1d, 0x0f};
    uint8_t tag;
    size_t vl, hl;
    const uint8_t *p = cert_der;
    size_t rem = cert_len;
    const uint8_t *tbs = NULL;
    size_t tbs_rem = 0;
    int saw_basic_constraints = 0;
    int is_ca = 0;
    int key_usage_allows = 1;

    if (der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    p += hl;
    rem = vl;
    if (der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    tbs = p + hl;
    tbs_rem = vl;

    if (der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0)
        return 0;
    if (tag == 0xA0) {
        tbs += hl + vl;
        tbs_rem -= hl + vl;
    }

    for (int i = 0; i < 6; i++) {
        if (der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0)
            return 0;
        tbs += hl + vl;
        tbs_rem -= hl + vl;
    }

    while (tbs_rem > 0) {
        if (der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0)
            break;
        if (tag == 0xA3) {
            const uint8_t *exts = tbs + hl;
            size_t exts_rem = vl;
            if (der_read_tlv(exts, exts_rem, &tag, &vl, &hl) != 0 || tag != 0x30)
                return 0;
            exts += hl;
            exts_rem = vl;
            while (exts_rem > 0) {
                const uint8_t *ext = exts;
                size_t ext_rem = 0;
                size_t ext_hdr_len = 0;
                size_t ext_seq_len = 0;
                const uint8_t *extn_value = NULL;
                size_t extn_value_len = 0;
                if (der_read_tlv(ext, exts_rem, &tag, &ext_seq_len, &ext_hdr_len) != 0 || tag != 0x30)
                    return 0;
                ext += ext_hdr_len;
                ext_rem = ext_seq_len;
                if (der_read_tlv(ext, ext_rem, &tag, &vl, &hl) != 0 || tag != 0x06)
                    return 0;
                {
                    int is_basic = (vl == sizeof(OID_BASIC_CONSTRAINTS) &&
                                    memcmp(ext + hl, OID_BASIC_CONSTRAINTS, sizeof(OID_BASIC_CONSTRAINTS)) == 0);
                    int is_key_usage = (vl == sizeof(OID_KEY_USAGE) &&
                                        memcmp(ext + hl, OID_KEY_USAGE, sizeof(OID_KEY_USAGE)) == 0);
                    ext += hl + vl;
                    ext_rem -= hl + vl;
                    if (der_read_tlv(ext, ext_rem, &tag, &vl, &hl) == 0 && tag == 0x01) {
                        ext += hl + vl;
                        ext_rem -= hl + vl;
                    }
                    if (der_read_tlv(ext, ext_rem, &tag, &vl, &hl) != 0 || tag != 0x04)
                        return 0;
                    extn_value = ext + hl;
                    extn_value_len = vl;

                    if (is_basic) {
                        const uint8_t *bc = extn_value;
                        size_t bc_len = extn_value_len;
                        saw_basic_constraints = 1;
                        if (der_read_tlv(bc, bc_len, &tag, &vl, &hl) == 0 && tag == 0x30) {
                            bc += hl;
                            bc_len = vl;
                            if (bc_len > 0 && der_read_tlv(bc, bc_len, &tag, &vl, &hl) == 0 &&
                                tag == 0x01 && vl == 1 && bc[hl] != 0) {
                                is_ca = 1;
                            }
                        }
                    } else if (is_key_usage) {
                        if (der_read_tlv(extn_value, extn_value_len, &tag, &vl, &hl) == 0 &&
                            tag == 0x03 && vl >= 2) {
                            const uint8_t *bits = extn_value + hl;
                            key_usage_allows = (bits[1] & 0x04) != 0;
                        }
                    }
                }

                exts += ext_hdr_len + ext_seq_len;
                exts_rem -= ext_hdr_len + ext_seq_len;
            }
            break;
        }
        tbs += hl + vl;
        tbs_rem -= hl + vl;
    }

    return saw_basic_constraints && is_ca && key_usage_allows;
}

static RT_TLS_MAYBE_UNUSED int cert_get_rsa_pubkey(const uint8_t *cert_der,
                                                   size_t cert_len,
                                                   rt_rsa_key_t *out) {
    static const uint8_t OID_RSA_ENCRYPTION[] = {
        0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01};
    uint8_t tag;
    size_t vl, hl;
    const uint8_t *p = cert_der;
    size_t rem = cert_len;
    const uint8_t *tbs = NULL;
    size_t tbs_rem = 0;
    const uint8_t *spki = NULL;
    size_t spki_rem = 0;
    const uint8_t *algo = NULL;
    size_t algo_rem = 0;
    const uint8_t *bits = NULL;

    rt_rsa_key_init(out);
    if (der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    p += hl;
    rem = vl;
    if (der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    tbs = p + hl;
    tbs_rem = vl;

    if (der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0)
        return 0;
    if (tag == 0xA0) {
        tbs += hl + vl;
        tbs_rem -= hl + vl;
    }

    for (int i = 0; i < 5; i++) {
        if (der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0)
            return 0;
        tbs += hl + vl;
        tbs_rem -= hl + vl;
    }

    if (der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    spki = tbs + hl;
    spki_rem = vl;
    if (der_read_tlv(spki, spki_rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    algo = spki + hl;
    algo_rem = vl;
    spki += hl + vl;
    spki_rem -= hl + vl;
    if (der_read_tlv(algo, algo_rem, &tag, &vl, &hl) != 0 || tag != 0x06 ||
        memcmp(algo + hl, OID_RSA_ENCRYPTION, sizeof(OID_RSA_ENCRYPTION)) != 0 ||
        vl != sizeof(OID_RSA_ENCRYPTION)) {
        return 0;
    }
    if (der_read_tlv(spki, spki_rem, &tag, &vl, &hl) != 0 || tag != 0x03 || vl < 2)
        return 0;
    bits = spki + hl;
    if (bits[0] != 0x00)
        return 0;
    return rt_rsa_parse_public_key_pkcs1(bits + 1, vl - 1, out);
}

static RT_TLS_MAYBE_UNUSED int cert_get_ec_pubkey(const uint8_t *cert_der,
                                                  size_t cert_len,
                                                  uint8_t x_out[32],
                                                  uint8_t y_out[32]) {
    static const uint8_t OID_EC_PUBLIC_KEY[] = {
        0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01};
    static const uint8_t OID_PRIME256V1[] = {
        0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07};
    uint8_t tag;
    size_t vl, hl;
    const uint8_t *p = cert_der;
    size_t rem = cert_len;
    const uint8_t *tbs = NULL;
    size_t tbs_rem = 0;
    const uint8_t *spki = NULL;
    size_t spki_rem = 0;
    const uint8_t *algo = NULL;
    size_t algo_rem = 0;
    const uint8_t *bits = NULL;

    if (der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return -1;
    p += hl;
    rem = vl;
    if (der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return -1;
    tbs = p + hl;
    tbs_rem = vl;
    if (der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0)
        return -1;
    if (tag == 0xA0) {
        tbs += hl + vl;
        tbs_rem -= hl + vl;
    }
    for (int i = 0; i < 5; i++) {
        if (der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0)
            return -1;
        tbs += hl + vl;
        tbs_rem -= hl + vl;
    }
    if (der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return -1;
    spki = tbs + hl;
    spki_rem = vl;
    if (der_read_tlv(spki, spki_rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return -1;
    algo = spki + hl;
    algo_rem = vl;
    spki += hl + vl;
    spki_rem -= hl + vl;
    if (der_read_tlv(algo, algo_rem, &tag, &vl, &hl) != 0 || tag != 0x06 ||
        vl != sizeof(OID_EC_PUBLIC_KEY) ||
        memcmp(algo + hl, OID_EC_PUBLIC_KEY, sizeof(OID_EC_PUBLIC_KEY)) != 0) {
        return -1;
    }
    algo += hl + vl;
    algo_rem -= hl + vl;
    if (der_read_tlv(algo, algo_rem, &tag, &vl, &hl) != 0 || tag != 0x06 ||
        vl != sizeof(OID_PRIME256V1) ||
        memcmp(algo + hl, OID_PRIME256V1, sizeof(OID_PRIME256V1)) != 0) {
        return -1;
    }
    if (der_read_tlv(spki, spki_rem, &tag, &vl, &hl) != 0 || tag != 0x03 || vl < 66)
        return -1;
    bits = spki + hl;
    if (bits[0] != 0x00 || bits[1] != 0x04)
        return -1;
    memcpy(x_out, bits + 2, 32);
    memcpy(y_out, bits + 34, 32);
    return 0;
}

static RT_TLS_MAYBE_UNUSED int parse_ecdsa_sig_der(const uint8_t *sig,
                                                   size_t sig_len,
                                                   uint8_t r_out[32],
                                                   uint8_t s_out[32]) {
    uint8_t tag;
    size_t vl, hl;
    const uint8_t *inner = NULL;
    size_t inner_rem = 0;
    const uint8_t *r_bytes = NULL;
    const uint8_t *s_bytes = NULL;
    size_t r_len = 0;
    size_t s_len = 0;

    if (der_read_tlv(sig, sig_len, &tag, &vl, &hl) != 0 || tag != 0x30)
        return -1;
    inner = sig + hl;
    inner_rem = vl;
    if (der_read_tlv(inner, inner_rem, &tag, &vl, &hl) != 0 || tag != 0x02)
        return -1;
    r_bytes = inner + hl;
    r_len = vl;
    inner += hl + vl;
    inner_rem -= hl + vl;
    if (der_read_tlv(inner, inner_rem, &tag, &vl, &hl) != 0 || tag != 0x02)
        return -1;
    s_bytes = inner + hl;
    s_len = vl;

    while (r_len > 1 && r_bytes[0] == 0x00) {
        r_bytes++;
        r_len--;
    }
    while (s_len > 1 && s_bytes[0] == 0x00) {
        s_bytes++;
        s_len--;
    }
    if (r_len > 32 || s_len > 32)
        return -1;
    memset(r_out, 0, 32);
    memset(s_out, 0, 32);
    memcpy(r_out + (32 - r_len), r_bytes, r_len);
    memcpy(s_out + (32 - s_len), s_bytes, s_len);
    return 0;
}

static RT_TLS_MAYBE_UNUSED int cert_extract_signature_parts(const uint8_t *cert_der,
                                                            size_t cert_len,
                                                            const uint8_t **tbs_der,
                                                            size_t *tbs_len,
                                                            const uint8_t **alg_der,
                                                            size_t *alg_len,
                                                            const uint8_t **sig_bytes,
                                                            size_t *sig_len) {
    uint8_t tag;
    size_t vl, hl;
    const uint8_t *p = cert_der;
    size_t rem = cert_len;

    if (!tbs_der || !tbs_len || !alg_der || !alg_len || !sig_bytes || !sig_len)
        return 0;
    if (der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    p += hl;
    rem = vl;

    if (der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    *tbs_der = p;
    *tbs_len = hl + vl;
    p += hl + vl;
    rem -= hl + vl;

    if (der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    *alg_der = p;
    *alg_len = hl + vl;
    p += hl + vl;
    rem -= hl + vl;

    if (der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x03 || vl < 1)
        return 0;
    if (p[hl] != 0x00)
        return 0;
    *sig_bytes = p + hl + 1;
    *sig_len = vl - 1;
    return 1;
}

static RT_TLS_MAYBE_UNUSED int cert_parse_hash_oid(
    const uint8_t *oid, size_t oid_len, rt_rsa_hash_t *hash_id) {
    static const uint8_t OID_SHA256[] = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01};
    static const uint8_t OID_SHA384[] = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02};
    static const uint8_t OID_SHA512[] = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03};

    if (oid_len == sizeof(OID_SHA256) && memcmp(oid, OID_SHA256, sizeof(OID_SHA256)) == 0) {
        *hash_id = RT_RSA_HASH_SHA256;
        return 1;
    }
    if (oid_len == sizeof(OID_SHA384) && memcmp(oid, OID_SHA384, sizeof(OID_SHA384)) == 0) {
        *hash_id = RT_RSA_HASH_SHA384;
        return 1;
    }
    if (oid_len == sizeof(OID_SHA512) && memcmp(oid, OID_SHA512, sizeof(OID_SHA512)) == 0) {
        *hash_id = RT_RSA_HASH_SHA512;
        return 1;
    }
    return 0;
}

static RT_TLS_MAYBE_UNUSED int cert_parse_pss_params(const uint8_t *params_der,
                                                     size_t params_len,
                                                     tls_cert_sig_alg_t *alg) {
    static const uint8_t OID_MGF1[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x08};
    uint8_t tag;
    size_t vl, hl;
    const uint8_t *p = params_der;
    size_t rem = params_len;
    rt_rsa_hash_t mgf_hash = RT_RSA_HASH_SHA256;
    size_t salt_len = 0;
    int have_hash = 0;
    int have_mgf = 0;
    int have_salt = 0;

    if (!alg || der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    p += hl;
    rem = vl;

    while (rem > 0) {
        if (der_read_tlv(p, rem, &tag, &vl, &hl) != 0)
            return 0;
        if (tag == 0xA0) {
            const uint8_t *q = p + hl;
            size_t q_rem = vl;
            if (der_read_tlv(q, q_rem, &tag, &vl, &hl) != 0 || tag != 0x30)
                return 0;
            q += hl;
            q_rem = vl;
            if (der_read_tlv(q, q_rem, &tag, &vl, &hl) != 0 || tag != 0x06)
                return 0;
            if (!cert_parse_hash_oid(q + hl, vl, &alg->hash_id))
                return 0;
            have_hash = 1;
        } else if (tag == 0xA1) {
            const uint8_t *q = p + hl;
            size_t q_rem = vl;
            if (der_read_tlv(q, q_rem, &tag, &vl, &hl) != 0 || tag != 0x30)
                return 0;
            q += hl;
            q_rem = vl;
            if (der_read_tlv(q, q_rem, &tag, &vl, &hl) != 0 || tag != 0x06 || vl != sizeof(OID_MGF1) ||
                memcmp(q + hl, OID_MGF1, sizeof(OID_MGF1)) != 0) {
                return 0;
            }
            q += hl + vl;
            q_rem -= hl + vl;
            if (der_read_tlv(q, q_rem, &tag, &vl, &hl) != 0 || tag != 0x30)
                return 0;
            q += hl;
            q_rem = vl;
            if (der_read_tlv(q, q_rem, &tag, &vl, &hl) != 0 || tag != 0x06)
                return 0;
            if (!cert_parse_hash_oid(q + hl, vl, &mgf_hash))
                return 0;
            have_mgf = 1;
        } else if (tag == 0xA2) {
            const uint8_t *q = p + hl;
            size_t q_rem = vl;
            size_t parsed_salt_len = 0;
            if (der_read_tlv(q, q_rem, &tag, &vl, &hl) != 0 || tag != 0x02 || vl == 0 || vl > 4)
                return 0;
            for (size_t i = 0; i < vl; i++)
                parsed_salt_len = (parsed_salt_len << 8) | q[hl + i];
            salt_len = parsed_salt_len;
            have_salt = 1;
        } else if (tag == 0xA3) {
            const uint8_t *q = p + hl;
            size_t q_rem = vl;
            if (der_read_tlv(q, q_rem, &tag, &vl, &hl) != 0 || tag != 0x02 || vl != 1 || q[hl] != 1)
                return 0;
        }
        p += hl + vl;
        rem -= hl + vl;
    }

    return have_hash && have_mgf && have_salt && mgf_hash == alg->hash_id &&
           salt_len == hash_len_from_id(alg->hash_id);
}

static RT_TLS_MAYBE_UNUSED int cert_parse_signature_algorithm(const uint8_t *alg_der,
                                                              size_t alg_len,
                                                              tls_cert_sig_alg_t *alg) {
    static const uint8_t OID_SHA256_RSA[] = {
        0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0B};
    static const uint8_t OID_SHA384_RSA[] = {
        0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0C};
    static const uint8_t OID_SHA512_RSA[] = {
        0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0D};
    static const uint8_t OID_RSA_PSS[] = {
        0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0A};
    static const uint8_t OID_ECDSA_SHA256[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02};
    static const uint8_t OID_ECDSA_SHA384[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x03};
    static const uint8_t OID_ECDSA_SHA512[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x04};
    uint8_t tag;
    size_t vl, hl;
    const uint8_t *p = alg_der;
    size_t rem = alg_len;

    if (!alg)
        return 0;
    memset(alg, 0, sizeof(*alg));
    if (der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    p += hl;
    rem = vl;
    if (der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x06)
        return 0;
    if (vl == sizeof(OID_SHA256_RSA) && memcmp(p + hl, OID_SHA256_RSA, sizeof(OID_SHA256_RSA)) == 0) {
        alg->kind = TLS_CERT_SIG_RSA_PKCS1;
        alg->hash_id = RT_RSA_HASH_SHA256;
        return 1;
    }
    if (vl == sizeof(OID_SHA384_RSA) && memcmp(p + hl, OID_SHA384_RSA, sizeof(OID_SHA384_RSA)) == 0) {
        alg->kind = TLS_CERT_SIG_RSA_PKCS1;
        alg->hash_id = RT_RSA_HASH_SHA384;
        return 1;
    }
    if (vl == sizeof(OID_SHA512_RSA) && memcmp(p + hl, OID_SHA512_RSA, sizeof(OID_SHA512_RSA)) == 0) {
        alg->kind = TLS_CERT_SIG_RSA_PKCS1;
        alg->hash_id = RT_RSA_HASH_SHA512;
        return 1;
    }
    if (vl == sizeof(OID_ECDSA_SHA256) &&
        memcmp(p + hl, OID_ECDSA_SHA256, sizeof(OID_ECDSA_SHA256)) == 0) {
        alg->kind = TLS_CERT_SIG_ECDSA_P256;
        alg->hash_id = RT_RSA_HASH_SHA256;
        return 1;
    }
    if (vl == sizeof(OID_ECDSA_SHA384) &&
        memcmp(p + hl, OID_ECDSA_SHA384, sizeof(OID_ECDSA_SHA384)) == 0) {
        alg->kind = TLS_CERT_SIG_ECDSA_P256;
        alg->hash_id = RT_RSA_HASH_SHA384;
        return 1;
    }
    if (vl == sizeof(OID_ECDSA_SHA512) &&
        memcmp(p + hl, OID_ECDSA_SHA512, sizeof(OID_ECDSA_SHA512)) == 0) {
        alg->kind = TLS_CERT_SIG_ECDSA_P256;
        alg->hash_id = RT_RSA_HASH_SHA512;
        return 1;
    }
    if (vl == sizeof(OID_RSA_PSS) && memcmp(p + hl, OID_RSA_PSS, sizeof(OID_RSA_PSS)) == 0) {
        alg->kind = TLS_CERT_SIG_RSA_PSS;
        p += hl + vl;
        rem -= hl + vl;
        if (rem == 0)
            return 0;
        return cert_parse_pss_params(p, rem, alg);
    }
    return 0;
}

static RT_TLS_MAYBE_UNUSED size_t hash_len_from_id(rt_rsa_hash_t hash_id) {
    return hash_id == RT_RSA_HASH_SHA256 ? 32
         : hash_id == RT_RSA_HASH_SHA384 ? 48
         : hash_id == RT_RSA_HASH_SHA512 ? 64
                                         : 0;
}

static RT_TLS_MAYBE_UNUSED int hash_bytes_for_id(
    rt_rsa_hash_t hash_id, const uint8_t *data, size_t len, uint8_t out[64]) {
    switch (hash_id) {
        case RT_RSA_HASH_SHA256:
            rt_sha256(data, len, out);
            return 1;
        case RT_RSA_HASH_SHA384:
            rt_sha384(data, len, out);
            return 1;
        case RT_RSA_HASH_SHA512:
            rt_sha512(data, len, out);
            return 1;
        default:
            return 0;
    }
}

static RT_TLS_MAYBE_UNUSED int verify_cert_signature(const uint8_t *cert_der,
                                                     size_t cert_len,
                                                     const uint8_t *issuer_der,
                                                     size_t issuer_len) {
    const uint8_t *tbs_der = NULL;
    const uint8_t *alg_der = NULL;
    const uint8_t *sig_bytes = NULL;
    size_t tbs_len = 0;
    size_t alg_len = 0;
    size_t sig_len = 0;
    tls_cert_sig_alg_t alg;
    uint8_t digest[64];

    if (!cert_extract_signature_parts(
            cert_der, cert_len, &tbs_der, &tbs_len, &alg_der, &alg_len, &sig_bytes, &sig_len)) {
        return 0;
    }
    if (!cert_parse_signature_algorithm(alg_der, alg_len, &alg))
        return 0;
    if (!hash_bytes_for_id(alg.hash_id, tbs_der, tbs_len, digest))
        return 0;

    if (alg.kind == TLS_CERT_SIG_RSA_PKCS1 || alg.kind == TLS_CERT_SIG_RSA_PSS) {
        rt_rsa_key_t issuer_key;
        int ok;
        rt_rsa_key_init(&issuer_key);
        if (!cert_get_rsa_pubkey(issuer_der, issuer_len, &issuer_key))
            return 0;
        ok = (alg.kind == TLS_CERT_SIG_RSA_PKCS1)
                 ? rt_rsa_pkcs1_v15_verify(
                       &issuer_key, alg.hash_id, digest, hash_len_from_id(alg.hash_id), sig_bytes, sig_len)
                 : rt_rsa_pss_verify(
                       &issuer_key, alg.hash_id, digest, hash_len_from_id(alg.hash_id), sig_bytes, sig_len);
        rt_rsa_key_free(&issuer_key);
        return ok;
    }

    if (alg.kind == TLS_CERT_SIG_ECDSA_P256) {
        uint8_t pub_x[32];
        uint8_t pub_y[32];
        uint8_t sig_r[32];
        uint8_t sig_s[32];
        uint8_t digest32[32];
        if (cert_get_ec_pubkey(issuer_der, issuer_len, pub_x, pub_y) != 0 ||
            parse_ecdsa_sig_der(sig_bytes, sig_len, sig_r, sig_s) != 0) {
            return 0;
        }
        memcpy(digest32, digest, 32);
        return ecdsa_p256_verify(pub_x, pub_y, digest32, sig_r, sig_s);
    }

    return 0;
}

static RT_TLS_MAYBE_UNUSED char *tls_read_file_text(const char *path, size_t *len_out) {
    FILE *f = NULL;
    char *buf = NULL;
    long len = 0;
    if (len_out)
        *len_out = 0;
    if (!path || !*path)
        return NULL;
    f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0)
        goto fail;
    len = ftell(f);
    if (len < 0 || fseek(f, 0, SEEK_SET) != 0)
        goto fail;
    buf = (char *)malloc((size_t)len + 1);
    if (!buf)
        goto fail;
    if ((long)fread(buf, 1, (size_t)len, f) != len)
        goto fail;
    buf[len] = '\0';
    fclose(f);
    if (len_out)
        *len_out = (size_t)len;
    return buf;
fail:
    if (f)
        fclose(f);
    free(buf);
    return NULL;
}

static RT_TLS_MAYBE_UNUSED int pem_next_certificate(const char *pem,
                                                    size_t pem_len,
                                                    size_t *pos,
                                                    const char **body_out,
                                                    size_t *body_len_out) {
    const char *begin_marker = "-----BEGIN CERTIFICATE-----";
    const char *end_marker = "-----END CERTIFICATE-----";
    const char *cursor = pem + *pos;
    const char *end = pem + pem_len;
    const char *begin = strstr(cursor, begin_marker);
    const char *body = NULL;
    const char *finish = NULL;
    if (!begin || begin >= end)
        return 0;
    body = strchr(begin, '\n');
    if (!body)
        return 0;
    body++;
    finish = strstr(body, end_marker);
    if (!finish || finish > end)
        return 0;
    *body_out = body;
    *body_len_out = (size_t)(finish - body);
    *pos = (size_t)(finish - pem) + strlen(end_marker);
    return 1;
}

static RT_TLS_MAYBE_UNUSED int bundle_contains_exact_cert(const char *pem,
                                                          size_t pem_len,
                                                          const uint8_t *cert_der,
                                                          size_t cert_len) {
    size_t pos = 0;
    const char *body = NULL;
    size_t body_len = 0;
    while (pem_next_certificate(pem, pem_len, &pos, &body, &body_len)) {
        uint8_t *decoded = (uint8_t *)malloc(body_len);
        size_t decoded_len = 0;
        int match = 0;
        if (!decoded)
            return 0;
        decoded_len = pem_decode_cert(body, body_len, decoded, body_len);
        match = decoded_len == cert_len && memcmp(decoded, cert_der, cert_len) == 0;
        free(decoded);
        if (match)
            return 1;
    }
    return 0;
}

static RT_TLS_MAYBE_UNUSED int bundle_has_trusted_issuer(const char *pem,
                                                         size_t pem_len,
                                                         const uint8_t *child_der,
                                                         size_t child_len) {
    size_t child_issuer_len = 0;
    const uint8_t *child_issuer = cert_get_issuer(child_der, child_len, &child_issuer_len);
    size_t pos = 0;
    const char *body = NULL;
    size_t body_len = 0;

    if (!child_issuer)
        return 0;
    while (pem_next_certificate(pem, pem_len, &pos, &body, &body_len)) {
        uint8_t *decoded = (uint8_t *)malloc(body_len);
        size_t decoded_len = 0;
        size_t subject_len = 0;
        const uint8_t *subject = NULL;
        int ok = 0;
        if (!decoded)
            return 0;
        decoded_len = pem_decode_cert(body, body_len, decoded, body_len);
        subject = cert_get_subject(decoded, decoded_len, &subject_len);
        if (decoded_len > 0 && subject && der_names_equal(subject, subject_len, child_issuer, child_issuer_len)) {
            if (cert_check_expiry(decoded, decoded_len) == 0 && cert_is_ca(decoded, decoded_len) &&
                verify_cert_signature(child_der, child_len, decoded, decoded_len)) {
                ok = 1;
            }
        }
        free(decoded);
        if (ok)
            return 1;
    }
    return 0;
}

int tls_verify_chain(rt_tls_session_t *session) {
    struct cert_ref {
        const uint8_t *der;
        size_t len;
        int used;
    } intermediates[16];
    size_t intermediate_count = 0;
    size_t list_pos = 0;
    const uint8_t *current_der = NULL;
    size_t current_len = 0;
    const char *bundle_path = NULL;
    char *bundle_pem = NULL;
    size_t bundle_len = 0;

    if (!session->server_cert_der_len) {
        session->error = "TLS: no certificate to validate";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    if (session->server_cert_list && session->server_cert_list_len > 0) {
        size_t cert_index = 0;
        while (list_pos < session->server_cert_list_len && intermediate_count < 16) {
            const uint8_t *cert_der = NULL;
            size_t cert_len = 0;
            if (tls_next_certificate_entry(session->server_cert_list,
                                           session->server_cert_list_len,
                                           &list_pos,
                                           &cert_der,
                                           &cert_len) != 0) {
                session->error = "TLS: malformed certificate chain";
                return RT_TLS_ERROR_HANDSHAKE;
            }
            if (cert_index++ == 0)
                continue;
            intermediates[intermediate_count].der = cert_der;
            intermediates[intermediate_count].len = cert_len;
            intermediates[intermediate_count].used = 0;
            intermediate_count++;
        }
    }

    bundle_path = session->ca_file[0] ? session->ca_file : find_ca_bundle();
    if (!bundle_path) {
        session->error = "TLS: no PEM trust bundle is available";
        return RT_TLS_ERROR_HANDSHAKE;
    }
    bundle_pem = tls_read_file_text(bundle_path, &bundle_len);
    if (!bundle_pem) {
        session->error = "TLS: failed to read the configured trust bundle";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    current_der = session->server_cert_der;
    current_len = session->server_cert_der_len;
    for (size_t depth = 0; depth < intermediate_count + 8; depth++) {
        if (cert_check_expiry(current_der, current_len) != 0) {
            session->error = "TLS: certificate chain validation failed (expired or not yet valid)";
            free(bundle_pem);
            return RT_TLS_ERROR_HANDSHAKE;
        }
        if (bundle_contains_exact_cert(bundle_pem, bundle_len, current_der, current_len)) {
            free(bundle_pem);
            return RT_TLS_OK;
        }

        {
            size_t issuer_len = 0;
            const uint8_t *issuer = cert_get_issuer(current_der, current_len, &issuer_len);
            int advanced = 0;
            if (!issuer)
                break;
            for (size_t i = 0; i < intermediate_count; i++) {
                size_t subject_len = 0;
                const uint8_t *subject = NULL;
                if (intermediates[i].used)
                    continue;
                subject = cert_get_subject(intermediates[i].der, intermediates[i].len, &subject_len);
                if (!subject || !der_names_equal(subject, subject_len, issuer, issuer_len))
                    continue;
                if (cert_check_expiry(intermediates[i].der, intermediates[i].len) != 0 ||
                    !cert_is_ca(intermediates[i].der, intermediates[i].len) ||
                    !verify_cert_signature(current_der, current_len, intermediates[i].der, intermediates[i].len)) {
                    continue;
                }
                intermediates[i].used = 1;
                current_der = intermediates[i].der;
                current_len = intermediates[i].len;
                advanced = 1;
                break;
            }
            if (advanced)
                continue;
        }

        if (bundle_has_trusted_issuer(bundle_pem, bundle_len, current_der, current_len)) {
            free(bundle_pem);
            return RT_TLS_OK;
        }
        break;
    }

    free(bundle_pem);
    session->error = "TLS: certificate chain validation failed";
    return RT_TLS_ERROR_HANDSHAKE;
}

#endif // Platform-specific chain validation

// ---------------------------------------------------------------------------
// B.5 — CertificateVerify signature verification
// ---------------------------------------------------------------------------

/// @brief Build the 130-byte CertificateVerify message (RFC 8446 §4.4.3).
/// Content = 64 spaces + "TLS 1.3, server CertificateVerify" + 0x00 + transcript_hash
/// @param transcript_hash 32-byte SHA-256 transcript hash.
/// @param out_content Output buffer (must be at least 130 bytes).
static void build_cert_verify_message(const uint8_t transcript_hash[32], uint8_t out_content[130]) {
    static const char context_str[] = "TLS 1.3, server CertificateVerify";
    memset(out_content, 0x20, 64);
    memcpy(out_content + 64, context_str, 33);
    out_content[97] = 0x00;
    memcpy(out_content + 98, transcript_hash, 32);
}

/// @brief Hash the CertificateVerify content with SHA-256.
/// Used by the Windows path which always uses SHA-256 for the content hash
/// (Windows CNG handles the algorithm internally for RSA-PSS schemes).
#if defined(_WIN32)
static void build_cert_verify_content(const uint8_t transcript_hash[32], uint8_t content_hash[32]) {
    uint8_t content[130];
    build_cert_verify_message(transcript_hash, content);
    rt_sha256(content, 130, content_hash);
}
#endif

/// @brief Determine the hash output size for a signature scheme.
/// @return 32 for SHA-256, 48 for SHA-384, 64 for SHA-512, 0 if unknown.
static size_t sig_scheme_hash_len(uint16_t sig_scheme) {
    switch (sig_scheme) {
        case 0x0403: /* ecdsa_secp256r1_sha256 */
        case 0x0804: /* rsa_pss_rsae_sha256 */
            return 32;
        case 0x0503: /* ecdsa_secp384r1_sha384 */
        case 0x0805: /* rsa_pss_rsae_sha384 */
            return 48;
        case 0x0806: /* rsa_pss_rsae_sha512 */
            return 64;
        default:
            return 0;
    }
}

#if defined(_WIN32)

/// @brief Windows path: verify CertificateVerify via Windows CryptoAPI / NCrypt.
///
/// Builds the message, hashes via Windows CryptCreateHash, imports
/// the cert's public key via `CryptImportPublicKeyInfo`, then calls
/// `CryptVerifySignature`. Note: signature bytes need byte-reversal
/// for Windows' little-endian RSA convention vs TLS big-endian.
int tls_verify_cert_verify(rt_tls_session_t *session, const uint8_t *data, size_t len) {
    if (len < 4) {
        session->error = "TLS: CertificateVerify message too short";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    uint16_t sig_scheme = ((uint16_t)data[0] << 8) | data[1];
    uint16_t sig_len = ((uint16_t)data[2] << 8) | data[3];
    if (4 + sig_len > len) {
        session->error = "TLS: CertificateVerify signature length overflows";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    const uint8_t *sig_bytes = data + 4;

    uint8_t content_hash[64];
    memset(content_hash, 0, sizeof(content_hash));
    build_cert_verify_content(session->cert_transcript_hash, content_hash);

    PCCERT_CONTEXT cert_ctx = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                                           session->server_cert_der,
                                                           (DWORD)session->server_cert_der_len);

    if (!cert_ctx) {
        session->error = "TLS: CertVerify: could not parse certificate (Windows)";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Determine hash algorithm OID from signature scheme
    LPCSTR hash_oid;
    switch (sig_scheme) {
        case 0x0403:
        case 0x0804:
            hash_oid = szOID_NIST_sha256;
            break;
        case 0x0503:
        case 0x0805:
            hash_oid = szOID_NIST_sha384;
            break;
        case 0x0806:
            hash_oid = szOID_NIST_sha512;
            break;
        default:
            CertFreeCertificateContext(cert_ctx);
            session->error = "TLS: CertificateVerify: unsupported scheme (Windows)";
            return RT_TLS_ERROR_HANDSHAKE;
    }

    // Import public key
    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE key_handle = 0;
    DWORD key_spec = 0;
    BOOL must_free_key = FALSE;
    if (!CryptAcquireCertificatePrivateKey(cert_ctx,
                                           CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG,
                                           NULL,
                                           &key_handle,
                                           &key_spec,
                                           &must_free_key)) {
        // Fall back to public key only via CERT_KEY_PROV_INFO
        // Use CryptImportPublicKeyInfo for verification
        HCRYPTPROV hprov = 0;
        if (!CryptAcquireContextW(&hprov, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
            CertFreeCertificateContext(cert_ctx);
            session->error = "TLS: CertVerify: CryptAcquireContext failed";
            return RT_TLS_ERROR_HANDSHAKE;
        }

        HCRYPTKEY hkey = 0;
        if (!CryptImportPublicKeyInfo(
                hprov, X509_ASN_ENCODING, &cert_ctx->pCertInfo->SubjectPublicKeyInfo, &hkey)) {
            CryptReleaseContext(hprov, 0);
            CertFreeCertificateContext(cert_ctx);
            session->error = "TLS: CertVerify: CryptImportPublicKeyInfo failed";
            return RT_TLS_ERROR_HANDSHAKE;
        }

        HCRYPTHASH hhash = 0;
        ALG_ID alg_id = (sig_scheme == 0x0403 || sig_scheme == 0x0804)   ? CALG_SHA_256
                        : (sig_scheme == 0x0503 || sig_scheme == 0x0805) ? CALG_SHA_384
                                                                         : CALG_SHA_512;

        if (!CryptCreateHash(hprov, alg_id, 0, 0, &hhash)) {
            CryptDestroyKey(hkey);
            CryptReleaseContext(hprov, 0);
            CertFreeCertificateContext(cert_ctx);
            session->error = "TLS: CertVerify: CryptCreateHash failed";
            return RT_TLS_ERROR_HANDSHAKE;
        }

        // Set the hash value directly (we already computed SHA-256 of the content)
        if (!CryptSetHashParam(hhash, HP_HASHVAL, content_hash, 0)) {
            CryptDestroyHash(hhash);
            CryptDestroyKey(hkey);
            CryptReleaseContext(hprov, 0);
            CertFreeCertificateContext(cert_ctx);
            session->error = "TLS: CertVerify: CryptSetHashParam failed";
            return RT_TLS_ERROR_HANDSHAKE;
        }

        // Signature bytes may need byte-reversal for Windows RSA (big-endian vs little-endian)
        uint8_t sig_copy[4096];
        DWORD sig_copy_len = sig_len;
        if (sig_len <= sizeof(sig_copy)) {
            memcpy(sig_copy, sig_bytes, sig_len);
            // Reverse for Windows CAPI RSA (Windows stores in little-endian)
            for (DWORD i = 0; i < sig_copy_len / 2; i++) {
                uint8_t tmp = sig_copy[i];
                sig_copy[i] = sig_copy[sig_copy_len - 1 - i];
                sig_copy[sig_copy_len - 1 - i] = tmp;
            }
        }

        BOOL verified = CryptVerifySignature(hhash, sig_copy, sig_copy_len, hkey, NULL, 0);
        CryptDestroyHash(hhash);
        CryptDestroyKey(hkey);
        CryptReleaseContext(hprov, 0);
        CertFreeCertificateContext(cert_ctx);

        if (!verified) {
            session->error = "TLS: CertificateVerify signature failed (Windows)";
            return RT_TLS_ERROR_HANDSHAKE;
        }

        return RT_TLS_OK;
    }

    CertFreeCertificateContext(cert_ctx);
    session->error = "TLS: CertVerify: unsupported key type on Windows";
    return RT_TLS_ERROR_HANDSHAKE;
}

#else // Native macOS/Linux

int tls_verify_cert_verify(rt_tls_session_t *session, const uint8_t *data, size_t len) {
    uint16_t sig_scheme = 0;
    uint16_t sig_len = 0;
    const uint8_t *sig_bytes = NULL;
    uint8_t cv_message[130];
    uint8_t content_hash[64];
    size_t hash_len = 0;

    if (len < 4) {
        session->error = "TLS: CertificateVerify message too short";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    sig_scheme = ((uint16_t)data[0] << 8) | data[1];
    sig_len = ((uint16_t)data[2] << 8) | data[3];
    if (4 + sig_len > len) {
        session->error = "TLS: CertificateVerify signature length overflows";
        return RT_TLS_ERROR_HANDSHAKE;
    }
    sig_bytes = data + 4;

    build_cert_verify_message(session->cert_transcript_hash, cv_message);
    hash_len = sig_scheme_hash_len(sig_scheme);
    if (hash_len == 0) {
        session->error = "TLS: CertificateVerify: unsupported signature scheme";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    switch (hash_len) {
        case 32:
            rt_sha256(cv_message, sizeof(cv_message), content_hash);
            break;
        case 48:
            rt_sha384(cv_message, sizeof(cv_message), content_hash);
            break;
        case 64:
            rt_sha512(cv_message, sizeof(cv_message), content_hash);
            break;
        default:
            session->error = "TLS: CertificateVerify: unsupported signature hash";
            return RT_TLS_ERROR_HANDSHAKE;
    }

    if (sig_scheme == 0x0403) {
        uint8_t pub_x[32];
        uint8_t pub_y[32];
        uint8_t sig_r[32];
        uint8_t sig_s[32];

        if (cert_get_ec_pubkey(session->server_cert_der, session->server_cert_der_len, pub_x, pub_y) != 0) {
            session->error = "TLS: CertificateVerify: certificate does not contain a P-256 public key";
            return RT_TLS_ERROR_HANDSHAKE;
        }
        if (parse_ecdsa_sig_der(sig_bytes, sig_len, sig_r, sig_s) != 0 ||
            !ecdsa_p256_verify(pub_x, pub_y, content_hash, sig_r, sig_s)) {
            session->error = "TLS: CertificateVerify signature verification failed";
            return RT_TLS_ERROR_HANDSHAKE;
        }
        return RT_TLS_OK;
    }

    if (sig_scheme == 0x0503) {
        session->error = "TLS: CertificateVerify: secp384r1 signatures are not implemented";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    if (sig_scheme == 0x0804 || sig_scheme == 0x0805 || sig_scheme == 0x0806) {
        rt_rsa_key_t rsa_key;
        int verified = 0;
        rt_rsa_key_init(&rsa_key);
        if (!cert_get_rsa_pubkey(session->server_cert_der, session->server_cert_der_len, &rsa_key)) {
            session->error = "TLS: CertificateVerify: certificate does not contain an RSA public key";
            rt_rsa_key_free(&rsa_key);
            return RT_TLS_ERROR_HANDSHAKE;
        }
        verified = rt_rsa_pss_verify(&rsa_key,
                                     sig_scheme == 0x0804   ? RT_RSA_HASH_SHA256
                                     : sig_scheme == 0x0805 ? RT_RSA_HASH_SHA384
                                                            : RT_RSA_HASH_SHA512,
                                     content_hash,
                                     hash_len,
                                     sig_bytes,
                                     sig_len);
        rt_rsa_key_free(&rsa_key);
        if (!verified) {
            session->error = "TLS: CertificateVerify signature verification failed";
            return RT_TLS_ERROR_HANDSHAKE;
        }
        return RT_TLS_OK;
    }

    session->error = "TLS: CertificateVerify: unsupported signature scheme";
    return RT_TLS_ERROR_HANDSHAKE;
}

#endif // Platform CertificateVerify
