/**
 * @file asn1.cpp
 * @brief ASN.1 DER parser implementation.
 *
 * @details
 * Implements the minimal DER parsing utilities declared in `asn1.hpp`. The
 * parser is designed to be small and freestanding while supporting the subset
 * of ASN.1 used in X.509 certificate parsing.
 */

#include "asn1.hpp"

namespace viper::asn1
{

/** @copydoc viper::asn1::parser_init */
void parser_init(Parser *p, const void *data, usize length)
{
    p->data = static_cast<const u8 *>(data);
    p->length = length;
    p->offset = 0;
}

/**
 * @brief Parse a DER length field.
 *
 * @details
 * Supports:
 * - Short form: single byte length < 128.
 * - Long form: up to 4 bytes of length.
 *
 * Indefinite-length encoding (`0x80`) is rejected because it is not permitted
 * in DER.
 *
 * @param data Pointer to the length field bytes.
 * @param available Number of bytes available in the buffer.
 * @param length Output decoded content length.
 * @param header_size Output number of bytes consumed by the length field.
 * @return `true` on success, otherwise `false`.
 */
static bool parse_length(const u8 *data, usize available, usize *length, usize *header_size)
{
    if (available < 1)
        return false;

    u8 first = data[0];

    if (first < 0x80)
    {
        // Short form: single byte length
        *length = first;
        *header_size = 1;
        return true;
    }

    if (first == 0x80)
    {
        // Indefinite length - not allowed in DER
        return false;
    }

    // Long form: first byte indicates number of length bytes
    usize num_bytes = first & 0x7F;
    if (num_bytes > 4 || num_bytes > available - 1)
    {
        return false; // Length too large or not enough data
    }

    *length = 0;
    for (usize i = 0; i < num_bytes; i++)
    {
        *length = (*length << 8) | data[1 + i];
    }

    *header_size = 1 + num_bytes;
    return true;
}

/** @copydoc viper::asn1::parse_element */
bool parse_element(Parser *p, Element *elem)
{
    if (p->offset >= p->length)
    {
        return false; // End of data
    }

    const u8 *start = p->data + p->offset;
    usize remaining = p->length - p->offset;

    if (remaining < 2)
    {
        return false; // Not enough for tag + length
    }

    // Parse tag
    u8 tag_byte = start[0];
    elem->tag = tag_byte & 0x1F;
    elem->tag_class = static_cast<TagClass>(tag_byte & 0xC0);
    elem->constructed = (tag_byte & Constructed) != 0;

    // Handle high-tag-number form (tag >= 31)
    usize tag_size = 1;
    if (elem->tag == 0x1F)
    {
        // Multi-byte tag - not commonly used, skip for now
        return false;
    }

    // Store full tag byte for matching
    elem->tag = tag_byte & 0x3F; // Tag number + constructed bit

    // Parse length
    usize content_length;
    usize length_size;
    if (!parse_length(start + tag_size, remaining - tag_size, &content_length, &length_size))
    {
        return false;
    }

    usize header_total = tag_size + length_size;

    if (header_total + content_length > remaining)
    {
        return false; // Content extends beyond available data
    }

    elem->data = start + header_total;
    elem->length = content_length;
    elem->raw = start;
    elem->raw_length = header_total + content_length;

    p->offset += elem->raw_length;
    return true;
}

/** @copydoc viper::asn1::peek_element */
bool peek_element(Parser *p, usize offset, Element *elem)
{
    Parser temp = *p;
    temp.offset = offset;
    return parse_element(&temp, elem);
}

/** @copydoc viper::asn1::enter_constructed */
Parser enter_constructed(const Element *elem)
{
    Parser p;
    parser_init(&p, elem->data, elem->length);
    return p;
}

/** @copydoc viper::asn1::skip_element */
bool skip_element(Parser *p)
{
    Element elem;
    return parse_element(p, &elem);
}

/** @copydoc viper::asn1::parse_integer */
bool parse_integer(const Element *elem, i64 *value)
{
    if ((elem->tag & 0x1F) != Integer)
    {
        return false;
    }

    if (elem->length == 0 || elem->length > 8)
    {
        return false; // Empty or too large
    }

    // Check if negative (high bit set)
    bool negative = (elem->data[0] & 0x80) != 0;

    *value = negative ? -1 : 0; // Sign extend

    for (usize i = 0; i < elem->length; i++)
    {
        *value = (*value << 8) | elem->data[i];
    }

    return true;
}

/** @copydoc viper::asn1::parse_oid */
bool parse_oid(const Element *elem, char *buffer, usize buffer_size)
{
    if ((elem->tag & 0x1F) != ObjectIdentifier)
    {
        return false;
    }

    if (elem->length == 0 || buffer_size < 32)
    {
        return false;
    }

    const u8 *data = elem->data;
    usize len = elem->length;
    usize pos = 0;

    // First byte encodes first two components
    u32 first = data[0] / 40;
    u32 second = data[0] % 40;

    // Write first.second
    int written = 0;
    if (first < 10)
    {
        buffer[written++] = '0' + first;
    }
    else
    {
        buffer[written++] = '0' + (first / 10);
        buffer[written++] = '0' + (first % 10);
    }
    buffer[written++] = '.';

    // Write second component
    u32 temp = second;
    char temp_buf[16];
    int temp_len = 0;
    do
    {
        temp_buf[temp_len++] = '0' + (temp % 10);
        temp /= 10;
    } while (temp > 0);
    for (int i = temp_len - 1; i >= 0; i--)
    {
        buffer[written++] = temp_buf[i];
    }

    // Parse remaining components
    pos = 1;
    while (pos < len)
    {
        buffer[written++] = '.';

        // Parse variable-length integer
        u32 component = 0;
        while (pos < len)
        {
            u8 byte = data[pos++];
            component = (component << 7) | (byte & 0x7F);
            if ((byte & 0x80) == 0)
                break;
        }

        // Write component
        temp = component;
        temp_len = 0;
        do
        {
            temp_buf[temp_len++] = '0' + (temp % 10);
            temp /= 10;
        } while (temp > 0);
        for (int i = temp_len - 1; i >= 0; i--)
        {
            if (static_cast<usize>(written) >= buffer_size - 1)
            {
                buffer[written] = '\0';
                return false;
            }
            buffer[written++] = temp_buf[i];
        }
    }

    buffer[written] = '\0';
    return true;
}

/** @copydoc viper::asn1::parse_string */
bool parse_string(const Element *elem, char *buffer, usize buffer_size)
{
    u8 tag = elem->tag & 0x1F;
    if (tag != PrintableString && tag != UTF8String && tag != IA5String)
    {
        return false;
    }

    usize copy_len = elem->length;
    if (copy_len >= buffer_size)
    {
        copy_len = buffer_size - 1;
    }

    for (usize i = 0; i < copy_len; i++)
    {
        buffer[i] = static_cast<char>(elem->data[i]);
    }
    buffer[copy_len] = '\0';

    return true;
}

/** @copydoc viper::asn1::parse_bitstring */
bool parse_bitstring(const Element *elem, const u8 **bits, usize *bit_count)
{
    if ((elem->tag & 0x1F) != BitString)
    {
        return false;
    }

    if (elem->length < 1)
    {
        return false;
    }

    // First byte is number of unused bits in last byte
    u8 unused = elem->data[0];
    if (unused > 7)
    {
        return false;
    }

    *bits = elem->data + 1;
    *bit_count = (elem->length - 1) * 8 - unused;
    return true;
}

// Compare two strings
/**
 * @brief Exact string equality.
 *
 * @param a First string.
 * @param b Second string.
 * @return `true` if equal, otherwise `false`.
 */
static bool str_eq(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (*a != *b)
            return false;
        a++;
        b++;
    }
    return *a == *b;
}

/** @copydoc viper::asn1::oid_equals */
bool oid_equals(const Element *elem, const char *oid_str)
{
    char parsed[128];
    if (!parse_oid(elem, parsed, sizeof(parsed)))
    {
        return false;
    }
    return str_eq(parsed, oid_str);
}

} // namespace viper::asn1
