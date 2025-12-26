/**
 * @file x509.cpp
 * @brief X.509 certificate parser implementation.
 *
 * @details
 * Implements the minimal X.509 parsing routines declared in `x509.hpp` using
 * the DER parser from `asn1.hpp`. The goal is to extract enough information to
 * support TLS server certificate verification during bring-up.
 */

#include "x509.hpp"
#include "../../../lib/str.hpp"

namespace viper::x509
{

// Use lib::strlen and lib::strcmp for string operations

/**
 * @brief Exact string equality.
 *
 * @param a First string.
 * @param b Second string.
 * @return `true` if equal, otherwise `false`.
 */
static bool str_eq(const char *a, const char *b)
{
    return lib::strcmp(a, b) == 0;
}

// Case-insensitive string comparison
/**
 * @brief Case-insensitive ASCII string equality.
 *
 * @details
 * Used for hostname matching and issuer/subject comparisons in bring-up.
 *
 * @param a First string.
 * @param b Second string.
 * @return `true` if equal ignoring ASCII case, otherwise `false`.
 */
static bool str_eq_nocase(const char *a, const char *b)
{
    while (*a && *b)
    {
        char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
        if (ca != cb)
            return false;
        a++;
        b++;
    }
    return *a == *b;
}

// Parse signature algorithm OID
/**
 * @brief Map a signature algorithm OID element to an internal enum.
 *
 * @param elem OID element from an AlgorithmIdentifier.
 * @return Parsed signature algorithm enumeration value.
 */
static SignatureAlgorithm parse_sig_alg(const asn1::Element *elem)
{
    if (asn1::oid_equals(elem, asn1::oid::SHA256_RSA))
    {
        return SignatureAlgorithm::SHA256_RSA;
    }
    if (asn1::oid_equals(elem, asn1::oid::SHA384_RSA))
    {
        return SignatureAlgorithm::SHA384_RSA;
    }
    if (asn1::oid_equals(elem, asn1::oid::SHA256_ECDSA))
    {
        return SignatureAlgorithm::SHA256_ECDSA;
    }
    if (asn1::oid_equals(elem, asn1::oid::SHA384_ECDSA))
    {
        return SignatureAlgorithm::SHA384_ECDSA;
    }
    if (asn1::oid_equals(elem, asn1::oid::ED25519))
    {
        return SignatureAlgorithm::ED25519;
    }
    return SignatureAlgorithm::Unknown;
}

// Parse a Name (sequence of RDNs)
/**
 * @brief Parse a distinguished name and extract selected attributes.
 *
 * @details
 * Walks the RDNSequence and extracts Common Name (CN) and Organization (O)
 * values when present. Other attributes are ignored.
 *
 * @param p Parser positioned at the Name element.
 * @param cn_out Output CN string buffer.
 * @param org_out Output Organization string buffer.
 */
static void parse_name(asn1::Parser *p, char *cn_out, char *org_out)
{
    cn_out[0] = '\0';
    org_out[0] = '\0';

    asn1::Element name_elem;
    if (!asn1::parse_element(p, &name_elem))
        return;
    if (!name_elem.constructed)
        return;

    asn1::Parser name_parser = asn1::enter_constructed(&name_elem);

    // Iterate through RDNs (RelativeDistinguishedName)
    asn1::Element rdn;
    while (asn1::parse_element(&name_parser, &rdn))
    {
        if (!rdn.constructed)
            continue;

        asn1::Parser rdn_parser = asn1::enter_constructed(&rdn);

        // Each RDN is a SET of AttributeTypeAndValue
        asn1::Element atv;
        while (asn1::parse_element(&rdn_parser, &atv))
        {
            if (!atv.constructed)
                continue;

            asn1::Parser atv_parser = asn1::enter_constructed(&atv);

            // AttributeTypeAndValue ::= SEQUENCE { type, value }
            asn1::Element type_elem, value_elem;
            if (!asn1::parse_element(&atv_parser, &type_elem))
                continue;
            if (!asn1::parse_element(&atv_parser, &value_elem))
                continue;

            if (asn1::oid_equals(&type_elem, asn1::oid::COMMON_NAME))
            {
                asn1::parse_string(&value_elem, cn_out, MAX_NAME_LENGTH);
            }
            else if (asn1::oid_equals(&type_elem, asn1::oid::ORGANIZATION))
            {
                asn1::parse_string(&value_elem, org_out, MAX_NAME_LENGTH);
            }
        }
    }
}

// Parse time (UTCTime or GeneralizedTime)
/**
 * @brief Parse an ASN.1 UTCTime or GeneralizedTime value into components.
 *
 * @details
 * Expects a `...Z` style timestamp (UTC). Only the basic formats used by most
 * certificates are handled.
 *
 * @param elem Time element.
 * @param year Output year.
 * @param month Output month (1-12).
 * @param day Output day (1-31).
 * @param hour Output hour (0-23).
 * @param minute Output minute (0-59).
 * @param second Output second (0-59).
 * @return `true` on success, otherwise `false`.
 */
static bool parse_time(
    const asn1::Element *elem, u16 *year, u8 *month, u8 *day, u8 *hour, u8 *minute, u8 *second)
{
    const char *s = reinterpret_cast<const char *>(elem->data);
    usize len = elem->length;

    u8 tag = elem->tag & 0x1F;

    if (tag == asn1::UTCTime && len >= 12)
    {
        // YYMMDDhhmmssZ
        *year = 1900 + (s[0] - '0') * 10 + (s[1] - '0');
        if (*year < 1950)
            *year += 100; // 2000-2049
        *month = (s[2] - '0') * 10 + (s[3] - '0');
        *day = (s[4] - '0') * 10 + (s[5] - '0');
        *hour = (s[6] - '0') * 10 + (s[7] - '0');
        *minute = (s[8] - '0') * 10 + (s[9] - '0');
        *second = (s[10] - '0') * 10 + (s[11] - '0');
        return true;
    }

    if (tag == asn1::GeneralizedTime && len >= 14)
    {
        // YYYYMMDDhhmmssZ
        *year = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
        *month = (s[4] - '0') * 10 + (s[5] - '0');
        *day = (s[6] - '0') * 10 + (s[7] - '0');
        *hour = (s[8] - '0') * 10 + (s[9] - '0');
        *minute = (s[10] - '0') * 10 + (s[11] - '0');
        *second = (s[12] - '0') * 10 + (s[13] - '0');
        return true;
    }

    return false;
}

// Parse Validity
/**
 * @brief Parse the Validity sequence and fill a @ref Validity structure.
 *
 * @param p Parser positioned at the Validity element.
 * @param validity Output validity structure.
 */
static void parse_validity(asn1::Parser *p, Validity *validity)
{
    asn1::Element validity_seq;
    if (!asn1::parse_element(p, &validity_seq))
        return;
    if (!validity_seq.constructed)
        return;

    asn1::Parser val_parser = asn1::enter_constructed(&validity_seq);

    asn1::Element not_before, not_after;
    if (asn1::parse_element(&val_parser, &not_before))
    {
        parse_time(&not_before,
                   &validity->not_before_year,
                   &validity->not_before_month,
                   &validity->not_before_day,
                   &validity->not_before_hour,
                   &validity->not_before_minute,
                   &validity->not_before_second);
    }
    if (asn1::parse_element(&val_parser, &not_after))
    {
        parse_time(&not_after,
                   &validity->not_after_year,
                   &validity->not_after_month,
                   &validity->not_after_day,
                   &validity->not_after_hour,
                   &validity->not_after_minute,
                   &validity->not_after_second);
    }
}

// Parse SubjectPublicKeyInfo
/**
 * @brief Parse SubjectPublicKeyInfo and extract key material.
 *
 * @details
 * Extracts the public key type by OID (RSA or EC) and stores pointers to the
 * public key BIT STRING payload. For RSA keys, attempts to parse the inner
 * RSAPublicKey sequence to locate modulus and exponent.
 *
 * @param p Parser positioned at SubjectPublicKeyInfo.
 * @param cert Output certificate structure to update.
 */
static void parse_public_key(asn1::Parser *p, Certificate *cert)
{
    asn1::Element spki;
    if (!asn1::parse_element(p, &spki))
        return;
    if (!spki.constructed)
        return;

    asn1::Parser spki_parser = asn1::enter_constructed(&spki);

    // AlgorithmIdentifier
    asn1::Element alg_id;
    if (!asn1::parse_element(&spki_parser, &alg_id))
        return;
    if (!alg_id.constructed)
        return;

    asn1::Parser alg_parser = asn1::enter_constructed(&alg_id);
    asn1::Element alg_oid;
    if (asn1::parse_element(&alg_parser, &alg_oid))
    {
        char oid_str[128];
        asn1::parse_oid(&alg_oid, oid_str, sizeof(oid_str));

        if (str_eq(oid_str, "1.2.840.113549.1.1.1"))
        {
            cert->key_type = KeyType::RSA;
        }
        else if (str_eq(oid_str, "1.2.840.10045.2.1"))
        {
            // EC public key - check curve parameter
            asn1::Element curve_oid;
            if (asn1::parse_element(&alg_parser, &curve_oid))
            {
                if (asn1::oid_equals(&curve_oid, "1.2.840.10045.3.1.7"))
                {
                    cert->key_type = KeyType::ECDSA_P256;
                }
                else if (asn1::oid_equals(&curve_oid, "1.3.132.0.34"))
                {
                    cert->key_type = KeyType::ECDSA_P384;
                }
            }
        }
        else if (str_eq(oid_str, "1.3.101.112"))
        {
            cert->key_type = KeyType::ED25519;
        }
    }

    // SubjectPublicKey (BIT STRING)
    asn1::Element pub_key;
    if (asn1::parse_element(&spki_parser, &pub_key))
    {
        asn1::parse_bitstring(&pub_key, &cert->public_key, &cert->public_key_length);

        // For RSA, parse the modulus and exponent
        if (cert->key_type == KeyType::RSA && cert->public_key)
        {
            asn1::Parser rsa_parser;
            asn1::parser_init(&rsa_parser, cert->public_key, cert->public_key_length / 8);

            asn1::Element rsa_seq;
            if (asn1::parse_element(&rsa_parser, &rsa_seq) && rsa_seq.constructed)
            {
                asn1::Parser rsa_inner = asn1::enter_constructed(&rsa_seq);
                asn1::Element modulus, exponent;
                if (asn1::parse_element(&rsa_inner, &modulus))
                {
                    cert->rsa_modulus = modulus.data;
                    cert->rsa_modulus_length = modulus.length;
                }
                if (asn1::parse_element(&rsa_inner, &exponent))
                {
                    cert->rsa_exponent = exponent.data;
                    cert->rsa_exponent_length = exponent.length;
                }
            }
        }
    }
}

// Parse extensions
/**
 * @brief Parse selected certificate extensions (v3).
 *
 * @details
 * Currently recognizes:
 * - SubjectAltName (extracts dNSName entries).
 * - BasicConstraints (extracts CA flag and optional path length).
 *
 * Other extensions are ignored.
 *
 * @param p Parser positioned at the extensions wrapper (context-specific [3]).
 * @param cert Certificate to update.
 */
static void parse_extensions(asn1::Parser *p, Certificate *cert)
{
    asn1::Element ext_wrapper;
    if (!asn1::parse_element(p, &ext_wrapper))
        return;

    // Extensions are context-specific [3]
    if (ext_wrapper.tag_class != asn1::ContextSpecific)
        return;

    asn1::Parser wrapper_parser = asn1::enter_constructed(&ext_wrapper);
    asn1::Element extensions;
    if (!asn1::parse_element(&wrapper_parser, &extensions))
        return;
    if (!extensions.constructed)
        return;

    asn1::Parser ext_parser = asn1::enter_constructed(&extensions);

    asn1::Element ext;
    while (asn1::parse_element(&ext_parser, &ext))
    {
        if (!ext.constructed)
            continue;

        asn1::Parser single_ext = asn1::enter_constructed(&ext);

        asn1::Element oid_elem;
        if (!asn1::parse_element(&single_ext, &oid_elem))
            continue;

        // Check for critical flag (optional BOOLEAN)
        asn1::Element next;
        if (!asn1::parse_element(&single_ext, &next))
            continue;

        const asn1::Element *value_elem = &next;
        if ((next.tag & 0x1F) == asn1::Boolean)
        {
            // Skip critical flag, get actual value
            if (!asn1::parse_element(&single_ext, &next))
                continue;
            value_elem = &next;
        }

        // Parse extension value (OCTET STRING containing DER)
        if ((value_elem->tag & 0x1F) != asn1::OctetString)
            continue;

        // Subject Alternative Name
        if (asn1::oid_equals(&oid_elem, asn1::oid::SUBJECT_ALT_NAME))
        {
            asn1::Parser san_parser;
            asn1::parser_init(&san_parser, value_elem->data, value_elem->length);

            asn1::Element san_seq;
            if (asn1::parse_element(&san_parser, &san_seq) && san_seq.constructed)
            {
                asn1::Parser san_inner = asn1::enter_constructed(&san_seq);

                asn1::Element san_entry;
                while (cert->san_count < MAX_SAN_ENTRIES &&
                       asn1::parse_element(&san_inner, &san_entry))
                {
                    // Context-specific tags for SAN types
                    u8 san_type = san_entry.tag & 0x1F;
                    if (san_type == 2)
                    { // dNSName
                        cert->san[cert->san_count].type = SanEntry::DNS;
                        usize copy_len = san_entry.length;
                        if (copy_len > 127)
                            copy_len = 127;
                        for (usize i = 0; i < copy_len; i++)
                        {
                            cert->san[cert->san_count].value[i] = san_entry.data[i];
                        }
                        cert->san[cert->san_count].value[copy_len] = '\0';
                        cert->san_count++;
                    }
                }
            }
        }

        // Basic Constraints
        if (asn1::oid_equals(&oid_elem, asn1::oid::BASIC_CONSTRAINTS))
        {
            asn1::Parser bc_parser;
            asn1::parser_init(&bc_parser, value_elem->data, value_elem->length);

            asn1::Element bc_seq;
            if (asn1::parse_element(&bc_parser, &bc_seq) && bc_seq.constructed)
            {
                asn1::Parser bc_inner = asn1::enter_constructed(&bc_seq);

                asn1::Element bc_elem;
                while (asn1::parse_element(&bc_inner, &bc_elem))
                {
                    if ((bc_elem.tag & 0x1F) == asn1::Boolean)
                    {
                        cert->is_ca = bc_elem.data[0] != 0;
                    }
                    else if ((bc_elem.tag & 0x1F) == asn1::Integer)
                    {
                        i64 path_len;
                        if (asn1::parse_integer(&bc_elem, &path_len))
                        {
                            cert->path_length = static_cast<i32>(path_len);
                        }
                    }
                }
            }
        }
    }
}

/** @copydoc viper::x509::parse_certificate */
bool parse_certificate(const void *data, usize length, Certificate *cert)
{
    // Initialize certificate
    cert->version = 0;
    cert->serial_number = nullptr;
    cert->serial_number_length = 0;
    cert->signature_algorithm = SignatureAlgorithm::Unknown;
    cert->issuer_cn[0] = '\0';
    cert->issuer_org[0] = '\0';
    cert->subject_cn[0] = '\0';
    cert->subject_org[0] = '\0';
    cert->key_type = KeyType::Unknown;
    cert->public_key = nullptr;
    cert->public_key_length = 0;
    cert->rsa_modulus = nullptr;
    cert->rsa_modulus_length = 0;
    cert->rsa_exponent = nullptr;
    cert->rsa_exponent_length = 0;
    cert->san_count = 0;
    cert->is_ca = false;
    cert->path_length = -1;
    cert->key_usage = 0;
    cert->tbs_certificate = nullptr;
    cert->tbs_certificate_length = 0;
    cert->signature = nullptr;
    cert->signature_length = 0;
    cert->raw = static_cast<const u8 *>(data);
    cert->raw_length = length;

    asn1::Parser parser;
    asn1::parser_init(&parser, data, length);

    // Certificate ::= SEQUENCE { tbsCertificate, signatureAlgorithm, signature }
    asn1::Element cert_seq;
    if (!asn1::parse_element(&parser, &cert_seq))
        return false;
    if (!cert_seq.constructed)
        return false;

    asn1::Parser cert_parser = asn1::enter_constructed(&cert_seq);

    // TBSCertificate
    asn1::Element tbs;
    if (!asn1::parse_element(&cert_parser, &tbs))
        return false;
    cert->tbs_certificate = tbs.raw;
    cert->tbs_certificate_length = tbs.raw_length;

    asn1::Parser tbs_parser = asn1::enter_constructed(&tbs);

    // Version (optional, context [0])
    asn1::Element first;
    if (!asn1::parse_element(&tbs_parser, &first))
        return false;

    if (first.tag_class == asn1::ContextSpecific && (first.tag & 0x1F) == 0)
    {
        // Version present
        asn1::Parser ver_parser = asn1::enter_constructed(&first);
        asn1::Element ver_int;
        if (asn1::parse_element(&ver_parser, &ver_int))
        {
            i64 ver;
            if (asn1::parse_integer(&ver_int, &ver))
            {
                cert->version = static_cast<i32>(ver);
            }
        }
        // Read serial number
        if (!asn1::parse_element(&tbs_parser, &first))
            return false;
    }

    // Serial number (now in 'first')
    cert->serial_number = first.data;
    cert->serial_number_length = first.length;

    // Signature algorithm
    asn1::Element sig_alg;
    if (!asn1::parse_element(&tbs_parser, &sig_alg))
        return false;
    if (sig_alg.constructed)
    {
        asn1::Parser alg_parser = asn1::enter_constructed(&sig_alg);
        asn1::Element alg_oid;
        if (asn1::parse_element(&alg_parser, &alg_oid))
        {
            cert->signature_algorithm = parse_sig_alg(&alg_oid);
        }
    }

    // Issuer
    parse_name(&tbs_parser, cert->issuer_cn, cert->issuer_org);

    // Validity
    parse_validity(&tbs_parser, &cert->validity);

    // Subject
    parse_name(&tbs_parser, cert->subject_cn, cert->subject_org);

    // Subject Public Key Info
    parse_public_key(&tbs_parser, cert);

    // Extensions (if v3)
    if (cert->version >= 2)
    {
        parse_extensions(&tbs_parser, cert);
    }

    // Signature Algorithm (outer, should match inner)
    asn1::Element outer_sig_alg;
    if (!asn1::parse_element(&cert_parser, &outer_sig_alg))
        return false;

    // Signature Value (BIT STRING)
    asn1::Element sig_value;
    if (!asn1::parse_element(&cert_parser, &sig_value))
        return false;
    asn1::parse_bitstring(&sig_value, &cert->signature, &cert->signature_length);

    return true;
}

/** @copydoc viper::x509::matches_hostname */
bool matches_hostname(const Certificate *cert, const char *hostname)
{
    // Check Subject Alternative Names first
    for (usize i = 0; i < cert->san_count; i++)
    {
        if (cert->san[i].type == SanEntry::DNS)
        {
            const char *pattern = cert->san[i].value;

            // Handle wildcard
            if (pattern[0] == '*' && pattern[1] == '.')
            {
                // Wildcard: *.example.com matches foo.example.com
                const char *suffix = pattern + 1; // .example.com
                usize suffix_len = lib::strlen(suffix);

                // Find first dot in hostname
                const char *first_dot = hostname;
                while (*first_dot && *first_dot != '.')
                    first_dot++;

                if (*first_dot == '.')
                {
                    // Compare suffix
                    usize remaining = lib::strlen(first_dot);
                    if (remaining == suffix_len && str_eq_nocase(first_dot, suffix))
                    {
                        return true;
                    }
                }
            }
            else
            {
                // Exact match
                if (str_eq_nocase(pattern, hostname))
                {
                    return true;
                }
            }
        }
    }

    // Fall back to Common Name
    if (cert->subject_cn[0])
    {
        if (str_eq_nocase(cert->subject_cn, hostname))
        {
            return true;
        }
    }

    return false;
}

/** @copydoc viper::x509::is_time_valid */
bool is_time_valid(const Certificate *cert)
{
    // Certificate time validation requires a real-time clock source.
    // ViperOS currently lacks RTC or NTP, so we cannot check validity periods.
    //
    // When a time source is available, this should compare:
    // - cert->validity.not_before_* against current time (cert not yet valid)
    // - cert->validity.not_after_* against current time (cert expired)
    //
    // For now, return true to allow connections. The security impact is that
    // expired or not-yet-valid certificates will be accepted.
    (void)cert;
    return true;
}

/** @copydoc viper::x509::is_issued_by */
bool is_issued_by(const Certificate *cert, const Certificate *issuer)
{
    // Simple check: cert's issuer CN matches issuer's subject CN
    return str_eq(cert->issuer_cn, issuer->subject_cn);
}

/** @copydoc viper::x509::verify_signature */
bool verify_signature(const Certificate *cert, const Certificate *issuer)
{
    // NOTE: This function is deprecated in favor of cert::verify_certificate_signature()
    // in verify.cpp, which provides full RSA PKCS#1 v1.5 signature verification.
    // This stub exists for API compatibility but should not be relied upon.
    //
    // For actual signature verification, use:
    //   viper::tls::cert::verify_certificate_signature(cert, issuer)
    (void)cert;
    (void)issuer;
    return true;
}

} // namespace viper::x509
