//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/iconv.c
// Purpose: Character set conversion functions for ViperDOS libc.
// Key invariants: Converts via Unicode codepoints; supports UTF-8/16/32, ASCII.
// Ownership/Lifetime: Library; conversion descriptor dynamically allocated.
// Links: user/libc/include/iconv.h
//
//===----------------------------------------------------------------------===//

/**
 * @file iconv.c
 * @brief Character set conversion functions for ViperDOS libc.
 *
 * @details
 * This file implements POSIX character set conversion:
 *
 * - iconv_open: Open a conversion descriptor
 * - iconv: Perform character set conversion
 * - iconv_close: Close a conversion descriptor
 *
 * Supported encodings:
 * - ASCII, US-ASCII
 * - UTF-8
 * - ISO-8859-1, LATIN-1
 * - UTF-16BE, UTF-16LE, UTF-16
 * - UTF-32BE, UTF-32LE, UTF-32
 *
 * Conversion works by decoding source characters to Unicode codepoints,
 * then encoding them to the target character set. Invalid sequences
 * set errno to EILSEQ; incomplete sequences set EINVAL.
 */

#include "../include/iconv.h"
#include "../include/errno.h"
#include "../include/stdlib.h"
#include "../include/string.h"

/* Encoding identifiers */
enum encoding
{
    ENC_UNKNOWN = 0,
    ENC_ASCII,
    ENC_UTF8,
    ENC_ISO8859_1,
    ENC_UTF16BE,
    ENC_UTF16LE,
    ENC_UTF32BE,
    ENC_UTF32LE
};

/* Conversion descriptor structure */
struct iconv_desc
{
    enum encoding from;
    enum encoding to;
};

/*
 * Helper: Parse encoding name
 */
static enum encoding parse_encoding(const char *name)
{
    if (!name)
        return ENC_UNKNOWN;

    /* Normalize comparison - case insensitive */
    if (strcasecmp(name, "UTF-8") == 0 || strcasecmp(name, "UTF8") == 0)
        return ENC_UTF8;
    if (strcasecmp(name, "ASCII") == 0 || strcasecmp(name, "US-ASCII") == 0)
        return ENC_ASCII;
    if (strcasecmp(name, "ISO-8859-1") == 0 || strcasecmp(name, "ISO8859-1") == 0 ||
        strcasecmp(name, "LATIN1") == 0 || strcasecmp(name, "LATIN-1") == 0)
        return ENC_ISO8859_1;
    if (strcasecmp(name, "UTF-16BE") == 0 || strcasecmp(name, "UTF16BE") == 0)
        return ENC_UTF16BE;
    if (strcasecmp(name, "UTF-16LE") == 0 || strcasecmp(name, "UTF16LE") == 0)
        return ENC_UTF16LE;
    if (strcasecmp(name, "UTF-16") == 0 || strcasecmp(name, "UTF16") == 0)
        return ENC_UTF16BE; /* Default to BE */
    if (strcasecmp(name, "UTF-32BE") == 0 || strcasecmp(name, "UTF32BE") == 0)
        return ENC_UTF32BE;
    if (strcasecmp(name, "UTF-32LE") == 0 || strcasecmp(name, "UTF32LE") == 0)
        return ENC_UTF32LE;
    if (strcasecmp(name, "UTF-32") == 0 || strcasecmp(name, "UTF32") == 0)
        return ENC_UTF32BE; /* Default to BE */

    return ENC_UNKNOWN;
}

/*
 * Helper: Decode one codepoint from input
 * Returns codepoint value, or -1 on error
 * Updates *src and *srclen
 */
static int decode_char(enum encoding enc, const char **src, size_t *srclen)
{
    const unsigned char *p = (const unsigned char *)*src;

    switch (enc)
    {
        case ENC_ASCII:
            if (*srclen < 1)
                return -1;
            if (*p > 127)
            {
                errno = EILSEQ;
                return -1;
            }
            (*src)++;
            (*srclen)--;
            return *p;

        case ENC_UTF8:
            if (*srclen < 1)
                return -1;
            if (*p < 0x80)
            {
                (*src)++;
                (*srclen)--;
                return *p;
            }
            else if ((*p & 0xE0) == 0xC0)
            {
                if (*srclen < 2)
                {
                    errno = EINVAL;
                    return -1;
                }
                if ((p[1] & 0xC0) != 0x80)
                {
                    errno = EILSEQ;
                    return -1;
                }
                int cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
                *src += 2;
                *srclen -= 2;
                return cp;
            }
            else if ((*p & 0xF0) == 0xE0)
            {
                if (*srclen < 3)
                {
                    errno = EINVAL;
                    return -1;
                }
                if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80)
                {
                    errno = EILSEQ;
                    return -1;
                }
                int cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
                *src += 3;
                *srclen -= 3;
                return cp;
            }
            else if ((*p & 0xF8) == 0xF0)
            {
                if (*srclen < 4)
                {
                    errno = EINVAL;
                    return -1;
                }
                if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80)
                {
                    errno = EILSEQ;
                    return -1;
                }
                int cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) |
                         (p[3] & 0x3F);
                *src += 4;
                *srclen -= 4;
                return cp;
            }
            errno = EILSEQ;
            return -1;

        case ENC_ISO8859_1:
            if (*srclen < 1)
                return -1;
            (*src)++;
            (*srclen)--;
            return *p; /* ISO-8859-1 maps directly to Unicode */

        case ENC_UTF16BE:
            if (*srclen < 2)
            {
                errno = EINVAL;
                return -1;
            }
            {
                int cp = (p[0] << 8) | p[1];
                if (cp >= 0xD800 && cp <= 0xDBFF)
                {
                    /* Surrogate pair */
                    if (*srclen < 4)
                    {
                        errno = EINVAL;
                        return -1;
                    }
                    int low = (p[2] << 8) | p[3];
                    if (low < 0xDC00 || low > 0xDFFF)
                    {
                        errno = EILSEQ;
                        return -1;
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    *src += 4;
                    *srclen -= 4;
                }
                else
                {
                    *src += 2;
                    *srclen -= 2;
                }
                return cp;
            }

        case ENC_UTF16LE:
            if (*srclen < 2)
            {
                errno = EINVAL;
                return -1;
            }
            {
                int cp = p[0] | (p[1] << 8);
                if (cp >= 0xD800 && cp <= 0xDBFF)
                {
                    if (*srclen < 4)
                    {
                        errno = EINVAL;
                        return -1;
                    }
                    int low = p[2] | (p[3] << 8);
                    if (low < 0xDC00 || low > 0xDFFF)
                    {
                        errno = EILSEQ;
                        return -1;
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    *src += 4;
                    *srclen -= 4;
                }
                else
                {
                    *src += 2;
                    *srclen -= 2;
                }
                return cp;
            }

        case ENC_UTF32BE:
            if (*srclen < 4)
            {
                errno = EINVAL;
                return -1;
            }
            {
                int cp = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
                *src += 4;
                *srclen -= 4;
                return cp;
            }

        case ENC_UTF32LE:
            if (*srclen < 4)
            {
                errno = EINVAL;
                return -1;
            }
            {
                int cp = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
                *src += 4;
                *srclen -= 4;
                return cp;
            }

        default:
            errno = EINVAL;
            return -1;
    }
}

/*
 * Helper: Encode one codepoint to output
 * Returns bytes written, or -1 on error
 */
static int encode_char(enum encoding enc, int codepoint, char **dst, size_t *dstlen)
{
    unsigned char *p = (unsigned char *)*dst;

    switch (enc)
    {
        case ENC_ASCII:
            if (codepoint > 127)
            {
                errno = EILSEQ;
                return -1;
            }
            if (*dstlen < 1)
            {
                errno = E2BIG;
                return -1;
            }
            *p = (unsigned char)codepoint;
            (*dst)++;
            (*dstlen)--;
            return 1;

        case ENC_UTF8:
            if (codepoint < 0x80)
            {
                if (*dstlen < 1)
                {
                    errno = E2BIG;
                    return -1;
                }
                *p = (unsigned char)codepoint;
                (*dst)++;
                (*dstlen)--;
                return 1;
            }
            else if (codepoint < 0x800)
            {
                if (*dstlen < 2)
                {
                    errno = E2BIG;
                    return -1;
                }
                p[0] = 0xC0 | (codepoint >> 6);
                p[1] = 0x80 | (codepoint & 0x3F);
                *dst += 2;
                *dstlen -= 2;
                return 2;
            }
            else if (codepoint < 0x10000)
            {
                if (*dstlen < 3)
                {
                    errno = E2BIG;
                    return -1;
                }
                p[0] = 0xE0 | (codepoint >> 12);
                p[1] = 0x80 | ((codepoint >> 6) & 0x3F);
                p[2] = 0x80 | (codepoint & 0x3F);
                *dst += 3;
                *dstlen -= 3;
                return 3;
            }
            else if (codepoint < 0x110000)
            {
                if (*dstlen < 4)
                {
                    errno = E2BIG;
                    return -1;
                }
                p[0] = 0xF0 | (codepoint >> 18);
                p[1] = 0x80 | ((codepoint >> 12) & 0x3F);
                p[2] = 0x80 | ((codepoint >> 6) & 0x3F);
                p[3] = 0x80 | (codepoint & 0x3F);
                *dst += 4;
                *dstlen -= 4;
                return 4;
            }
            errno = EILSEQ;
            return -1;

        case ENC_ISO8859_1:
            if (codepoint > 255)
            {
                errno = EILSEQ;
                return -1;
            }
            if (*dstlen < 1)
            {
                errno = E2BIG;
                return -1;
            }
            *p = (unsigned char)codepoint;
            (*dst)++;
            (*dstlen)--;
            return 1;

        case ENC_UTF16BE:
            if (codepoint < 0x10000)
            {
                if (*dstlen < 2)
                {
                    errno = E2BIG;
                    return -1;
                }
                p[0] = (codepoint >> 8) & 0xFF;
                p[1] = codepoint & 0xFF;
                *dst += 2;
                *dstlen -= 2;
                return 2;
            }
            else
            {
                if (*dstlen < 4)
                {
                    errno = E2BIG;
                    return -1;
                }
                codepoint -= 0x10000;
                int high = 0xD800 + (codepoint >> 10);
                int low = 0xDC00 + (codepoint & 0x3FF);
                p[0] = (high >> 8) & 0xFF;
                p[1] = high & 0xFF;
                p[2] = (low >> 8) & 0xFF;
                p[3] = low & 0xFF;
                *dst += 4;
                *dstlen -= 4;
                return 4;
            }

        case ENC_UTF16LE:
            if (codepoint < 0x10000)
            {
                if (*dstlen < 2)
                {
                    errno = E2BIG;
                    return -1;
                }
                p[0] = codepoint & 0xFF;
                p[1] = (codepoint >> 8) & 0xFF;
                *dst += 2;
                *dstlen -= 2;
                return 2;
            }
            else
            {
                if (*dstlen < 4)
                {
                    errno = E2BIG;
                    return -1;
                }
                codepoint -= 0x10000;
                int high = 0xD800 + (codepoint >> 10);
                int low = 0xDC00 + (codepoint & 0x3FF);
                p[0] = high & 0xFF;
                p[1] = (high >> 8) & 0xFF;
                p[2] = low & 0xFF;
                p[3] = (low >> 8) & 0xFF;
                *dst += 4;
                *dstlen -= 4;
                return 4;
            }

        case ENC_UTF32BE:
            if (*dstlen < 4)
            {
                errno = E2BIG;
                return -1;
            }
            p[0] = (codepoint >> 24) & 0xFF;
            p[1] = (codepoint >> 16) & 0xFF;
            p[2] = (codepoint >> 8) & 0xFF;
            p[3] = codepoint & 0xFF;
            *dst += 4;
            *dstlen -= 4;
            return 4;

        case ENC_UTF32LE:
            if (*dstlen < 4)
            {
                errno = E2BIG;
                return -1;
            }
            p[0] = codepoint & 0xFF;
            p[1] = (codepoint >> 8) & 0xFF;
            p[2] = (codepoint >> 16) & 0xFF;
            p[3] = (codepoint >> 24) & 0xFF;
            *dst += 4;
            *dstlen -= 4;
            return 4;

        default:
            errno = EINVAL;
            return -1;
    }
}

/*
 * iconv_open - Open conversion descriptor
 */
iconv_t iconv_open(const char *tocode, const char *fromcode)
{
    enum encoding from = parse_encoding(fromcode);
    enum encoding to = parse_encoding(tocode);

    if (from == ENC_UNKNOWN || to == ENC_UNKNOWN)
    {
        errno = EINVAL;
        return (iconv_t)-1;
    }

    struct iconv_desc *desc = (struct iconv_desc *)malloc(sizeof(*desc));
    if (!desc)
    {
        errno = ENOMEM;
        return (iconv_t)-1;
    }

    desc->from = from;
    desc->to = to;

    return (iconv_t)desc;
}

/*
 * iconv - Convert characters
 */
size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft)
{
    if (cd == (iconv_t)-1)
    {
        errno = EBADF;
        return (size_t)-1;
    }

    struct iconv_desc *desc = (struct iconv_desc *)cd;

    /* Reset state if inbuf is NULL */
    if (!inbuf || !*inbuf)
    {
        return 0;
    }

    size_t conversions = 0;

    while (*inbytesleft > 0)
    {
        const char *save_in = *inbuf;
        size_t save_inlen = *inbytesleft;

        int codepoint = decode_char(desc->from, (const char **)inbuf, inbytesleft);
        if (codepoint < 0)
        {
            /* Restore pointers on error */
            *inbuf = (char *)save_in;
            *inbytesleft = save_inlen;
            return (size_t)-1;
        }

        int result = encode_char(desc->to, codepoint, outbuf, outbytesleft);
        if (result < 0)
        {
            /* Restore input pointers on output error */
            *inbuf = (char *)save_in;
            *inbytesleft = save_inlen;
            return (size_t)-1;
        }

        /* Check for non-reversible conversion (lossy) */
        if (desc->to == ENC_ASCII && codepoint > 127)
        {
            conversions++;
        }
        else if (desc->to == ENC_ISO8859_1 && codepoint > 255)
        {
            conversions++;
        }
    }

    return conversions;
}

/*
 * iconv_close - Close conversion descriptor
 */
int iconv_close(iconv_t cd)
{
    if (cd == (iconv_t)-1)
    {
        errno = EBADF;
        return -1;
    }

    free(cd);
    return 0;
}
