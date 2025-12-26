#pragma once

/**
 * @file asn1.hpp
 * @brief Minimal ASN.1 DER parser used by the TLS/X.509 implementation.
 *
 * @details
 * X.509 certificates are encoded using ASN.1 DER. This module implements a
 * small subset of DER parsing sufficient for the certificate parser and chain
 * verifier in v0.2.0:
 * - Tag/length/value parsing with definite-length encoding (DER).
 * - Helper routines for INTEGER, OID, strings and BIT STRING extraction.
 * - Convenience helpers for entering constructed values (SEQUENCE/SET).
 *
 * Limitations:
 * - Indefinite-length encoding is rejected (not allowed in DER).
 * - High-tag-number form (tag >= 31) is not supported in the current parser.
 */

#include "../../../include/types.hpp"

namespace viper::asn1
{

/**
 * @brief ASN.1 tag class bits.
 *
 * @details
 * These values correspond to the class bits in the identifier octet.
 */
enum TagClass : u8
{
    Universal = 0x00,
    Application = 0x40,
    ContextSpecific = 0x80,
    Private = 0xC0,
};

/**
 * @brief Common ASN.1 universal tag numbers.
 *
 * @details
 * These are the low 5 bits (tag number) for universal class elements.
 */
enum Tag : u8
{
    Boolean = 0x01,
    Integer = 0x02,
    BitString = 0x03,
    OctetString = 0x04,
    Null = 0x05,
    ObjectIdentifier = 0x06,
    UTF8String = 0x0C,
    Sequence = 0x10,
    Set = 0x11,
    PrintableString = 0x13,
    IA5String = 0x16,
    UTCTime = 0x17,
    GeneralizedTime = 0x18,
};

/** @brief Constructed bit in the identifier octet. */
constexpr u8 Constructed = 0x20;

/**
 * @brief Parsed ASN.1 element view.
 *
 * @details
 * Represents a single TLV (tag-length-value) element within a DER buffer.
 * Pointers refer into the original source buffer; no memory is owned by the
 * element.
 *
 * Note: `tag` stores the identifier octet with class bits masked out
 * (`tag_byte & 0x3F`). The tag number can be obtained with `tag & 0x1F`.
 */
struct Element
{
    u8 tag;             // Tag byte
    TagClass tag_class; // Tag class
    bool constructed;   // Is constructed (sequence/set)
    const u8 *data;     // Pointer to content
    usize length;       // Content length
    const u8 *raw;      // Pointer to full element (including header)
    usize raw_length;   // Full element length
};

/**
 * @brief Incremental parser state for walking DER elements.
 *
 * @details
 * The parser is a simple cursor over a byte buffer. Calls to
 * @ref parse_element advance the cursor by the length of the parsed element.
 */
struct Parser
{
    const u8 *data;
    usize length;
    usize offset;
};

/**
 * @brief Initialize a parser over a DER-encoded buffer.
 *
 * @param p Parser to initialize.
 * @param data Pointer to DER bytes.
 * @param length Length of the buffer in bytes.
 */
void parser_init(Parser *p, const void *data, usize length);

/**
 * @brief Parse the next element and advance the parser cursor.
 *
 * @details
 * Parses a tag and DER length field, validates bounds, and fills `elem` with
 * pointers to the element's content and raw bytes. The parser cursor is advanced
 * to the next element.
 *
 * @param p Parser state.
 * @param elem Output element structure.
 * @return `true` if an element was parsed, `false` on error or end-of-buffer.
 */
bool parse_element(Parser *p, Element *elem);

/**
 * @brief Parse an element at a specific offset without advancing the parser.
 *
 * @details
 * Creates a temporary parser at `offset` and parses a single element. The
 * original parser cursor is not modified.
 *
 * @param p Parser state (base buffer).
 * @param offset Byte offset into the buffer.
 * @param elem Output element.
 * @return `true` on success, otherwise `false`.
 */
bool peek_element(Parser *p, usize offset, Element *elem);

/**
 * @brief Create a parser over the contents of a constructed element.
 *
 * @details
 * Used to iterate the children of a SEQUENCE/SET by treating the element's
 * content bytes as a new buffer.
 *
 * @param elem Constructed element.
 * @return A new Parser initialized to walk the element's content bytes.
 */
Parser enter_constructed(const Element *elem);

/**
 * @brief Skip the next element in the parser.
 *
 * @details
 * Equivalent to parsing an element and discarding it.
 *
 * @param p Parser state.
 * @return `true` if an element was skipped, otherwise `false`.
 */
bool skip_element(Parser *p);

/**
 * @brief Parse an INTEGER value into a signed 64-bit integer.
 *
 * @details
 * Supports INTEGER values up to 8 bytes. Negative integers are sign-extended.
 *
 * @param elem Element expected to be an INTEGER.
 * @param value Output value.
 * @return `true` on success, otherwise `false`.
 */
bool parse_integer(const Element *elem, i64 *value);

/**
 * @brief Parse an OBJECT IDENTIFIER into dotted-decimal form.
 *
 * @details
 * Converts the binary OID encoding into a human-readable string like
 * `"1.2.840.113549.1.1.11"`. The string is written into `buffer` and
 * NUL-terminated.
 *
 * @param elem Element expected to be an OBJECT IDENTIFIER.
 * @param buffer Output string buffer.
 * @param buffer_size Capacity of `buffer`.
 * @return `true` on success, otherwise `false`.
 */
bool parse_oid(const Element *elem, char *buffer, usize buffer_size);

/**
 * @brief Parse a DER string into a C string buffer.
 *
 * @details
 * Supports PrintableString, UTF8String and IA5String. Bytes are copied directly
 * into `buffer` and NUL-terminated; no UTF-8 validation is performed.
 *
 * @param elem String element.
 * @param buffer Output buffer.
 * @param buffer_size Output buffer capacity.
 * @return `true` on success, otherwise `false`.
 */
bool parse_string(const Element *elem, char *buffer, usize buffer_size);

/**
 * @brief Parse a BIT STRING element.
 *
 * @details
 * Returns a pointer to the bit string payload bytes (excluding the "unused bits"
 * count byte) and the total number of meaningful bits.
 *
 * @param elem Element expected to be a BIT STRING.
 * @param bits Output pointer to the bit string bytes.
 * @param bit_count Output bit count.
 * @return `true` on success, otherwise `false`.
 */
bool parse_bitstring(const Element *elem, const u8 **bits, usize *bit_count);

/**
 * @brief Compare an OID element to a known dotted-decimal string.
 *
 * @details
 * Parses the OID element and performs a string comparison with `oid_str`.
 *
 * @param elem OID element.
 * @param oid_str Dotted-decimal OID string.
 * @return `true` if equal, otherwise `false`.
 */
bool oid_equals(const Element *elem, const char *oid_str);

/**
 * @brief Common OID strings used by certificate parsing/verification.
 *
 * @details
 * Strings are in dotted-decimal form for easy comparison/debug printing.
 */
namespace oid
{
// Signature algorithms
constexpr const char *SHA256_RSA = "1.2.840.113549.1.1.11";
constexpr const char *SHA384_RSA = "1.2.840.113549.1.1.12";
constexpr const char *SHA256_ECDSA = "1.2.840.10045.4.3.2";
constexpr const char *SHA384_ECDSA = "1.2.840.10045.4.3.3";
constexpr const char *ED25519 = "1.3.101.112";

// X.509 extensions
constexpr const char *SUBJECT_ALT_NAME = "2.5.29.17";
constexpr const char *BASIC_CONSTRAINTS = "2.5.29.19";
constexpr const char *KEY_USAGE = "2.5.29.15";

// X.500 attribute types
constexpr const char *COMMON_NAME = "2.5.4.3";
constexpr const char *COUNTRY = "2.5.4.6";
constexpr const char *ORGANIZATION = "2.5.4.10";
constexpr const char *ORG_UNIT = "2.5.4.11";
} // namespace oid

} // namespace viper::asn1
