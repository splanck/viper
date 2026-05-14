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
#include <wincrypt.h>
#include <bcrypt.h>
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")
#endif

#if defined(__GNUC__) || defined(__clang__)
#define RT_TLS_MAYBE_UNUSED __attribute__((unused))
#else
#define RT_TLS_MAYBE_UNUSED
#endif

/// @brief Forward declaration: check whether a certificate's Extended Key Usage allows TLS Web Server Authentication.
/// @details Real implementation later in the file. Marked MAYBE_UNUSED so
///          builds that strip platform-specific verification paths still
///          link. Used by chain validation to reject leaf certs that don't
///          carry the id-kp-serverAuth EKU (1.3.6.1.5.5.7.3.1).
static RT_TLS_MAYBE_UNUSED int cert_allows_tls_server_auth(const uint8_t *cert_der,
                                                           size_t cert_len);

#if defined(_WIN32)
/// @brief Read an entire file into a freshly allocated buffer (Windows).
///        Caller must free() the returned pointer.
/// @return Pointer to null-terminated buffer on success, NULL on failure.
static char *tls_read_file_bytes_win(const char *path, size_t *len_out) {
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
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    len = ftell(f);
    if (len < 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    buf = (char *)malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (len > 0 && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    buf[len] = '\0';
    if (len_out)
        *len_out = (size_t)len;
    return buf;
}

/// @brief Load a PEM bundle or DER file and add all certificates to a Windows HCERTSTORE.
/// @return 1 if at least one certificate was added, 0 otherwise.
static int tls_add_pem_or_der_certs_to_store_win(HCERTSTORE store, const char *path) {
    static const char begin_marker[] = "-----BEGIN CERTIFICATE-----";
    static const char end_marker[] = "-----END CERTIFICATE-----";
    size_t file_len = 0;
    char *file = tls_read_file_bytes_win(path, &file_len);
    char *cursor = file;
    int added = 0;

    if (!store || !file)
        return 0;

    while (cursor && *cursor) {
        char *begin = strstr(cursor, begin_marker);
        char *end = begin ? strstr(begin, end_marker) : NULL;
        DWORD der_len = 0;
        uint8_t *der = NULL;
        size_t pem_len = 0;

        if (!begin || !end)
            break;
        end += strlen(end_marker);
        pem_len = (size_t)(end - begin);

        if (CryptStringToBinaryA(begin,
                                 (DWORD)pem_len,
                                 CRYPT_STRING_BASE64HEADER,
                                 NULL,
                                 &der_len,
                                 NULL,
                                 NULL) &&
            der_len > 0) {
            der = (uint8_t *)malloc(der_len);
            if (der &&
                CryptStringToBinaryA(begin,
                                     (DWORD)pem_len,
                                     CRYPT_STRING_BASE64HEADER,
                                     der,
                                     &der_len,
                                     NULL,
                                     NULL) &&
                CertAddEncodedCertificateToStore(store,
                                                 X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                                 der,
                                                 der_len,
                                                 CERT_STORE_ADD_ALWAYS,
                                                 NULL)) {
                added++;
            }
            free(der);
        }

        cursor = end;
    }

    if (added == 0 && file_len > 0 &&
        CertAddEncodedCertificateToStore(store,
                                         X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                         (const BYTE *)file,
                                         (DWORD)file_len,
                                         CERT_STORE_ADD_ALWAYS,
                                         NULL)) {
        added = 1;
    }

    free(file);
    return added > 0;
}
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
    if (pos + list_len != len) {
        session->error = "TLS: Certificate message has trailing data";
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
    if (!buf || !tag || !val_len || !hdr_len || buf_len < 2)
        return -1;

    *tag = buf[0];

    uint8_t l0 = buf[1];
    if (l0 < 0x80) {
        *val_len = l0;
        *hdr_len = 2;
    } else {
        size_t num_len_bytes = l0 & 0x7F;
        if (num_len_bytes == 0 || num_len_bytes > sizeof(size_t) ||
            2 + num_len_bytes > buf_len || buf[2] == 0x00)
            return -1;

        size_t v = 0;
        for (size_t i = 0; i < num_len_bytes; i++)
            v = (v << 8) | buf[2 + i];
        if (v < 128)
            return -1;

        *val_len = v;
        *hdr_len = 2 + num_len_bytes;
    }

    if (*hdr_len > buf_len || *val_len > buf_len - *hdr_len)
        return -1;

    return 0;
}

static RT_TLS_MAYBE_UNUSED int der_params_absent_or_null(const uint8_t *params, size_t params_len) {
    uint8_t tag;
    size_t vl;
    size_t hl;
    if (params_len == 0)
        return 1;
    if (der_read_tlv(params, params_len, &tag, &vl, &hl) != 0 || tag != 0x05)
        return 0;
    return vl == 0 && hl + vl == params_len;
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
static const uint8_t OID_X509_KEY_USAGE[] = {0x55, 0x1d, 0x0f};        // 2.5.29.15
static const uint8_t OID_X509_BASIC_CONSTRAINTS[] = {0x55, 0x1d, 0x13}; // 2.5.29.19
static const uint8_t OID_X509_EXT_KEY_USAGE[] = {0x55, 0x1d, 0x25};    // 2.5.29.37

/// @brief Return a pointer into cert_der at the DER-encoded Subject field, and write its total
///        TLV length (header + value) into *subject_len.
/// @return Non-null pointer into cert_der on success, NULL if the certificate is malformed.
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

/// @brief Validate a DNS name byte sequence for TLS hostname matching.
/// @details Enforces RFC 1035 / RFC 6125 hostname rules:
///          - Length 1..255 bytes total.
///          - Each label 1..63 bytes (no consecutive dots).
///          - Only ASCII letters, digits, and hyphen; no NUL, control bytes,
///            or non-ASCII (IDNA hostnames must be passed in punycode form).
///          - Wildcard `*` only valid when @p allow_wildcard is true, only
///            in the leftmost label, and only when followed by `.` plus a
///            non-empty suffix (so `*.example.com` is OK but `*.com` is
///            rejected by tls_wildcard_suffix_allowed downstream).
/// @return 1 if the name is well-formed, 0 otherwise.
static int tls_dns_name_bytes_valid(const uint8_t *name, size_t len, int allow_wildcard) {
    if (!name || len == 0 || len >= 256)
        return 0;
    size_t label_len = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t c = name[i];
        if (c == 0 || c < 0x20 || c >= 0x7f)
            return 0;
        if (c == '.') {
            if (label_len == 0 && i != len - 1)
                return 0;
            label_len = 0;
            continue;
        }
        if (c == '*') {
            if (!allow_wildcard || i != 0 || len < 3 || name[1] != '.')
                return 0;
        } else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') || c == '-')) {
            return 0;
        }
        label_len++;
        if (label_len > 63)
            return 0;
    }
    return 1;
}

/// @brief NUL-terminated convenience wrapper around tls_dns_name_bytes_valid.
/// @details Returns 0 for NULL. Otherwise calls strlen and delegates to the
///          byte-buffer validator. Used by call sites that already have a
///          C-string-shaped hostname.
static int tls_dns_name_valid(const char *name, int allow_wildcard) {
    return name &&
           tls_dns_name_bytes_valid((const uint8_t *)name, strlen(name), allow_wildcard);
}

/// @brief Check that a wildcard certificate's matching suffix is "long enough" to be safe.
/// @details Wildcards like `*.com` are dangerous — they would match every
///          .com domain. We reject any single-label suffix and a curated
///          set of public-suffix-list-ish multi-label suffixes (.co.uk,
///          .com.au, etc.). This is intentionally conservative: callers
///          deploying with private CAs whose policy permits broader
///          wildcards must validate at the application layer rather than
///          relying on this check to be lenient.
/// @return 1 if the suffix is acceptably specific, 0 if too broad.
static int tls_wildcard_suffix_allowed(const char *suffix) {
    static const char *const blocked_suffixes[] = {
        "com",     "net",    "org",    "edu",    "gov",    "mil",    "int",
        "uk",      "co.uk",  "org.uk", "ac.uk",  "gov.uk",
        "au",      "com.au", "net.au", "org.au", "edu.au", "gov.au",
        "jp",      "co.jp",  "ne.jp",  "or.jp",
        "de",      "fr",     "it",     "es",     "nl",     "br",     "ca",
        "io",      "dev",    "app"
    };

    if (!suffix || !tls_dns_name_valid(suffix, 0))
        return 0;
    if (!strchr(suffix, '.'))
        return 0;
    for (size_t i = 0; i < sizeof(blocked_suffixes) / sizeof(blocked_suffixes[0]); i++) {
        if (strcasecmp(suffix, blocked_suffixes[i]) == 0)
            return 0;
    }
    return 1;
}

/// @brief Test whether the verifier knows how to honor this critical extension OID.
/// @details Per RFC 5280 §4.2, a certificate that contains a critical
///          extension the verifier doesn't understand MUST be rejected.
///          We currently support: subjectAltName, keyUsage, basicConstraints,
///          and extendedKeyUsage. Anything else marked critical causes the
///          chain to fail.
/// @return 1 if the verifier handles this OID, 0 otherwise.
static int cert_critical_extension_supported(const uint8_t *oid, size_t oid_len) {
    return oid_matches(oid, oid_len, OID_SUBJECT_ALT_NAME, sizeof(OID_SUBJECT_ALT_NAME)) ||
           oid_matches(oid, oid_len, OID_X509_KEY_USAGE, sizeof(OID_X509_KEY_USAGE)) ||
           oid_matches(oid, oid_len, OID_X509_BASIC_CONSTRAINTS, sizeof(OID_X509_BASIC_CONSTRAINTS)) ||
           oid_matches(oid, oid_len, OID_X509_EXT_KEY_USAGE, sizeof(OID_X509_EXT_KEY_USAGE));
}

/// @brief Walk the TBSCertificate.extensions sequence and reject if any critical extension is unrecognized.
/// @details Implements the "MUST reject" rule from RFC 5280 §4.2: parses
///          the certificate's extensions list, and for each entry marked
///          critical that isn't in cert_critical_extension_supported, the
///          chain fails. Returns 1 if any unsupported critical extension
///          is found (caller should reject the cert), 0 if all critical
///          extensions are recognized.
/// @return 1 if the cert should be rejected, 0 if safe to continue.
static int cert_has_unsupported_critical_extension(const uint8_t *cert_der, size_t cert_len) {
    uint8_t tag;
    size_t vl, hl;
    if (der_read_tlv(cert_der, cert_len, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 1;
    const uint8_t *p = cert_der + hl;
    size_t rem = vl;
    if (der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 1;
    const uint8_t *tbs = p + hl;
    size_t tbs_rem = vl;

    if (der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0)
        return 1;
    if (tag == 0xA0) {
        tbs += hl + vl;
        tbs_rem -= hl + vl;
    }

    for (int i = 0; i < 6; i++) {
        if (der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0)
            return 1;
        tbs += hl + vl;
        tbs_rem -= hl + vl;
    }

    while (tbs_rem > 0) {
        if (der_read_tlv(tbs, tbs_rem, &tag, &vl, &hl) != 0)
            return 1;
        if (tag == 0xA3) {
            uint8_t seq_tag;
            size_t seq_len, seq_hl;
            if (der_read_tlv(tbs + hl, vl, &seq_tag, &seq_len, &seq_hl) != 0 ||
                seq_tag != 0x30)
                return 1;
            const uint8_t *exts = tbs + hl + seq_hl;
            size_t exts_rem = seq_len;
            while (exts_rem > 0) {
                uint8_t ext_tag;
                size_t ext_len, ext_hl;
                if (der_read_tlv(exts, exts_rem, &ext_tag, &ext_len, &ext_hl) != 0 ||
                    ext_tag != 0x30)
                    return 1;
                const uint8_t *ext = exts + ext_hl;
                size_t ext_rem = ext_len;
                uint8_t oid_tag;
                size_t oid_len, oid_hl;
                if (der_read_tlv(ext, ext_rem, &oid_tag, &oid_len, &oid_hl) != 0 ||
                    oid_tag != 0x06)
                    return 1;
                const uint8_t *oid = ext + oid_hl;
                ext += oid_hl + oid_len;
                ext_rem -= oid_hl + oid_len;

                int critical = 0;
                uint8_t next_tag;
                size_t next_len, next_hl;
                if (der_read_tlv(ext, ext_rem, &next_tag, &next_len, &next_hl) != 0)
                    return 1;
                if (next_tag == 0x01) {
                    if (next_len != 1)
                        return 1;
                    critical = ext[next_hl] != 0;
                    ext += next_hl + next_len;
                    ext_rem -= next_hl + next_len;
                    if (der_read_tlv(ext, ext_rem, &next_tag, &next_len, &next_hl) != 0)
                        return 1;
                }
                if (next_tag != 0x04)
                    return 1;
                if (critical && !cert_critical_extension_supported(oid, oid_len))
                    return 1;

                exts += ext_hl + ext_len;
                exts_rem -= ext_hl + ext_len;
            }
            return 0;
        }
        tbs += hl + vl;
        tbs_rem -= hl + vl;
    }
    return 0;
}

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
        if (t == 0x82 && tls_dns_name_bytes_valid(names + pos + hl, vl, 1)) {
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
                        oid_matches(ext + hl4, vl4,
                                    OID_SUBJECT_ALT_NAME,
                                    sizeof(OID_SUBJECT_ALT_NAME))) {
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
                        if (oid_matches(ext + hl4, vl4,
                                        OID_SUBJECT_ALT_NAME,
                                        sizeof(OID_SUBJECT_ALT_NAME))) {
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

/// @brief Check whether a DER-encoded certificate carries a Subject Alternative Name extension.
/// @details Walks the TBSCertificate extensions sequence looking for the
///          subjectAltName OID (2.5.29.17). Used by hostname-matching code
///          to enforce RFC 6125's "if SAN is present, ignore CN" rule —
///          presence of SAN means CN-based matching MUST NOT be used.
/// @return 1 if a SAN extension is present, 0 if not (or on parse failure).
int tls_cert_has_san_extension(const uint8_t *der, size_t der_len) {
    uint8_t t;
    size_t vl, hl;
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
            return 0;
        if (t == 0xA3) {
            const uint8_t *exts_wrap = tbs_val + pos + hl;
            uint8_t t2;
            size_t vl2, hl2;
            if (der_read_tlv(exts_wrap, vl, &t2, &vl2, &hl2) != 0 || t2 != 0x30)
                return 0;

            const uint8_t *exts = exts_wrap + hl2;
            size_t exts_len = vl2;
            size_t ep = 0;
            while (ep < exts_len) {
                uint8_t t3;
                size_t vl3, hl3;
                if (der_read_tlv(exts + ep, exts_len - ep, &t3, &vl3, &hl3) != 0)
                    return 0;
                if (t3 == 0x30) {
                    const uint8_t *ext = exts + ep + hl3;
                    uint8_t t4;
                    size_t vl4, hl4;
                    if (der_read_tlv(ext, vl3, &t4, &vl4, &hl4) == 0 && t4 == 0x06 &&
                        oid_matches(ext + hl4, vl4, OID_SUBJECT_ALT_NAME, sizeof(OID_SUBJECT_ALT_NAME))) {
                        return 1;
                    }
                }
                ep += hl3 + vl3;
            }
            return 0;
        }
        pos += hl + vl;
    }

    return 0;
}

/// @brief Extract the CommonName (CN) attribute from the certificate Subject.
static int tls_extract_cn_from_name(const uint8_t *name_der, size_t name_len, char cn_out[256]) {
    uint8_t t;
    size_t vl, hl;
    if (!name_der || !cn_out || der_read_tlv(name_der, name_len, &t, &vl, &hl) != 0 ||
        t != 0x30) {
        return 0;
    }

    const uint8_t *seq_val = name_der + hl;
    size_t seq_len = vl;
    size_t sp = 0;

    while (sp < seq_len) {
        uint8_t ts;
        size_t vls, hls;
        if (der_read_tlv(seq_val + sp, seq_len - sp, &ts, &vls, &hls) != 0)
            return 0;

        if (ts == 0x31) {
            const uint8_t *set_val = seq_val + sp + hls;
            size_t set_pos = 0;
            while (set_pos < vls) {
                uint8_t ta;
                size_t vla, hla;
                if (der_read_tlv(set_val + set_pos, vls - set_pos, &ta, &vla, &hla) != 0)
                    return 0;
                if (ta == 0x30) {
                    const uint8_t *atv = set_val + set_pos + hla;
                    uint8_t to;
                    size_t vlo, hlo;
                    if (der_read_tlv(atv, vla, &to, &vlo, &hlo) == 0 && to == 0x06 &&
                        oid_matches(atv + hlo, vlo, OID_COMMON_NAME, sizeof(OID_COMMON_NAME))) {
                        size_t after_oid = hlo + vlo;
                        if (after_oid >= vla)
                            return 0;
                        const uint8_t *val_start = atv + after_oid;
                        size_t val_remaining = vla - after_oid;
                        uint8_t tv;
                        size_t vlv, hlv;
                        if (der_read_tlv(val_start, val_remaining, &tv, &vlv, &hlv) == 0 &&
                            tls_dns_name_bytes_valid(val_start + hlv, vlv, 1)) {
                            memcpy(cn_out, val_start + hlv, vlv);
                            cn_out[vlv] = '\0';
                            return 1;
                        }
                    }
                }
                set_pos += hla + vla;
            }
        }

        sp += hls + vls;
    }

    return 0;
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
    size_t subject_len = 0;
    const uint8_t *subject = cert_get_subject(der, der_len, &subject_len);
    if (!subject)
        return 0;
    return tls_extract_cn_from_name(subject, subject_len, cn_out);
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
    if (!tls_dns_name_valid(pattern, 1) || !tls_dns_name_valid(hostname, 0))
        return 0;

    if (pattern[0] == '*' && pattern[1] == '.') {
        // Wildcard: *.example.com
        const char *suffix = pattern + 2; // "example.com"
        if (!tls_wildcard_suffix_allowed(suffix))
            return 0;
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

/// @brief Walk one SAN extension and check every dNSName against @p hostname.
/// @details This is separate from tls_extract_san_names because verification
///          must not be capped by the fixed-size public extraction buffer.
static int san_ext_has_dns_match(const uint8_t *ext_val,
                                 size_t ext_len,
                                 const char *hostname,
                                 int *saw_dns_san) {
    uint8_t t;
    size_t vl, hl;
    if (saw_dns_san)
        *saw_dns_san = 0;

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
        if (t == 0x82 && tls_dns_name_bytes_valid(names + pos + hl, vl, 1)) {
            char name[256];
            if (saw_dns_san)
                *saw_dns_san = 1;
            memcpy(name, names + pos + hl, vl);
            name[vl] = '\0';
            if (tls_match_hostname(name, hostname))
                return 1;
        }
        pos += hl + vl;
    }
    return 0;
}

/// @brief Return whether any DNS SubjectAltName matches, scanning all names.
static int tls_cert_has_matching_dns_san(const uint8_t *der,
                                         size_t der_len,
                                         const char *hostname,
                                         int *saw_dns_san) {
    int saw_any = 0;
    uint8_t t;
    size_t vl, hl;
    if (saw_dns_san)
        *saw_dns_san = 0;

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
                        oid_matches(ext + hl4, vl4,
                                    OID_SUBJECT_ALT_NAME,
                                    sizeof(OID_SUBJECT_ALT_NAME))) {
                        size_t after_oid = hl4 + vl4;
                        if (after_oid < vl3) {
                            uint8_t nt;
                            size_t nvl, nhl;
                            if (der_read_tlv(ext + after_oid, vl3 - after_oid, &nt, &nvl, &nhl) == 0) {
                                if (nt == 0x01)
                                    after_oid += nhl + nvl;
                                if (after_oid < vl3) {
                                    int saw_this_ext = 0;
                                    if (san_ext_has_dns_match(
                                            ext + after_oid, vl3 - after_oid, hostname, &saw_this_ext))
                                        return 1;
                                    if (saw_this_ext)
                                        saw_any = 1;
                                }
                            }
                        }
                    }
                }

                ep += hl3 + vl3;
            }
        }

        pos += hl + vl;
    }

    if (saw_dns_san)
        *saw_dns_san = saw_any;
    return 0;
}

// Maximum number of SAN DNS names exposed through tls_extract_san_names.
#define TLS_MAX_SAN_NAMES 64

/// @brief Verify that the session's intended hostname matches the leaf
///        certificate (RFC 6125 § 6).
/// @details The verification ordering is load-bearing for security:
///          1. **IP literals** (e.g., `"192.0.2.1"`, `"::1"`) match exclusively
///             against the certificate's iPAddress SANs. Falling back to dNSName
///             SAN or CN matching for an IP literal would be a bypass —
///             `"192.0.2.1"` should never match a cert issued for the literal
///             string `"192.0.2.1"` as a DNS name.
///          2. **DNS hostnames** scan every dNSName SAN entry before falling
///             back to CommonName.
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

    if (!host || !host[0]) {
        session->error = "TLS: hostname verification requires a hostname";
        return RT_TLS_ERROR_HANDSHAKE;
    }

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

    if (!tls_dns_name_valid(host, 0)) {
        session->error = "TLS: invalid hostname for certificate verification";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Try SubjectAltName first (RFC 6125 §6.4: SAN takes precedence over CN).
    int saw_dns_san = 0;
    if (tls_cert_has_matching_dns_san(
            session->server_cert_der, session->server_cert_der_len, host, &saw_dns_san))
        return RT_TLS_OK;
    if (saw_dns_san) {
        session->error = "TLS: certificate hostname mismatch (SAN did not match)";
        return RT_TLS_ERROR_HANDSHAKE;
    }
    if (tls_cert_has_san_extension(session->server_cert_der, session->server_cert_der_len)) {
        session->error = "TLS: certificate SAN extension does not contain any DNS names";
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

/// @brief Check whether a certificate permits TLS server authentication (Windows).
///        Inspects KeyUsage (digitalSignature) and ExtendedKeyUsage
///        (id-kp-serverAuth or anyExtendedKeyUsage) extensions via DER parsing.
/// @return 1 if both checks pass, 0 if any extension explicitly forbids server auth.
static RT_TLS_MAYBE_UNUSED int cert_allows_tls_server_auth(const uint8_t *cert_der, size_t cert_len) {
    static const uint8_t OID_KEY_USAGE[] = {0x55, 0x1d, 0x0f};
    static const uint8_t OID_EXTENDED_KEY_USAGE[] = {0x55, 0x1d, 0x25};
    static const uint8_t OID_SERVER_AUTH[] = {0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01};
    static const uint8_t OID_ANY_EXTENDED_KEY_USAGE[] = {0x55, 0x1d, 0x25, 0x00};
    uint8_t tag;
    size_t vl, hl;
    const uint8_t *p = cert_der;
    size_t rem = cert_len;
    const uint8_t *tbs = NULL;
    size_t tbs_rem = 0;
    int key_usage_allows = 1;
    int saw_eku = 0;
    int eku_allows = 1;

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
                if (der_read_tlv(ext, exts_rem, &tag, &ext_seq_len, &ext_hdr_len) != 0 ||
                    tag != 0x30)
                    return 0;
                ext += ext_hdr_len;
                ext_rem = ext_seq_len;
                if (der_read_tlv(ext, ext_rem, &tag, &vl, &hl) != 0 || tag != 0x06)
                    return 0;
                {
                    int is_key_usage = (vl == sizeof(OID_KEY_USAGE) &&
                                        memcmp(ext + hl, OID_KEY_USAGE, sizeof(OID_KEY_USAGE)) == 0);
                    int is_eku = (vl == sizeof(OID_EXTENDED_KEY_USAGE) &&
                                  memcmp(ext + hl,
                                         OID_EXTENDED_KEY_USAGE,
                                         sizeof(OID_EXTENDED_KEY_USAGE)) == 0);
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

                    if (is_key_usage) {
                        if (der_read_tlv(extn_value, extn_value_len, &tag, &vl, &hl) == 0 &&
                            tag == 0x03 && vl >= 2 && extn_value[hl] <= 7) {
                            const uint8_t *bits = extn_value + hl;
                            key_usage_allows = (bits[1] & 0x80) != 0;
                        } else
                            key_usage_allows = 0;
                    } else if (is_eku) {
                        const uint8_t *eku = extn_value;
                        size_t eku_len = extn_value_len;
                        saw_eku = 1;
                        eku_allows = 0;
                        if (der_read_tlv(eku, eku_len, &tag, &vl, &hl) == 0 && tag == 0x30) {
                            eku += hl;
                            eku_len = vl;
                            while (eku_len > 0) {
                                if (der_read_tlv(eku, eku_len, &tag, &vl, &hl) != 0 || tag != 0x06)
                                    break;
                                if ((vl == sizeof(OID_SERVER_AUTH) &&
                                     memcmp(eku + hl, OID_SERVER_AUTH, sizeof(OID_SERVER_AUTH)) == 0) ||
                                    (vl == sizeof(OID_ANY_EXTENDED_KEY_USAGE) &&
                                     memcmp(eku + hl,
                                            OID_ANY_EXTENDED_KEY_USAGE,
                                            sizeof(OID_ANY_EXTENDED_KEY_USAGE)) == 0)) {
                                    eku_allows = 1;
                                    break;
                                }
                                eku += hl + vl;
                                eku_len -= hl + vl;
                            }
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

    return key_usage_allows && (!saw_eku || eku_allows);
}

/// @brief Validate the server certificate chain against the Windows system trust store (CryptoAPI).
///        If session->ca_file is set, builds an exclusive engine from that bundle; otherwise
///        uses the default ROOT store.  Intermediate certificates from the TLS handshake are
///        added as additional store hints.
/// @return RT_TLS_OK on success, RT_TLS_ERROR_HANDSHAKE on validation failure.
int tls_verify_chain(rt_tls_session_t *session) {
    if (!session->server_cert_der_len) {
        session->error = "TLS: no certificate to validate";
        return RT_TLS_ERROR_HANDSHAKE;
    }
    if (cert_has_unsupported_critical_extension(session->server_cert_der,
                                                session->server_cert_der_len)) {
        session->error = "TLS: certificate contains unsupported critical extension";
        return RT_TLS_ERROR_HANDSHAKE;
    }
    if (!cert_allows_tls_server_auth(session->server_cert_der, session->server_cert_der_len)) {
        session->error = "TLS: certificate is not valid for TLS server authentication";
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

    HCERTSTORE root_store = NULL;
    HCERTCHAINENGINE chain_engine = NULL;
    HCERTSTORE extra_store =
        CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, NULL);
    if (!extra_store) {
        CertFreeCertificateContext(cert_ctx);
        session->error = "TLS: could not create intermediate certificate store";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    if (session->ca_file[0]) {
        root_store =
            CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, NULL);
        if (!root_store) {
            CertCloseStore(extra_store, 0);
            CertFreeCertificateContext(cert_ctx);
            session->error = "TLS: could not create custom root certificate store";
            return RT_TLS_ERROR_HANDSHAKE;
        }
        if (!tls_add_pem_or_der_certs_to_store_win(root_store, session->ca_file)) {
            CertCloseStore(root_store, 0);
            CertCloseStore(extra_store, 0);
            CertFreeCertificateContext(cert_ctx);
            session->error = "TLS: could not load custom CA file";
            return RT_TLS_ERROR_HANDSHAKE;
        }

        CERT_CHAIN_ENGINE_CONFIG engine_config = {0};
        engine_config.cbSize = sizeof(engine_config);
#if defined(CERT_CHAIN_EXCLUSIVE_ENABLE_CA_FLAG)
        engine_config.hExclusiveRoot = root_store;
        engine_config.dwExclusiveFlags = CERT_CHAIN_EXCLUSIVE_ENABLE_CA_FLAG;
#else
        engine_config.hRestrictedRoot = root_store;
#endif
        if (!CertCreateCertificateChainEngine(&engine_config, &chain_engine)) {
            CertCloseStore(root_store, 0);
            CertCloseStore(extra_store, 0);
            CertFreeCertificateContext(cert_ctx);
            session->error = "TLS: could not create custom certificate chain engine";
            return RT_TLS_ERROR_HANDSHAKE;
        }
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
                if (chain_engine)
                    CertFreeCertificateChainEngine(chain_engine);
                if (root_store)
                    CertCloseStore(root_store, 0);
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
                if (chain_engine)
                    CertFreeCertificateChainEngine(chain_engine);
                if (root_store)
                    CertCloseStore(root_store, 0);
                CertCloseStore(extra_store, 0);
                CertFreeCertificateContext(cert_ctx);
                session->error = "TLS: could not add intermediate certificate to store";
                return RT_TLS_ERROR_HANDSHAKE;
            }
        }
    }

    PCCERT_CHAIN_CONTEXT chain_ctx = NULL;
    BOOL ok = CertGetCertificateChain(
        chain_engine, cert_ctx, NULL, extra_store, &chain_para, 0, NULL, &chain_ctx);
    CertCloseStore(extra_store, 0);

    if (!ok || !chain_ctx) {
        if (chain_engine)
            CertFreeCertificateChainEngine(chain_engine);
        if (root_store)
            CertCloseStore(root_store, 0);
        CertFreeCertificateContext(cert_ctx);
        session->error = "TLS: CertGetCertificateChain failed";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    CERT_CHAIN_POLICY_PARA policy_para = {0};
    policy_para.cbSize = sizeof(policy_para);

    CERT_CHAIN_POLICY_STATUS policy_status = {0};
    policy_status.cbSize = sizeof(policy_status);

    ok = CertVerifyCertificateChainPolicy(
        CERT_CHAIN_POLICY_BASE, chain_ctx, &policy_para, &policy_status);

    CertFreeCertificateChain(chain_ctx);
    if (chain_engine)
        CertFreeCertificateChainEngine(chain_engine);
    if (root_store)
        CertCloseStore(root_store, 0);
    CertFreeCertificateContext(cert_ctx);

    if (!ok || policy_status.dwError != 0) {
        session->error = "TLS: certificate chain validation failed (Windows)";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    return RT_TLS_OK;
}

#else // Native macOS/Linux

/// @brief Find a PEM CA bundle file by probing standard OS paths.
/// @return Path string literal on success, NULL if none found.
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

/// @brief Decode a Base64 PEM body (between header/footer markers) into DER bytes.
///        Skips whitespace and stops at '=' padding.
/// @return Number of DER bytes written; 0 on output-buffer overflow or invalid input.
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

/// @brief Compare two DER-encoded X.509 Name structures for byte-exact equality.
/// @return 1 if equal, 0 otherwise.
static RT_TLS_MAYBE_UNUSED int der_names_equal(const uint8_t *a_der,
                                               size_t a_len,
                                               const uint8_t *b_der,
                                               size_t b_len) {
    return a_len == b_len && memcmp(a_der, b_der, a_len) == 0;
}

/// @brief Return a pointer into cert_der at the DER-encoded Issuer field, and write its total
///        TLV length (header + value) into *issuer_len.
/// @return Non-null pointer into cert_der on success, NULL if the certificate is malformed.
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

/// @brief Parse a 2-digit decimal ASCII number from a DER timestamp into @p *out.
/// @details DER UTCTime / GeneralizedTime fields are sequences of ASCII
///          decimal digits ("YYMMDD..." or "YYYYMMDD..."). This helper
///          parses one 2-digit field with strict validation — any non-digit
///          byte rejects the timestamp.
/// @return 1 on success, 0 on any non-digit byte.
static int der_decimal_2(const uint8_t *data, int *out) {
    if (data[0] < '0' || data[0] > '9' || data[1] < '0' || data[1] > '9')
        return 0;
    *out = (data[0] - '0') * 10 + (data[1] - '0');
    return 1;
}

/// @brief Parse a 4-digit decimal ASCII number from a DER GeneralizedTime year field.
/// @details Composes two der_decimal_2 calls into the high/low pair of a
///          four-digit year. UTCTime uses 2-digit years (parsed via
///          der_decimal_2 with century inference); GeneralizedTime uses
///          this 4-digit form directly.
/// @return 1 on success, 0 on any non-digit byte.
static int der_decimal_4(const uint8_t *data, int *out) {
    int hi, lo;
    if (!der_decimal_2(data, &hi) || !der_decimal_2(data + 2, &lo))
        return 0;
    *out = hi * 100 + lo;
    return 1;
}

/// @brief Test whether @p year is a Gregorian leap year (RFC 5280 § 4.1.2.5).
/// @details Standard rule: divisible by 4, except century years not
///          divisible by 400. Used by der_time_days_in_month to validate
///          February day counts.
static int der_time_is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/// @brief Return the number of days in a given Gregorian month, accounting for leap years.
/// @details Used to validate certificate notBefore / notAfter timestamps —
///          a date like "2026-02-30" must be rejected. Returns 0 for
///          out-of-range month values; 29 for February in leap years; the
///          standard table value otherwise.
static int der_time_days_in_month(int year, int month) {
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
        return 0;
    if (month == 2 && der_time_is_leap_year(year))
        return 29;
    return days[month - 1];
}

/// @brief Parse a UTC DER UTCTime or GeneralizedTime value into a time_t.
/// @return Parsed time, or (time_t)-1 on format error.
static time_t parse_der_time(const uint8_t *data, size_t len, uint8_t tag) {
    struct tm tm_val;
    int pos;
    int year, month, day, hour, minute, second;

    memset(&tm_val, 0, sizeof(tm_val));
    if (!data)
        return (time_t)-1;
    if (tag == 0x17) {
        int yy;
        if (len != 13 || data[12] != 'Z' || !der_decimal_2(data, &yy))
            return (time_t)-1;
        year = (yy >= 50) ? 1900 + yy : 2000 + yy;
        pos = 2;
    } else if (tag == 0x18) {
        if (len != 15 || data[14] != 'Z' || !der_decimal_4(data, &year))
            return (time_t)-1;
        pos = 4;
    } else {
        return (time_t)-1;
    }

    if (!der_decimal_2(data + pos, &month) ||
        !der_decimal_2(data + pos + 2, &day) ||
        !der_decimal_2(data + pos + 4, &hour) ||
        !der_decimal_2(data + pos + 6, &minute) ||
        !der_decimal_2(data + pos + 8, &second)) {
        return (time_t)-1;
    }
    if (month < 1 || month > 12 || day < 1 || day > der_time_days_in_month(year, month) ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return (time_t)-1;
    }

    tm_val.tm_year = year - 1900;
    tm_val.tm_mon = month - 1;
    tm_val.tm_mday = day;
    tm_val.tm_hour = hour;
    tm_val.tm_min = minute;
    tm_val.tm_sec = second;
    tm_val.tm_isdst = 0;
    return rt_network_timegm_utc(&tm_val);
}

/// @brief Check that the certificate's Validity window contains the current wall-clock time.
/// @return 0 if the certificate is currently valid, -1 if expired, not-yet-valid, or malformed.
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
        time_t now;
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
        now = time(NULL);
        if (now == (time_t)-1 || now < not_before || now > not_after) {
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

/// @brief Check whether a certificate is self-signed (Subject == Issuer byte-for-byte).
/// @return 1 if self-signed, 0 otherwise.
static RT_TLS_MAYBE_UNUSED int cert_is_self_signed(const uint8_t *cert_der, size_t cert_len) {
    size_t subject_len = 0;
    size_t issuer_len = 0;
    const uint8_t *subject = cert_get_subject(cert_der, cert_len, &subject_len);
    const uint8_t *issuer = cert_get_issuer(cert_der, cert_len, &issuer_len);
    return subject && issuer && der_names_equal(subject, subject_len, issuer, issuer_len);
}

/// @brief Check whether a certificate permits TLS server authentication (native macOS/Linux).
///        Same logic as the Windows variant but implemented with the in-tree DER parser.
/// @return 1 if both KeyUsage and ExtendedKeyUsage checks pass, 0 otherwise.
static RT_TLS_MAYBE_UNUSED int cert_allows_tls_server_auth(const uint8_t *cert_der, size_t cert_len) {
    static const uint8_t OID_KEY_USAGE[] = {0x55, 0x1d, 0x0f};
    static const uint8_t OID_EXTENDED_KEY_USAGE[] = {0x55, 0x1d, 0x25};
    static const uint8_t OID_SERVER_AUTH[] = {0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01};
    static const uint8_t OID_ANY_EXTENDED_KEY_USAGE[] = {0x55, 0x1d, 0x25, 0x00};
    uint8_t tag;
    size_t vl, hl;
    const uint8_t *p = cert_der;
    size_t rem = cert_len;
    const uint8_t *tbs = NULL;
    size_t tbs_rem = 0;
    int key_usage_allows = 1;
    int saw_eku = 0;
    int eku_allows = 1;

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
                    int is_key_usage = (vl == sizeof(OID_KEY_USAGE) &&
                                        memcmp(ext + hl, OID_KEY_USAGE, sizeof(OID_KEY_USAGE)) == 0);
                    int is_eku = (vl == sizeof(OID_EXTENDED_KEY_USAGE) &&
                                  memcmp(ext + hl,
                                         OID_EXTENDED_KEY_USAGE,
                                         sizeof(OID_EXTENDED_KEY_USAGE)) == 0);
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

                    if (is_key_usage) {
                        if (der_read_tlv(extn_value, extn_value_len, &tag, &vl, &hl) == 0 &&
                            tag == 0x03 && vl >= 2 && extn_value[hl] <= 7) {
                            const uint8_t *bits = extn_value + hl;
                            key_usage_allows = (bits[1] & 0x80) != 0;
                        } else
                            key_usage_allows = 0;
                    } else if (is_eku) {
                        const uint8_t *eku = extn_value;
                        size_t eku_len = extn_value_len;
                        saw_eku = 1;
                        eku_allows = 0;
                        if (der_read_tlv(eku, eku_len, &tag, &vl, &hl) == 0 && tag == 0x30) {
                            eku += hl;
                            eku_len = vl;
                            while (eku_len > 0) {
                                if (der_read_tlv(eku, eku_len, &tag, &vl, &hl) != 0 || tag != 0x06)
                                    break;
                                if ((vl == sizeof(OID_SERVER_AUTH) &&
                                     memcmp(eku + hl, OID_SERVER_AUTH, sizeof(OID_SERVER_AUTH)) == 0) ||
                                    (vl == sizeof(OID_ANY_EXTENDED_KEY_USAGE) &&
                                     memcmp(eku + hl,
                                            OID_ANY_EXTENDED_KEY_USAGE,
                                            sizeof(OID_ANY_EXTENDED_KEY_USAGE)) == 0)) {
                                    eku_allows = 1;
                                    break;
                                }
                                eku += hl + vl;
                                eku_len -= hl + vl;
                            }
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

    return key_usage_allows && (!saw_eku || eku_allows);
}

/// @brief Check whether a certificate is a CA (BasicConstraints cA=TRUE and KeyUsage keyCertSign).
/// @return 1 if the certificate is a CA with appropriate key usage, 0 otherwise.
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

/// @brief Extract the RSA public key from a certificate's SubjectPublicKeyInfo and parse it
///        into *out via rt_rsa_parse_public_key_pkcs1.
/// @return 1 on success, 0 if the key is absent, malformed, or not RSA.
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

    if (!cert_der || !out)
        return 0;
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
    algo += hl + vl;
    algo_rem -= hl + vl;
    if (!der_params_absent_or_null(algo, algo_rem))
        return 0;
    if (der_read_tlv(spki, spki_rem, &tag, &vl, &hl) != 0 || tag != 0x03 || vl < 2)
        return 0;
    if (hl + vl != spki_rem)
        return 0;
    bits = spki + hl;
    if (bits[0] != 0x00)
        return 0;
    return rt_rsa_parse_public_key_pkcs1(bits + 1, vl - 1, out);
}

/// @brief Extract an EC P-256 public key from a certificate's SubjectPublicKeyInfo.
///        Requires OID 1.2.840.10045.2.1 (id-ecPublicKey) with named curve prime256v1.
///        Writes the uncompressed point coordinates (X, Y) to x_out/y_out.
/// @return 0 on success, -1 if absent, malformed, or not an EC P-256 key.
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

    if (!cert_der || !x_out || !y_out)
        return -1;
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
    algo += hl + vl;
    algo_rem -= hl + vl;
    if (algo_rem != 0)
        return -1;
    if (der_read_tlv(spki, spki_rem, &tag, &vl, &hl) != 0 || tag != 0x03 || vl != 66)
        return -1;
    if (hl + vl != spki_rem)
        return -1;
    bits = spki + hl;
    if (bits[0] != 0x00 || bits[1] != 0x04)
        return -1;
    memcpy(x_out, bits + 2, 32);
    memcpy(y_out, bits + 34, 32);
    return 0;
}

static int ecdsa_der_integer_is_canonical(const uint8_t *bytes, size_t len) {
    if (!bytes || len == 0)
        return 0;
    if ((bytes[0] & 0x80u) != 0)
        return 0;
    if (len == 1)
        return bytes[0] != 0x00;
    if (bytes[0] == 0x00)
        return (bytes[1] & 0x80u) != 0;
    return 1;
}

/// @brief Parse a DER-encoded ECDSA signature (SEQUENCE { INTEGER r, INTEGER s }) and write
///        the r and s scalars as 32-byte big-endian values (zero-padded, leading 0x00 stripped).
/// @return 0 on success, -1 on malformed input.
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

    if (der_read_tlv(sig, sig_len, &tag, &vl, &hl) != 0 || tag != 0x30 ||
        hl + vl != sig_len)
        return -1;
    inner = sig + hl;
    inner_rem = vl;
    if (der_read_tlv(inner, inner_rem, &tag, &vl, &hl) != 0 || tag != 0x02)
        return -1;
    r_bytes = inner + hl;
    r_len = vl;
    if (!ecdsa_der_integer_is_canonical(r_bytes, r_len))
        return -1;
    inner += hl + vl;
    inner_rem -= hl + vl;
    if (der_read_tlv(inner, inner_rem, &tag, &vl, &hl) != 0 || tag != 0x02)
        return -1;
    s_bytes = inner + hl;
    s_len = vl;
    if (!ecdsa_der_integer_is_canonical(s_bytes, s_len))
        return -1;
    inner += hl + vl;
    inner_rem -= hl + vl;
    if (inner_rem != 0)
        return -1;

    if (r_len > 1 && r_bytes[0] == 0x00) {
        r_bytes++;
        r_len--;
    }
    if (s_len > 1 && s_bytes[0] == 0x00) {
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

/// @brief Decompose an X.509 certificate into its three top-level components:
///        TBSCertificate (for hashing), signatureAlgorithm OID, and signatureValue bit-string.
///        All output pointers point into cert_der; no allocation is performed.
/// @return 1 on success, 0 if the certificate structure is malformed.
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

    if (!cert_der || !tbs_der || !tbs_len || !alg_der || !alg_len || !sig_bytes || !sig_len)
        return 0;
    if (der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x30)
        return 0;
    if (hl + vl != cert_len)
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
    p += hl + vl;
    rem -= hl + vl;
    if (rem != 0)
        return 0;
    return 1;
}

/// @brief Map a DER OID value to one of SHA-256, SHA-384, or SHA-512.
/// @return 1 and sets *hash_id if the OID is recognised, 0 otherwise.
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

/// @brief Parse an RSASSA-PSS AlgorithmIdentifier parameter SEQUENCE (RFC 4055 §3.1).
///        Populates alg->hash_id, validates the MGF-1 hash matches, and checks the salt length
///        equals the hash output length.  Only SHA-256/384/512 hash+MGF combinations are accepted.
/// @return 1 if the PSS parameters are valid and consistent, 0 otherwise.
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
    if (hl + vl != params_len)
        return 0;
    p += hl;
    rem = vl;

    while (rem > 0) {
        if (der_read_tlv(p, rem, &tag, &vl, &hl) != 0)
            return 0;
        uint8_t field_tag = tag;
        const uint8_t *field_value = p + hl;
        size_t field_len = vl;
        size_t field_total_len = hl + vl;
        if (field_tag == 0xA0) {
            const uint8_t *q = field_value;
            size_t q_rem = field_len;
            if (have_hash)
                return 0;
            if (der_read_tlv(q, q_rem, &tag, &vl, &hl) != 0 || tag != 0x30)
                return 0;
            if (hl + vl != q_rem)
                return 0;
            q += hl;
            q_rem = vl;
            if (der_read_tlv(q, q_rem, &tag, &vl, &hl) != 0 || tag != 0x06)
                return 0;
            if (!cert_parse_hash_oid(q + hl, vl, &alg->hash_id))
                return 0;
            q += hl + vl;
            q_rem -= hl + vl;
            if (!der_params_absent_or_null(q, q_rem))
                return 0;
            have_hash = 1;
        } else if (field_tag == 0xA1) {
            const uint8_t *q = field_value;
            size_t q_rem = field_len;
            if (have_mgf)
                return 0;
            if (der_read_tlv(q, q_rem, &tag, &vl, &hl) != 0 || tag != 0x30)
                return 0;
            if (hl + vl != q_rem)
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
            if (hl + vl != q_rem)
                return 0;
            q += hl;
            q_rem = vl;
            if (der_read_tlv(q, q_rem, &tag, &vl, &hl) != 0 || tag != 0x06)
                return 0;
            if (!cert_parse_hash_oid(q + hl, vl, &mgf_hash))
                return 0;
            q += hl + vl;
            q_rem -= hl + vl;
            if (!der_params_absent_or_null(q, q_rem))
                return 0;
            have_mgf = 1;
        } else if (field_tag == 0xA2) {
            const uint8_t *q = field_value;
            size_t q_rem = field_len;
            size_t parsed_salt_len = 0;
            if (have_salt)
                return 0;
            if (der_read_tlv(q, q_rem, &tag, &vl, &hl) != 0 || tag != 0x02 || vl == 0 || vl > 4)
                return 0;
            if (hl + vl != q_rem)
                return 0;
            if ((q[hl] & 0x80u) != 0 || (vl > 1 && q[hl] == 0x00 && (q[hl + 1] & 0x80u) == 0))
                return 0;
            for (size_t i = 0; i < vl; i++)
                parsed_salt_len = (parsed_salt_len << 8) | q[hl + i];
            salt_len = parsed_salt_len;
            have_salt = 1;
        } else if (field_tag == 0xA3) {
            const uint8_t *q = field_value;
            size_t q_rem = field_len;
            if (der_read_tlv(q, q_rem, &tag, &vl, &hl) != 0 || tag != 0x02 || vl != 1 || q[hl] != 1)
                return 0;
            if (hl + vl != q_rem)
                return 0;
        } else {
            return 0;
        }
        p += field_total_len;
        rem -= field_total_len;
    }

    return have_hash && have_mgf && have_salt && mgf_hash == alg->hash_id &&
           salt_len == hash_len_from_id(alg->hash_id);
}

/// @brief Parse a certificate signatureAlgorithm SEQUENCE and classify it as one of
///        RSA-PKCS1, RSA-PSS, or ECDSA-P256, filling in alg->kind and alg->hash_id.
///        RSA-PSS additionally validates the parameters via cert_parse_pss_params.
/// @return 1 if recognised and parsed, 0 otherwise.
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
    if (hl + vl != alg_len)
        return 0;
    p += hl;
    rem = vl;
    if (der_read_tlv(p, rem, &tag, &vl, &hl) != 0 || tag != 0x06)
        return 0;
    const uint8_t *oid = p + hl;
    size_t oid_len = vl;
    p += hl + vl;
    rem -= hl + vl;
    if (oid_len == sizeof(OID_SHA256_RSA) && memcmp(oid, OID_SHA256_RSA, sizeof(OID_SHA256_RSA)) == 0) {
        if (!der_params_absent_or_null(p, rem))
            return 0;
        alg->kind = TLS_CERT_SIG_RSA_PKCS1;
        alg->hash_id = RT_RSA_HASH_SHA256;
        return 1;
    }
    if (oid_len == sizeof(OID_SHA384_RSA) && memcmp(oid, OID_SHA384_RSA, sizeof(OID_SHA384_RSA)) == 0) {
        if (!der_params_absent_or_null(p, rem))
            return 0;
        alg->kind = TLS_CERT_SIG_RSA_PKCS1;
        alg->hash_id = RT_RSA_HASH_SHA384;
        return 1;
    }
    if (oid_len == sizeof(OID_SHA512_RSA) && memcmp(oid, OID_SHA512_RSA, sizeof(OID_SHA512_RSA)) == 0) {
        if (!der_params_absent_or_null(p, rem))
            return 0;
        alg->kind = TLS_CERT_SIG_RSA_PKCS1;
        alg->hash_id = RT_RSA_HASH_SHA512;
        return 1;
    }
    if (oid_len == sizeof(OID_ECDSA_SHA256) &&
        memcmp(oid, OID_ECDSA_SHA256, sizeof(OID_ECDSA_SHA256)) == 0) {
        if (rem != 0)
            return 0;
        alg->kind = TLS_CERT_SIG_ECDSA_P256;
        alg->hash_id = RT_RSA_HASH_SHA256;
        return 1;
    }
    if (oid_len == sizeof(OID_ECDSA_SHA384) &&
        memcmp(oid, OID_ECDSA_SHA384, sizeof(OID_ECDSA_SHA384)) == 0) {
        if (rem != 0)
            return 0;
        alg->kind = TLS_CERT_SIG_ECDSA_P256;
        alg->hash_id = RT_RSA_HASH_SHA384;
        return 1;
    }
    if (oid_len == sizeof(OID_ECDSA_SHA512) &&
        memcmp(oid, OID_ECDSA_SHA512, sizeof(OID_ECDSA_SHA512)) == 0) {
        if (rem != 0)
            return 0;
        alg->kind = TLS_CERT_SIG_ECDSA_P256;
        alg->hash_id = RT_RSA_HASH_SHA512;
        return 1;
    }
    if (oid_len == sizeof(OID_RSA_PSS) && memcmp(oid, OID_RSA_PSS, sizeof(OID_RSA_PSS)) == 0) {
        alg->kind = TLS_CERT_SIG_RSA_PSS;
        if (rem == 0)
            return 0;
        return cert_parse_pss_params(p, rem, alg);
    }
    return 0;
}

/// @brief Return the digest output length in bytes for a given hash identifier.
/// @return 32, 48, or 64; 0 for unknown IDs.
static RT_TLS_MAYBE_UNUSED size_t hash_len_from_id(rt_rsa_hash_t hash_id) {
    return hash_id == RT_RSA_HASH_SHA256 ? 32
         : hash_id == RT_RSA_HASH_SHA384 ? 48
         : hash_id == RT_RSA_HASH_SHA512 ? 64
                                         : 0;
}

/// @brief Hash data with the algorithm specified by hash_id and write the digest to out (max 64 bytes).
/// @return 1 on success, 0 for unknown hash_id.
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

/// @brief Verify the digital signature on cert_der using the public key from issuer_der.
///        Supports RSA-PKCS1, RSA-PSS, and ECDSA-P256 based on the signatureAlgorithm OID.
/// @return 1 if the signature is valid, 0 on error or verification failure.
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

/// @brief Read an entire file into a freshly allocated, null-terminated buffer (POSIX).
///        Caller must free() the returned pointer.
/// @return Pointer to buffer on success, NULL on any I/O or allocation error.
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

/// @brief Iterate through PEM certificates in a bundle.
///        On each call, advances *pos past the next "-----BEGIN CERTIFICATE-----" ... "-----END
///        CERTIFICATE-----" block and sets *body_out/*body_len_out to the Base64 body.
/// @return 1 if a certificate block was found, 0 at end-of-bundle or on parse error.
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

/// @brief Search a PEM bundle for a certificate whose DER encoding exactly matches cert_der.
/// @return 1 if a matching certificate is found, 0 otherwise.
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

/// @brief Check whether a PEM bundle contains a trusted CA that issued child_der.
///        Matches by Issuer/Subject equality, validates the CA cert's expiry, BasicConstraints,
///        and verifies the child certificate's signature with the CA's public key.
/// @return 1 if a valid trusted issuer is found, 0 otherwise.
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

/// @brief Validate the server certificate chain against a PEM CA bundle (native macOS/Linux).
///        Uses session->ca_file if set, otherwise probes standard OS bundle paths.
///        Checks EKU, expiry, BasicConstraints, and cryptographic signatures for each link.
/// @return RT_TLS_OK on success, RT_TLS_ERROR_HANDSHAKE on validation failure.
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
    if (!cert_allows_tls_server_auth(session->server_cert_der, session->server_cert_der_len)) {
        session->error = "TLS: certificate is not valid for TLS server authentication";
        return RT_TLS_ERROR_HANDSHAKE;
    }
    if (cert_has_unsupported_critical_extension(session->server_cert_der, session->server_cert_der_len)) {
        session->error = "TLS: leaf certificate has an unsupported critical extension";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    if (session->server_cert_list && session->server_cert_list_len > 0) {
        size_t cert_index = 0;
        while (list_pos < session->server_cert_list_len) {
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
            if (intermediate_count >= 16) {
                session->error = "TLS: certificate chain has too many intermediates";
                return RT_TLS_ERROR_HANDSHAKE;
            }
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
                if (cert_has_unsupported_critical_extension(intermediates[i].der, intermediates[i].len) ||
                    cert_check_expiry(intermediates[i].der, intermediates[i].len) != 0 ||
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

static size_t sig_scheme_hash_len(uint16_t sig_scheme);
#if defined(_WIN32)
static int ecdsa_der_integer_is_canonical(const uint8_t *bytes, size_t len) {
    if (!bytes || len == 0)
        return 0;
    if ((bytes[0] & 0x80u) != 0)
        return 0;
    if (len == 1)
        return bytes[0] != 0x00;
    if (bytes[0] == 0x00)
        return (bytes[1] & 0x80u) != 0;
    return 1;
}

/// @brief Parse a DER-encoded ECDSA signature for CNG, writing raw r || s bytes.
/// @return 0 on success, -1 on malformed input.
static int parse_ecdsa_sig_der(const uint8_t *sig,
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

    if (der_read_tlv(sig, sig_len, &tag, &vl, &hl) != 0 || tag != 0x30 ||
        hl + vl != sig_len)
        return -1;
    inner = sig + hl;
    inner_rem = vl;
    if (der_read_tlv(inner, inner_rem, &tag, &vl, &hl) != 0 || tag != 0x02)
        return -1;
    r_bytes = inner + hl;
    r_len = vl;
    if (!ecdsa_der_integer_is_canonical(r_bytes, r_len))
        return -1;
    inner += hl + vl;
    inner_rem -= hl + vl;
    if (der_read_tlv(inner, inner_rem, &tag, &vl, &hl) != 0 || tag != 0x02)
        return -1;
    s_bytes = inner + hl;
    s_len = vl;
    if (!ecdsa_der_integer_is_canonical(s_bytes, s_len))
        return -1;
    inner += hl + vl;
    inner_rem -= hl + vl;
    if (inner_rem != 0)
        return -1;

    if (r_len > 1 && r_bytes[0] == 0x00) {
        r_bytes++;
        r_len--;
    }
    if (s_len > 1 && s_bytes[0] == 0x00) {
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

/// @brief Build and hash the CertificateVerify content for Windows CryptoAPI verification.
///        Constructs the 130-byte signed content, then hashes it with SHA-256/384/512 as
///        required by sig_scheme.
/// @return 1 on success, 0 for unknown or unsupported sig_scheme.
static int build_cert_verify_hash_for_scheme_win(uint16_t sig_scheme,
                                                 const uint8_t transcript_hash[32],
                                                 uint8_t out[64],
                                                 size_t *hash_len_out) {
    uint8_t content[130];
    size_t hash_len = sig_scheme_hash_len(sig_scheme);
    if (hash_len_out)
        *hash_len_out = hash_len;
    if (hash_len == 0)
        return 0;
    build_cert_verify_message(transcript_hash, content);
    switch (hash_len) {
        case 32:
            rt_sha256(content, sizeof(content), out);
            return 1;
        case 48:
            rt_sha384(content, sizeof(content), out);
            return 1;
        case 64:
            rt_sha512(content, sizeof(content), out);
            return 1;
        default:
            return 0;
    }
}
#endif

/// @brief Determine the hash output size for a signature scheme.
/// @return 32 for SHA-256, 48 for SHA-384, 64 for SHA-512, 0 if unknown.
static size_t sig_scheme_hash_len(uint16_t sig_scheme) {
    switch (sig_scheme) {
        case 0x0403: /* ecdsa_secp256r1_sha256 */
        case 0x0804: /* rsa_pss_rsae_sha256 */
            return 32;
        case 0x0805: /* rsa_pss_rsae_sha384 */
            return 48;
        case 0x0806: /* rsa_pss_rsae_sha512 */
            return 64;
        default:
            return 0;
    }
}

#if defined(_WIN32)

/// @brief Windows path: verify CertificateVerify via Windows CNG / CryptoAPI.
///
/// Builds the message, hashes it locally, then verifies ECDSA and RSA-PSS with
/// CNG. The older CryptoAPI fallback is kept for RSA-style public key imports.
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
    size_t hash_len = 0;
    memset(content_hash, 0, sizeof(content_hash));
    if (!build_cert_verify_hash_for_scheme_win(sig_scheme, session->cert_transcript_hash, content_hash, &hash_len)) {
        session->error = "TLS: CertificateVerify: unsupported scheme (Windows)";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    PCCERT_CONTEXT cert_ctx = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                                           session->server_cert_der,
                                                           (DWORD)session->server_cert_der_len);

    if (!cert_ctx) {
        session->error = "TLS: CertVerify: could not parse certificate (Windows)";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    if (sig_scheme == 0x0403) {
        BCRYPT_KEY_HANDLE key = NULL;
        uint8_t sig_r[32];
        uint8_t sig_s[32];
        uint8_t raw_sig[64];
        NTSTATUS status;

        if (parse_ecdsa_sig_der(sig_bytes, sig_len, sig_r, sig_s) != 0) {
            CertFreeCertificateContext(cert_ctx);
            session->error = "TLS: CertificateVerify: malformed ECDSA signature (Windows)";
            return RT_TLS_ERROR_HANDSHAKE;
        }
        memcpy(raw_sig, sig_r, sizeof(sig_r));
        memcpy(raw_sig + sizeof(sig_r), sig_s, sizeof(sig_s));

        if (!CryptImportPublicKeyInfoEx2(
                X509_ASN_ENCODING, &cert_ctx->pCertInfo->SubjectPublicKeyInfo, 0, NULL, &key)) {
            CertFreeCertificateContext(cert_ctx);
            session->error = "TLS: CertVerify: CryptImportPublicKeyInfoEx2 failed";
            return RT_TLS_ERROR_HANDSHAKE;
        }

        status = BCryptVerifySignature(key,
                                       NULL,
                                       content_hash,
                                       (ULONG)hash_len,
                                       raw_sig,
                                       (ULONG)sizeof(raw_sig),
                                       0);
        BCryptDestroyKey(key);
        CertFreeCertificateContext(cert_ctx);

        if (!BCRYPT_SUCCESS(status)) {
            session->error = "TLS: CertificateVerify signature failed (Windows)";
            return RT_TLS_ERROR_HANDSHAKE;
        }

        return RT_TLS_OK;
    }

    if (sig_scheme == 0x0804 || sig_scheme == 0x0805 || sig_scheme == 0x0806) {
        BCRYPT_KEY_HANDLE key = NULL;
        LPCWSTR hash_alg = (sig_scheme == 0x0804)   ? BCRYPT_SHA256_ALGORITHM
                          : (sig_scheme == 0x0805) ? BCRYPT_SHA384_ALGORITHM
                                                   : BCRYPT_SHA512_ALGORITHM;
        BCRYPT_PSS_PADDING_INFO padding_info;
        NTSTATUS status;

        if (!CryptImportPublicKeyInfoEx2(
                X509_ASN_ENCODING, &cert_ctx->pCertInfo->SubjectPublicKeyInfo, 0, NULL, &key)) {
            CertFreeCertificateContext(cert_ctx);
            session->error = "TLS: CertVerify: CryptImportPublicKeyInfoEx2 failed";
            return RT_TLS_ERROR_HANDSHAKE;
        }

        padding_info.pszAlgId = hash_alg;
        padding_info.cbSalt = (ULONG)hash_len;
        status = BCryptVerifySignature(key,
                                       &padding_info,
                                       content_hash,
                                       (ULONG)hash_len,
                                       (PUCHAR)sig_bytes,
                                       (ULONG)sig_len,
                                       BCRYPT_PAD_PSS);
        BCryptDestroyKey(key);
        CertFreeCertificateContext(cert_ctx);

        if (!BCRYPT_SUCCESS(status)) {
            session->error = "TLS: CertificateVerify signature failed (Windows)";
            return RT_TLS_ERROR_HANDSHAKE;
        }

        return RT_TLS_OK;
    }

    // Determine hash algorithm OID from signature scheme
    LPCSTR hash_oid;
    switch (sig_scheme) {
        case 0x0403:
        case 0x0804:
            hash_oid = szOID_NIST_sha256;
            break;
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
        ALG_ID alg_id = (sig_scheme == 0x0403 || sig_scheme == 0x0804) ? CALG_SHA_256
                        : (sig_scheme == 0x0805)                       ? CALG_SHA_384
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

/// @brief Verify the TLS 1.3 CertificateVerify message using in-tree ECDSA-P256 or RSA (native).
///        Builds the 130-byte signed content, hashes it, and dispatches to ecdsa_p256_verify
///        or rt_rsa_pss_verify / rt_rsa_pkcs1_v15_verify based on the signature scheme.
/// @return RT_TLS_OK on success, RT_TLS_ERROR_HANDSHAKE on any failure.
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
