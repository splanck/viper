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
//   - Certificate chain validation uses the OS trust store on each platform.
//   - Hostname verification follows RFC 6125 (SAN takes precedence over CN).
//   - CertificateVerify uses platform-native crypto or dlopen(libcrypto) fallback.
// Ownership/Lifetime:
//   - All functions operate on the session's internal buffers; no heap allocation
//     except platform API temporaries that are released before return.
// Links: rt_tls_internal.h (shared struct), rt_tls.c (caller), rt_crypto.h
//
//===----------------------------------------------------------------------===//

#include "rt_crypto.h"
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
#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#include <Security/Security.h>
#elif defined(_WIN32)
#ifndef _WINDOWS_
#error "windows.h must be included before wincrypt.h"
#endif
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")
#else
#include "rt_ecdsa_p256.h"
#include <dlfcn.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define RT_TLS_MAYBE_UNUSED __attribute__((unused))
#else
#define RT_TLS_MAYBE_UNUSED
#endif

//=============================================================================
// Certificate Validation — CS-1, CS-2, CS-3
//=============================================================================

// ---------------------------------------------------------------------------
// B.2 — TLS 1.3 Certificate message parser
// ---------------------------------------------------------------------------

/// @brief Advance through one entry of a TLS 1.3 certificate_list.
///
/// Each entry is `[3-byte length][DER cert][2-byte ext length][extensions]`.
/// Updates `*pos` to point past the entry and writes the DER pointer +
/// length out-parameters. Returns -1 on any structural error (length
/// underflow, extensions past end of list, etc.).
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

/// @brief Extract SubjectAltName DNS names from a certificate DER.
/// @return Number of names found (0 if SAN extension absent).
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

/// @brief Extract CommonName from certificate Subject.
/// @return 1 if found, 0 otherwise.
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

/// @brief Match a hostname against a pattern (RFC 6125 §6.4 wildcard rules).
///
/// Supports:
///   - Exact match: "example.com" vs "example.com"
///   - Single-label wildcard: "*.example.com" vs "foo.example.com"
///     (wildcard must be leftmost label, covers only one label)
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

/// @brief Verify that session->hostname matches the certificate DER.
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

#if defined(__APPLE__)

/// @brief Verify certificate chain + hostname via macOS Security.framework.
/// SecPolicyCreateSSL with a hostname string performs both chain validation
/// (CS-1) and hostname verification (CS-2) in a single call.
int tls_verify_chain(rt_tls_session_t *session) {
    if (!session->server_cert_der_len) {
        session->error = "TLS: no certificate to validate";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    CFMutableArrayRef certs = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    if (!certs) {
        session->error = "TLS: could not allocate certificate array";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    if (session->server_cert_list && session->server_cert_list_len > 0) {
        size_t list_pos = 0;
        while (list_pos < session->server_cert_list_len) {
            const uint8_t *cert_der = NULL;
            size_t cert_len = 0;
            if (tls_next_certificate_entry(session->server_cert_list,
                                           session->server_cert_list_len,
                                           &list_pos,
                                           &cert_der,
                                           &cert_len) != 0) {
                CFRelease(certs);
                session->error = "TLS: malformed certificate chain";
                return RT_TLS_ERROR_HANDSHAKE;
            }

            CFDataRef cert_data = CFDataCreate(kCFAllocatorDefault, cert_der, (CFIndex)cert_len);
            if (!cert_data) {
                CFRelease(certs);
                session->error = "TLS: could not create CFData for certificate";
                return RT_TLS_ERROR_HANDSHAKE;
            }

            SecCertificateRef cert = SecCertificateCreateWithData(kCFAllocatorDefault, cert_data);
            CFRelease(cert_data);
            if (!cert) {
                CFRelease(certs);
                session->error = "TLS: could not parse DER certificate";
                return RT_TLS_ERROR_HANDSHAKE;
            }

            CFArrayAppendValue(certs, cert);
            CFRelease(cert);
        }
    }

    if (CFArrayGetCount(certs) == 0) {
        CFRelease(certs);
        session->error = "TLS: no certificates available for validation";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    CFStringRef hostname_cf =
        CFStringCreateWithCString(kCFAllocatorDefault, session->hostname, kCFStringEncodingUTF8);
    if (!hostname_cf) {
        CFRelease(certs);
        session->error = "TLS: could not create hostname CFString";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    SecPolicyRef policy = SecPolicyCreateSSL(true, hostname_cf);
    CFRelease(hostname_cf);
    if (!policy) {
        CFRelease(certs);
        session->error = "TLS: could not create SSL policy";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    SecTrustRef trust = NULL;
    OSStatus os_status = SecTrustCreateWithCertificates(certs, policy, &trust);
    CFRelease(policy);
    CFRelease(certs);

    if (os_status != errSecSuccess || !trust) {
        if (trust)
            CFRelease(trust);
        session->error = "TLS: SecTrustCreateWithCertificates failed";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Disable network fetch (offline chain validation — rely on system trust store)
    SecTrustSetNetworkFetchAllowed(trust, false);

    CFErrorRef err = NULL;
    bool trusted = SecTrustEvaluateWithError(trust, &err);
    CFRelease(trust);

    if (!trusted) {
        if (err)
            CFRelease(err);
        session->error = "TLS: certificate chain validation failed (untrusted or expired)";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    return RT_TLS_OK;
}

#elif defined(_WIN32)

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

#else // Linux / other POSIX

typedef void *tls_x509_t;
typedef void *tls_x509_store_t;
typedef void *tls_x509_store_ctx_t;
typedef void *tls_openssl_stack_t;

typedef tls_x509_t *(*tls_d2i_x509_fn)(tls_x509_t **, const unsigned char **, long);
typedef void (*tls_x509_free_fn)(tls_x509_t *);
typedef tls_x509_store_t *(*tls_x509_store_new_fn)(void);
typedef void (*tls_x509_store_free_fn)(tls_x509_store_t *);
typedef int (*tls_x509_store_set_default_paths_fn)(tls_x509_store_t *);
typedef tls_x509_store_ctx_t *(*tls_x509_store_ctx_new_fn)(void);
typedef void (*tls_x509_store_ctx_free_fn)(tls_x509_store_ctx_t *);
typedef int (*tls_x509_store_ctx_init_fn)(tls_x509_store_ctx_t *,
                                          tls_x509_store_t *,
                                          tls_x509_t *,
                                          tls_openssl_stack_t *);
typedef int (*tls_x509_store_ctx_set_default_fn)(tls_x509_store_ctx_t *, const char *);
typedef int (*tls_x509_verify_cert_fn)(tls_x509_store_ctx_t *);
typedef tls_openssl_stack_t *(*tls_openssl_sk_new_null_fn)(void);
typedef int (*tls_openssl_sk_push_fn)(tls_openssl_stack_t *, const void *);
typedef void (*tls_openssl_sk_free_fn)(tls_openssl_stack_t *);

/// @brief Try to dlopen OpenSSL's libcrypto, falling back across versions.
///
/// Tries `.so.3` (modern), then `.so.1.1` (legacy), then unsuffixed.
/// Returned handle is the caller's responsibility to `dlclose`.
/// Returns NULL when no libcrypto is available — the caller falls back
/// to platform-native crypto (CommonCrypto on macOS, BCrypt on Windows).
static void *tls_dlopen_libcrypto(void) {
    void *crypto_lib = dlopen("libcrypto.so.3", RTLD_LAZY | RTLD_LOCAL);
    if (!crypto_lib)
        crypto_lib = dlopen("libcrypto.so.1.1", RTLD_LAZY | RTLD_LOCAL);
    if (!crypto_lib)
        crypto_lib = dlopen("libcrypto.so", RTLD_LAZY | RTLD_LOCAL);
    return crypto_lib;
}

/// @brief Try to find the system CA bundle.
static RT_TLS_MAYBE_UNUSED const char *find_ca_bundle(void) {
    static const char *bundles[] = {"/etc/ssl/certs/ca-certificates.crt", // Debian/Ubuntu
                                    "/etc/pki/tls/certs/ca-bundle.crt",   // RHEL/CentOS
                                    "/etc/ssl/ca-bundle.pem",             // OpenSUSE
                                    "/etc/ssl/cert.pem",                  // Alpine / macOS fallback
                                    NULL};
    for (int i = 0; bundles[i]; i++) {
        FILE *f = fopen(bundles[i], "r");
        if (f) {
            fclose(f);
            return bundles[i];
        }
    }
    return NULL;
}

/// @brief Decode one PEM certificate (between -----BEGIN/END CERTIFICATE-----).
/// Returns DER length written to out_der, or 0 on failure.
static RT_TLS_MAYBE_UNUSED size_t pem_decode_cert(const char *pem_b64,
                                                  size_t b64_len,
                                                  uint8_t *out_der,
                                                  size_t max_der) {
    // Simple base64 decode (no line breaks within the data stream)
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
        if (c == '\r' || c == '\n' || c == ' ')
            continue;
        if (c == '=')
            break;

        int8_t v = b64tab[c];
        if (v < 0)
            continue;

        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (out_len >= max_der)
                return 0; // overflow
            out_der[out_len++] = (uint8_t)((acc >> bits) & 0xFF);
        }
    }

    return out_len;
}

/// @brief Compare two DER-encoded Name structures byte-for-byte.
static RT_TLS_MAYBE_UNUSED int der_names_equal(const uint8_t *a_der,
                                               size_t a_len,
                                               const uint8_t *b_der,
                                               size_t b_len) {
    if (a_len != b_len)
        return 0;
    return memcmp(a_der, b_der, a_len) == 0;
}

/// @brief Get the DER-encoded Subject from a certificate.
static RT_TLS_MAYBE_UNUSED const uint8_t *cert_get_subject(const uint8_t *cert_der,
                                                           size_t cert_len,
                                                           size_t *subject_len) {
    uint8_t t;
    size_t vl, hl;

    // Certificate SEQUENCE
    if (der_read_tlv(cert_der, cert_len, &t, &vl, &hl) != 0 || t != 0x30)
        return NULL;

    // TBSCertificate SEQUENCE (first child)
    const uint8_t *cert_val = cert_der + hl;
    if (der_read_tlv(cert_val, vl, &t, &vl, &hl) != 0 || t != 0x30)
        return NULL;

    const uint8_t *tbs = cert_val + hl;
    size_t tbs_len = vl;
    size_t pos = 0;
    int seq_count = 0;

    // Subject is the 4th field in TBSCertificate (0-indexed):
    // [0] version (optional [0] EXPLICIT), [1] serial, [2] signature alg, [3] issuer, [4] validity,
    // [5] subject We count SEQUENCE/SET fields skipping context-tags to find Subject.
    while (pos < tbs_len) {
        if (der_read_tlv(tbs + pos, tbs_len - pos, &t, &vl, &hl) != 0)
            break;

        // Skip version [0] EXPLICIT
        if (t == 0xA0) {
            pos += hl + vl;
            continue;
        }

        seq_count++;
        // Subject is the 3rd SEQUENCE (after serial INTEGER, alg SEQUENCE, issuer SEQUENCE)
        // i.e., seq_count == 4 for Subject
        if (t == 0x30 && seq_count == 4) {
            *subject_len = hl + vl;
            return tbs + pos;
        }

        pos += hl + vl;
    }

    return NULL;
}

/// @brief Get the DER-encoded Issuer from a certificate.
static RT_TLS_MAYBE_UNUSED const uint8_t *cert_get_issuer(const uint8_t *cert_der,
                                                          size_t cert_len,
                                                          size_t *issuer_len) {
    uint8_t t;
    size_t vl, hl;

    if (der_read_tlv(cert_der, cert_len, &t, &vl, &hl) != 0 || t != 0x30)
        return NULL;

    const uint8_t *cert_val = cert_der + hl;
    if (der_read_tlv(cert_val, vl, &t, &vl, &hl) != 0 || t != 0x30)
        return NULL;

    const uint8_t *tbs = cert_val + hl;
    size_t tbs_len = vl;
    size_t pos = 0;
    int seq_count = 0;

    while (pos < tbs_len) {
        if (der_read_tlv(tbs + pos, tbs_len - pos, &t, &vl, &hl) != 0)
            break;

        if (t == 0xA0) {
            pos += hl + vl;
            continue;
        }

        seq_count++;
        // Issuer is the 2nd SEQUENCE (after serial)
        if (t == 0x30 && seq_count == 3) {
            *issuer_len = hl + vl;
            return tbs + pos;
        }

        pos += hl + vl;
    }

    return NULL;
}

/// @brief Parse a UTCTime or GeneralizedTime DER value into a Unix timestamp.
/// @param data Pointer to the time value bytes (after tag/length).
/// @param len Length of the time value.
/// @param tag DER tag (0x17 = UTCTime, 0x18 = GeneralizedTime).
/// @return Unix timestamp, or -1 on parse failure.
static time_t parse_der_time(const uint8_t *data, size_t len, uint8_t tag) {
    struct tm tm_val;
    memset(&tm_val, 0, sizeof(tm_val));

    const char *s = (const char *)data;
    int pos = 0;

    if (tag == 0x17 && len >= 12) {
        /* UTCTime: YYMMDDHHMMSSZ */
        int yy = (s[0] - '0') * 10 + (s[1] - '0');
        tm_val.tm_year =
            (yy >= 50) ? yy : (100 + yy); /* RFC 5280: 50-99 -> 1950-1999, 0-49 -> 2000-2049 */
        pos = 2;
    } else if (tag == 0x18 && len >= 14) {
        /* GeneralizedTime: YYYYMMDDHHMMSSZ */
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

#if defined(_WIN32)
    return _mkgmtime(&tm_val);
#else
    return timegm(&tm_val);
#endif
}

/// @brief Check if a certificate's Validity period covers the current time.
/// @param cert_der DER-encoded certificate.
/// @param cert_len Length in bytes.
/// @return 0 if valid (current time within notBefore..notAfter), -1 on error or expired.
static RT_TLS_MAYBE_UNUSED int cert_check_expiry(const uint8_t *cert_der, size_t cert_len) {
    uint8_t t;
    size_t vl, hl;

    /* Certificate SEQUENCE */
    if (der_read_tlv(cert_der, cert_len, &t, &vl, &hl) != 0 || t != 0x30)
        return -1;
    /* TBSCertificate SEQUENCE */
    const uint8_t *cert_val = cert_der + hl;
    if (der_read_tlv(cert_val, vl, &t, &vl, &hl) != 0 || t != 0x30)
        return -1;

    const uint8_t *tbs = cert_val + hl;
    size_t tbs_len = vl;
    size_t pos = 0;
    int seq_count = 0;

    /* Walk TBSCertificate fields to find Validity (field index 3, skipping version tag). */
    while (pos < tbs_len) {
        if (der_read_tlv(tbs + pos, tbs_len - pos, &t, &vl, &hl) != 0)
            break;

        if (t == 0xA0) /* version [0] EXPLICIT — skip */
        {
            pos += hl + vl;
            continue;
        }

        seq_count++;
        if (t == 0x30 && seq_count == 3) /* Validity is the 3rd SEQUENCE */
        {
            const uint8_t *validity = tbs + pos + hl;
            size_t validity_len = vl;
            size_t vpos = 0;

            /* notBefore (UTCTime or GeneralizedTime) */
            if (der_read_tlv(validity + vpos, validity_len - vpos, &t, &vl, &hl) != 0)
                return -1;
            time_t not_before = parse_der_time(validity + vpos + hl, vl, t);
            vpos += hl + vl;

            /* notAfter */
            if (der_read_tlv(validity + vpos, validity_len - vpos, &t, &vl, &hl) != 0)
                return -1;
            time_t not_after = parse_der_time(validity + vpos + hl, vl, t);

            if (not_before == (time_t)-1 || not_after == (time_t)-1)
                return -1;

            time_t now = time(NULL);
            if (now < not_before || now > not_after)
                return -1; /* expired or not yet valid */

            return 0; /* valid */
        }

        pos += hl + vl;
    }

    return -1; /* Validity field not found */
}

/// @brief Verify certificate chain against the Linux system trust store using libcrypto.
int tls_verify_chain(rt_tls_session_t *session) {
    if (!session->server_cert_der_len) {
        session->error = "TLS: no certificate to validate";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    void *crypto_lib = tls_dlopen_libcrypto();
    if (!crypto_lib) {
        session->error = "TLS: certificate validation requires libcrypto on this platform";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    tls_d2i_x509_fn fn_d2i_X509 = (tls_d2i_x509_fn)dlsym(crypto_lib, "d2i_X509");
    tls_x509_free_fn fn_X509_free = (tls_x509_free_fn)dlsym(crypto_lib, "X509_free");
    tls_x509_store_new_fn fn_X509_STORE_new =
        (tls_x509_store_new_fn)dlsym(crypto_lib, "X509_STORE_new");
    tls_x509_store_free_fn fn_X509_STORE_free =
        (tls_x509_store_free_fn)dlsym(crypto_lib, "X509_STORE_free");
    tls_x509_store_set_default_paths_fn fn_X509_STORE_set_default_paths =
        (tls_x509_store_set_default_paths_fn)dlsym(crypto_lib, "X509_STORE_set_default_paths");
    tls_x509_store_ctx_new_fn fn_X509_STORE_CTX_new =
        (tls_x509_store_ctx_new_fn)dlsym(crypto_lib, "X509_STORE_CTX_new");
    tls_x509_store_ctx_free_fn fn_X509_STORE_CTX_free =
        (tls_x509_store_ctx_free_fn)dlsym(crypto_lib, "X509_STORE_CTX_free");
    tls_x509_store_ctx_init_fn fn_X509_STORE_CTX_init =
        (tls_x509_store_ctx_init_fn)dlsym(crypto_lib, "X509_STORE_CTX_init");
    tls_x509_store_ctx_set_default_fn fn_X509_STORE_CTX_set_default =
        (tls_x509_store_ctx_set_default_fn)dlsym(crypto_lib, "X509_STORE_CTX_set_default");
    tls_x509_verify_cert_fn fn_X509_verify_cert =
        (tls_x509_verify_cert_fn)dlsym(crypto_lib, "X509_verify_cert");
    tls_openssl_sk_new_null_fn fn_OPENSSL_sk_new_null =
        (tls_openssl_sk_new_null_fn)dlsym(crypto_lib, "OPENSSL_sk_new_null");
    tls_openssl_sk_push_fn fn_OPENSSL_sk_push =
        (tls_openssl_sk_push_fn)dlsym(crypto_lib, "OPENSSL_sk_push");
    tls_openssl_sk_free_fn fn_OPENSSL_sk_free =
        (tls_openssl_sk_free_fn)dlsym(crypto_lib, "OPENSSL_sk_free");

    if (!fn_d2i_X509 || !fn_X509_free || !fn_X509_STORE_new || !fn_X509_STORE_free ||
        !fn_X509_STORE_set_default_paths || !fn_X509_STORE_CTX_new || !fn_X509_STORE_CTX_free ||
        !fn_X509_STORE_CTX_init || !fn_X509_verify_cert) {
        dlclose(crypto_lib);
        session->error = "TLS: libcrypto is missing X.509 validation symbols";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    const unsigned char *leaf_ptr = session->server_cert_der;
    tls_x509_t *leaf = fn_d2i_X509(NULL, &leaf_ptr, (long)session->server_cert_der_len);
    if (!leaf) {
        dlclose(crypto_lib);
        session->error = "TLS: failed to parse the leaf certificate";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    tls_x509_store_t *store = fn_X509_STORE_new();
    if (!store || fn_X509_STORE_set_default_paths(store) != 1) {
        if (store)
            fn_X509_STORE_free(store);
        fn_X509_free(leaf);
        dlclose(crypto_lib);
        session->error = "TLS: failed to initialize the system trust store";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    tls_openssl_stack_t *untrusted = NULL;
    tls_x509_t **intermediates = NULL;
    size_t intermediates_count = 0;
    if (session->server_cert_list && session->server_cert_count > 1) {
        if (!fn_OPENSSL_sk_new_null || !fn_OPENSSL_sk_push || !fn_OPENSSL_sk_free) {
            fn_X509_STORE_free(store);
            fn_X509_free(leaf);
            dlclose(crypto_lib);
            session->error = "TLS: libcrypto is missing certificate stack symbols";
            return RT_TLS_ERROR_HANDSHAKE;
        }

        intermediates_count = session->server_cert_count - 1;
        intermediates = (tls_x509_t **)calloc(intermediates_count, sizeof(tls_x509_t *));
        untrusted = fn_OPENSSL_sk_new_null();
        if (!intermediates || !untrusted) {
            free(intermediates);
            if (untrusted)
                fn_OPENSSL_sk_free(untrusted);
            fn_X509_STORE_free(store);
            fn_X509_free(leaf);
            dlclose(crypto_lib);
            session->error = "TLS: failed to allocate intermediate certificate storage";
            return RT_TLS_ERROR_HANDSHAKE;
        }

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
                session->error = "TLS: malformed intermediate certificate chain";
                goto linux_chain_cleanup;
            }
            if (cert_index++ == 0)
                continue;

            const unsigned char *cert_ptr = cert_der;
            tls_x509_t *intermediate = fn_d2i_X509(NULL, &cert_ptr, (long)cert_len);
            if (!intermediate) {
                session->error = "TLS: failed to parse an intermediate certificate";
                goto linux_chain_cleanup;
            }
            intermediates[cert_index - 2] = intermediate;
            if (fn_OPENSSL_sk_push(untrusted, intermediate) <= 0) {
                session->error = "TLS: failed to register an intermediate certificate";
                goto linux_chain_cleanup;
            }
        }
    }

    tls_x509_store_ctx_t *ctx = fn_X509_STORE_CTX_new();
    if (!ctx || fn_X509_STORE_CTX_init(ctx, store, leaf, untrusted) != 1) {
        if (ctx)
            fn_X509_STORE_CTX_free(ctx);
        session->error = "TLS: failed to create the certificate verification context";
        goto linux_chain_cleanup;
    }
    if (fn_X509_STORE_CTX_set_default)
        fn_X509_STORE_CTX_set_default(ctx, "ssl_client");

    if (fn_X509_verify_cert(ctx) != 1) {
        fn_X509_STORE_CTX_free(ctx);
        session->error = "TLS: certificate chain validation failed";
        goto linux_chain_cleanup;
    }

    fn_X509_STORE_CTX_free(ctx);
    if (untrusted)
        fn_OPENSSL_sk_free(untrusted);
    if (intermediates) {
        for (size_t i = 0; i < intermediates_count; i++) {
            if (intermediates[i])
                fn_X509_free(intermediates[i]);
        }
        free(intermediates);
    }
    fn_X509_STORE_free(store);
    fn_X509_free(leaf);
    dlclose(crypto_lib);
    return RT_TLS_OK;

linux_chain_cleanup:
    if (untrusted && fn_OPENSSL_sk_free)
        fn_OPENSSL_sk_free(untrusted);
    if (intermediates) {
        for (size_t i = 0; i < intermediates_count; i++) {
            if (intermediates[i])
                fn_X509_free(intermediates[i]);
        }
        free(intermediates);
    }
    fn_X509_STORE_free(store);
    fn_X509_free(leaf);
    dlclose(crypto_lib);
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

#if defined(__APPLE__)

/// @brief macOS path: verify the TLS 1.3 CertificateVerify signature via Security framework.
///
/// Builds the 130-byte message, hashes with SHA-256/384/512 per the
/// signature scheme, reconstructs the SecCertificateRef from stored
/// DER, then calls `SecKeyVerifySignature`. Returns RT_TLS_OK on
/// successful verification, RT_TLS_ERROR_* otherwise.
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

    // Build the raw 130-byte CertificateVerify message, then hash with the
    // algorithm matching the signature scheme.
    uint8_t content[130];
    build_cert_verify_message(session->cert_transcript_hash, content);

    uint8_t content_hash[64];
    memset(content_hash, 0, sizeof(content_hash));
    size_t hash_len = sig_scheme_hash_len(sig_scheme);
    if (hash_len == 0)
        hash_len = 32; // Fallback to SHA-256 for ECDSA and unknown schemes

    switch (hash_len) {
        case 48:
            CC_SHA384(content, 130, content_hash);
            break;
        case 64:
            CC_SHA512(content, 130, content_hash);
            break;
        default:
            rt_sha256(content, 130, content_hash);
            hash_len = 32;
            break;
    }

    // Reconstruct SecCertificateRef from stored DER
    CFDataRef cert_data = CFDataCreate(
        kCFAllocatorDefault, session->server_cert_der, (CFIndex)session->server_cert_der_len);
    if (!cert_data) {
        session->error = "TLS: CertVerify: could not create CFData";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    SecCertificateRef cert = SecCertificateCreateWithData(kCFAllocatorDefault, cert_data);
    CFRelease(cert_data);
    if (!cert) {
        session->error = "TLS: CertVerify: could not parse DER certificate";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    SecKeyRef pub_key = SecCertificateCopyKey(cert);
    CFRelease(cert);
    if (!pub_key) {
        session->error = "TLS: CertVerify: could not extract public key from cert";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Select the SecKeyAlgorithm based on signature scheme
    // 0x0403 = ecdsa_secp256r1_sha256
    // 0x0503 = ecdsa_secp384r1_sha384
    // 0x0804 = rsa_pss_rsae_sha256
    // 0x0805 = rsa_pss_rsae_sha384
    // 0x0806 = rsa_pss_rsae_sha512
    SecKeyAlgorithm algorithm;
    switch (sig_scheme) {
        case 0x0403:
            algorithm = kSecKeyAlgorithmECDSASignatureDigestX962SHA256;
            break;
        case 0x0503:
            algorithm = kSecKeyAlgorithmECDSASignatureDigestX962SHA384;
            break;
        case 0x0804:
            algorithm = kSecKeyAlgorithmRSASignatureDigestPSSSHA256;
            break;
        case 0x0805:
            algorithm = kSecKeyAlgorithmRSASignatureDigestPSSSHA384;
            break;
        case 0x0806:
            algorithm = kSecKeyAlgorithmRSASignatureDigestPSSSHA512;
            break;
        default:
            CFRelease(pub_key);
            session->error = "TLS: CertificateVerify: unsupported signature scheme";
            return RT_TLS_ERROR_HANDSHAKE;
    }

    CFDataRef sig_data = CFDataCreate(kCFAllocatorDefault, sig_bytes, (CFIndex)sig_len);
    CFDataRef hash_data = CFDataCreate(kCFAllocatorDefault, content_hash, (CFIndex)hash_len);

    if (!sig_data || !hash_data) {
        if (sig_data)
            CFRelease(sig_data);
        if (hash_data)
            CFRelease(hash_data);
        CFRelease(pub_key);
        session->error = "TLS: CertVerify: CFData allocation failed";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    CFErrorRef err = NULL;
    bool verified = SecKeyVerifySignature(pub_key, algorithm, hash_data, sig_data, &err);

    CFRelease(sig_data);
    CFRelease(hash_data);
    CFRelease(pub_key);

    if (!verified) {
        if (err)
            CFRelease(err);
        session->error = "TLS: CertificateVerify signature verification failed";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    return RT_TLS_OK;
}

#elif defined(_WIN32)

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

#else // Linux — native ECDSA P-256 + dlopen(libcrypto) fallback for RSA-PSS

// EC public key OIDs
static const uint8_t OID_EC_PUBLIC_KEY[] = {
    0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01}; // 1.2.840.10045.2.1
static const uint8_t OID_PRIME256V1[] = {
    0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07}; // 1.2.840.10045.3.1.7

/// @brief Extract the P-256 public key (X, Y) from a DER-encoded certificate.
/// Navigates: Certificate -> TBSCertificate -> SubjectPublicKeyInfo -> BIT STRING.
/// @return 0 on success, -1 on error (not an EC key, wrong curve, parse failure).
static RT_TLS_MAYBE_UNUSED int cert_get_ec_pubkey(const uint8_t *cert_der,
                                                  size_t cert_len,
                                                  uint8_t x_out[32],
                                                  uint8_t y_out[32]) {
    uint8_t tag;
    size_t vl, hl;
    const uint8_t *p = cert_der;
    size_t rem = cert_len;

    // Certificate ::= SEQUENCE { TBSCertificate, signatureAlgorithm, signatureValue }
    if (der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return -1;
    p += hl;
    rem = vl; // inside Certificate SEQUENCE

    // TBSCertificate ::= SEQUENCE { ... }
    if (der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return -1;
    const uint8_t *tbs = p + hl;
    size_t tbs_rem = vl;

    // Skip version [0] EXPLICIT (if present)
    if (der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0)
        return -1;
    if (tag == 0xA0) // explicit context [0] = version
    {
        tbs += hl + vl;
        tbs_rem -= hl + vl;
        if (der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0)
            return -1;
    }

    // serialNumber (INTEGER) — skip
    if (tag != 0x02)
        return -1;
    tbs += hl + vl;
    tbs_rem -= hl + vl;

    // signature (AlgorithmIdentifier SEQUENCE) — skip
    if (der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return -1;
    tbs += hl + vl;
    tbs_rem -= hl + vl;

    // issuer (Name SEQUENCE) — skip
    if (der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return -1;
    tbs += hl + vl;
    tbs_rem -= hl + vl;

    // validity (SEQUENCE) — skip
    if (der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return -1;
    tbs += hl + vl;
    tbs_rem -= hl + vl;

    // subject (Name SEQUENCE) — skip
    if (der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return -1;
    tbs += hl + vl;
    tbs_rem -= hl + vl;

    // SubjectPublicKeyInfo ::= SEQUENCE { algorithm, subjectPublicKey }
    if (der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return -1;
    const uint8_t *spki = tbs + hl;
    size_t spki_rem = vl;

    // AlgorithmIdentifier ::= SEQUENCE { algorithm OID, parameters }
    if (der_read_tlv(spki, spki_rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return -1;
    const uint8_t *algo = spki + hl;
    size_t algo_rem = vl;
    spki += hl + vl;
    spki_rem -= hl + vl;

    // Check algorithm OID = id-ecPublicKey
    if (der_read_tlv(algo, algo_rem, &tag, &vl, &hl) != 0 || tag != 0x06)
        return -1;
    if (!oid_matches(algo + hl, vl, OID_EC_PUBLIC_KEY, sizeof(OID_EC_PUBLIC_KEY)))
        return -1; // not an EC key
    algo += hl + vl;
    algo_rem -= hl + vl;

    // Check curve parameter = prime256v1
    if (der_read_tlv(algo, algo_rem, &tag, &vl, &hl) != 0 || tag != 0x06)
        return -1;
    if (!oid_matches(algo + hl, vl, OID_PRIME256V1, sizeof(OID_PRIME256V1)))
        return -1; // wrong curve

    // subjectPublicKey (BIT STRING)
    if (der_read_tlv(spki, spki_rem, &tag, &vl, &hl) != 0 || tag != 0x03)
        return -1;
    const uint8_t *bits = spki + hl;
    // First byte = number of unused bits (should be 0)
    if (vl < 66 || bits[0] != 0x00)
        return -1;
    // Second byte = 0x04 (uncompressed point format)
    if (bits[1] != 0x04)
        return -1;
    // bytes 2..33 = X, bytes 34..65 = Y
    memcpy(x_out, bits + 2, 32);
    memcpy(y_out, bits + 34, 32);

    return 0;
}

/// @brief Parse a DER-encoded ECDSA signature: SEQUENCE { INTEGER r, INTEGER s }.
/// Strips leading zero padding from DER integers and right-aligns into 32-byte buffers.
/// @return 0 on success, -1 on error (malformed signature).
static RT_TLS_MAYBE_UNUSED int parse_ecdsa_sig_der(const uint8_t *sig,
                                                   size_t sig_len,
                                                   uint8_t r_out[32],
                                                   uint8_t s_out[32]) {
    uint8_t tag;
    size_t vl, hl;

    // Outer SEQUENCE
    if (der_read_tlv(sig, sig_len, &tag, &vl, &hl) != 0 || tag != 0x30)
        return -1;
    const uint8_t *inner = sig + hl;
    size_t inner_rem = vl;

    // INTEGER r
    if (der_read_tlv(inner, inner_rem, &tag, &vl, &hl) != 0 || tag != 0x02)
        return -1;
    const uint8_t *r_bytes = inner + hl;
    size_t r_len = vl;
    inner += hl + vl;
    inner_rem -= hl + vl;

    // INTEGER s
    if (der_read_tlv(inner, inner_rem, &tag, &vl, &hl) != 0 || tag != 0x02)
        return -1;
    const uint8_t *s_bytes = inner + hl;
    size_t s_len = vl;

    (void)inner_rem; // remaining bytes after s not needed

    // Strip leading zeros (DER sign padding) and right-align into 32 bytes
    memset(r_out, 0, 32);
    memset(s_out, 0, 32);

    // Strip leading zeros from r
    while (r_len > 1 && r_bytes[0] == 0x00) {
        r_bytes++;
        r_len--;
    }
    if (r_len > 32)
        return -1;
    memcpy(r_out + (32 - r_len), r_bytes, r_len);

    // Strip leading zeros from s
    while (s_len > 1 && s_bytes[0] == 0x00) {
        s_bytes++;
        s_len--;
    }
    if (s_len > 32)
        return -1;
    memcpy(s_out + (32 - s_len), s_bytes, s_len);

    return 0;
}

// dlopen typedefs for RSA-PSS fallback only
typedef void *EVP_PKEY;
typedef void *X509;
typedef void *EVP_PKEY_CTX;

typedef X509 *(*d2i_X509_fn)(X509 **, const unsigned char **, long);
typedef EVP_PKEY *(*X509_get_pubkey_fn)(X509 *);
typedef EVP_PKEY_CTX *(*EVP_PKEY_CTX_new_fn)(EVP_PKEY *, void *);
typedef int (*EVP_PKEY_verify_init_fn)(EVP_PKEY_CTX *);
typedef int (*EVP_PKEY_CTX_set_signature_md_fn)(EVP_PKEY_CTX *, void *);
typedef int (*EVP_PKEY_verify_fn)(
    EVP_PKEY_CTX *, const unsigned char *, size_t, const unsigned char *, size_t);
typedef void (*EVP_PKEY_CTX_free_fn)(EVP_PKEY_CTX *);
typedef void (*EVP_PKEY_free_fn)(EVP_PKEY *);
typedef void (*X509_free_fn)(X509 *);
typedef void *(*EVP_sha256_fn)(void);
typedef void *(*EVP_sha384_fn)(void);
typedef void *(*EVP_sha512_fn)(void);
typedef int (*EVP_PKEY_CTX_set_rsa_padding_fn)(EVP_PKEY_CTX *, int);
typedef int (*EVP_PKEY_CTX_set_rsa_pss_saltlen_fn)(EVP_PKEY_CTX *, int);
typedef int (*EVP_Digest_fn)(
    const void *, size_t, unsigned char *, unsigned int *, const void *, void *);

#define RSA_PKCS1_PSS_PADDING 6
#define RSA_PSS_SALTLEN_DIGEST -1

/// @brief Verify CertificateVerify signatures using libcrypto.
/// Supports ECDSA P-256/P-384 and RSA-PSS schemes over the already-hashed
/// CertificateVerify content.
static int tls_verify_signature_dlopen(rt_tls_session_t *session,
                                       uint16_t sig_scheme,
                                       const uint8_t *sig_bytes,
                                       uint16_t sig_len,
                                       const uint8_t *content_hash,
                                       size_t content_hash_len) {
    void *crypto_lib = tls_dlopen_libcrypto();
    if (!crypto_lib) {
        session->error = "TLS: CertVerify: libcrypto is required for signature verification";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    d2i_X509_fn fn_d2i_X509 = (d2i_X509_fn)dlsym(crypto_lib, "d2i_X509");
    X509_get_pubkey_fn fn_X509_get_pubkey =
        (X509_get_pubkey_fn)dlsym(crypto_lib, "X509_get_pubkey");
    EVP_PKEY_CTX_new_fn fn_EVP_PKEY_CTX_new =
        (EVP_PKEY_CTX_new_fn)dlsym(crypto_lib, "EVP_PKEY_CTX_new");
    EVP_PKEY_verify_init_fn fn_EVP_PKEY_verify_init =
        (EVP_PKEY_verify_init_fn)dlsym(crypto_lib, "EVP_PKEY_verify_init");
    EVP_PKEY_CTX_set_signature_md_fn fn_set_md =
        (EVP_PKEY_CTX_set_signature_md_fn)dlsym(crypto_lib, "EVP_PKEY_CTX_set_signature_md");
    EVP_PKEY_verify_fn fn_verify = (EVP_PKEY_verify_fn)dlsym(crypto_lib, "EVP_PKEY_verify");
    EVP_PKEY_CTX_free_fn fn_ctx_free = (EVP_PKEY_CTX_free_fn)dlsym(crypto_lib, "EVP_PKEY_CTX_free");
    EVP_PKEY_free_fn fn_pkey_free = (EVP_PKEY_free_fn)dlsym(crypto_lib, "EVP_PKEY_free");
    X509_free_fn fn_X509_free = (X509_free_fn)dlsym(crypto_lib, "X509_free");
    EVP_sha256_fn fn_sha256 = (EVP_sha256_fn)dlsym(crypto_lib, "EVP_sha256");
    EVP_sha384_fn fn_sha384 = (EVP_sha384_fn)dlsym(crypto_lib, "EVP_sha384");
    EVP_sha512_fn fn_sha512 = (EVP_sha512_fn)dlsym(crypto_lib, "EVP_sha512");
    EVP_PKEY_CTX_set_rsa_padding_fn fn_set_padding =
        (EVP_PKEY_CTX_set_rsa_padding_fn)dlsym(crypto_lib, "EVP_PKEY_CTX_set_rsa_padding");
    EVP_PKEY_CTX_set_rsa_pss_saltlen_fn fn_set_pss =
        (EVP_PKEY_CTX_set_rsa_pss_saltlen_fn)dlsym(crypto_lib, "EVP_PKEY_CTX_set_rsa_pss_saltlen");

    if (!fn_d2i_X509 || !fn_X509_get_pubkey || !fn_EVP_PKEY_CTX_new || !fn_EVP_PKEY_verify_init ||
        !fn_set_md || !fn_verify || !fn_ctx_free || !fn_pkey_free || !fn_X509_free || !fn_sha256) {
        dlclose(crypto_lib);
        session->error = "TLS: CertVerify: libcrypto symbols missing";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    const unsigned char *der_ptr = session->server_cert_der;
    X509 *x509 = fn_d2i_X509(NULL, &der_ptr, (long)session->server_cert_der_len);
    if (!x509) {
        dlclose(crypto_lib);
        session->error = "TLS: CertVerify: d2i_X509 failed";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    EVP_PKEY *pkey = fn_X509_get_pubkey(x509);
    fn_X509_free(x509);
    if (!pkey) {
        dlclose(crypto_lib);
        session->error = "TLS: CertVerify: X509_get_pubkey failed";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    EVP_PKEY_CTX *ctx = fn_EVP_PKEY_CTX_new(pkey, NULL);
    if (!ctx || fn_EVP_PKEY_verify_init(ctx) <= 0) {
        if (ctx)
            fn_ctx_free(ctx);
        fn_pkey_free(pkey);
        dlclose(crypto_lib);
        session->error = "TLS: CertVerify: EVP_PKEY_CTX init failed";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    void *md = NULL;
    switch (sig_scheme) {
        case 0x0403:
        case 0x0804:
            md = fn_sha256();
            break;
        case 0x0503:
        case 0x0805:
            md = (fn_sha384 ? fn_sha384() : NULL);
            break;
        case 0x0806:
            md = (fn_sha512 ? fn_sha512() : NULL);
            break;
        default:
            break;
    }

    if (!md) {
        fn_ctx_free(ctx);
        fn_pkey_free(pkey);
        dlclose(crypto_lib);
        session->error = "TLS: CertVerify: unsupported signature hash (Linux)";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    fn_set_md(ctx, md);

    if ((sig_scheme == 0x0804 || sig_scheme == 0x0805 || sig_scheme == 0x0806) && fn_set_padding &&
        fn_set_pss) {
        fn_set_padding(ctx, RSA_PKCS1_PSS_PADDING);
        fn_set_pss(ctx, RSA_PSS_SALTLEN_DIGEST);
    }

    int rc = fn_verify(ctx, sig_bytes, sig_len, content_hash, content_hash_len);

    fn_ctx_free(ctx);
    fn_pkey_free(pkey);
    dlclose(crypto_lib);

    if (rc != 1) {
        session->error = "TLS: CertificateVerify signature verification failed (Linux)";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    return RT_TLS_OK;
}

/// @brief Linux path: verify CertificateVerify via native ECDSA + libcrypto fallback.
///
/// Builds the message, hashes it (SHA-256 in-tree, SHA-384/512 via
/// libcrypto), then verifies. ECDSA P-256 uses the in-tree
/// `rt_ecdsa_p256_verify` (no libcrypto dependency); RSA-PSS / ECDSA
/// P-384 fall back to `tls_verify_signature_dlopen` which needs
/// libcrypto. Returns RT_TLS_OK on successful verification.
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

    // Build the raw 130-byte CertificateVerify message
    uint8_t cv_message[130];
    build_cert_verify_message(session->cert_transcript_hash, cv_message);

    // Determine hash length from signature scheme
    size_t hash_len = sig_scheme_hash_len(sig_scheme);
    if (hash_len == 0)
        hash_len = 32;

    // Compute the content hash with the correct algorithm.
    // For SHA-256 we use our built-in rt_sha256; for SHA-384/SHA-512 we use
    // dlopen'd libcrypto EVP_Digest when the RSA-PSS path needs it.
    uint8_t content_hash[64];
    memset(content_hash, 0, sizeof(content_hash));

    if (hash_len == 32) {
        rt_sha256(cv_message, 130, content_hash);
    } else {
        // SHA-384/SHA-512: use libcrypto EVP_Digest for the content hash
        void *crypto_lib = tls_dlopen_libcrypto();
        if (!crypto_lib) {
            session->error = "TLS: CertVerify: libcrypto required for SHA-384/SHA-512 hashing";
            return RT_TLS_ERROR_HANDSHAKE;
        }
        EVP_Digest_fn fn_digest = (EVP_Digest_fn)dlsym(crypto_lib, "EVP_Digest");
        void *(*fn_md)(void) = NULL;
        if (hash_len == 48)
            fn_md = (void *(*)(void))dlsym(crypto_lib, "EVP_sha384");
        else
            fn_md = (void *(*)(void))dlsym(crypto_lib, "EVP_sha512");

        if (!fn_digest || !fn_md) {
            dlclose(crypto_lib);
            session->error = "TLS: CertVerify: libcrypto missing EVP_Digest/EVP_sha* symbols";
            return RT_TLS_ERROR_HANDSHAKE;
        }
        unsigned int out_len = 0;
        int ok = fn_digest(cv_message, 130, content_hash, &out_len, fn_md(), NULL);
        dlclose(crypto_lib);
        if (ok != 1) {
            session->error = "TLS: CertVerify: EVP_Digest failed for content hash";
            return RT_TLS_ERROR_HANDSHAKE;
        }
        hash_len = out_len;
    }

    if (sig_scheme == 0x0403 || sig_scheme == 0x0503 || sig_scheme == 0x0804 ||
        sig_scheme == 0x0805 || sig_scheme == 0x0806) {
        return tls_verify_signature_dlopen(
            session, sig_scheme, sig_bytes, sig_len, content_hash, hash_len);
    }

    session->error = "TLS: CertificateVerify: unsupported signature scheme";
    return RT_TLS_ERROR_HANDSHAKE;
}

#endif // Platform CertificateVerify
