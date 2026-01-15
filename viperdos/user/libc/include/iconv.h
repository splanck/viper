/*
 * ViperDOS libc - iconv.h
 * Character set conversion
 */

#ifndef _ICONV_H
#define _ICONV_H

#include "sys/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Conversion descriptor type
     */
    typedef void *iconv_t;

    /*
     * iconv_open - Open conversion descriptor
     *
     * Opens a conversion descriptor for converting characters from
     * one encoding to another.
     *
     * Supported encodings:
     *   - "UTF-8"
     *   - "ASCII"
     *   - "ISO-8859-1" (Latin-1)
     *   - "UTF-16", "UTF-16BE", "UTF-16LE"
     *   - "UTF-32", "UTF-32BE", "UTF-32LE"
     *
     * @tocode: Target encoding name
     * @fromcode: Source encoding name
     *
     * Returns conversion descriptor, or (iconv_t)-1 on error.
     */
    iconv_t iconv_open(const char *tocode, const char *fromcode);

    /*
     * iconv - Convert characters
     *
     * Converts characters from the input buffer to the output buffer.
     *
     * @cd: Conversion descriptor from iconv_open()
     * @inbuf: Pointer to input buffer pointer (updated on return)
     * @inbytesleft: Pointer to remaining input bytes (updated)
     * @outbuf: Pointer to output buffer pointer (updated)
     * @outbytesleft: Pointer to remaining output space (updated)
     *
     * Returns number of non-reversible conversions performed,
     * or (size_t)-1 on error with errno set.
     *
     * Errors:
     *   EILSEQ - Invalid input sequence
     *   E2BIG  - Output buffer too small
     *   EINVAL - Incomplete input sequence
     */
    size_t iconv(
        iconv_t cd, char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft);

    /*
     * iconv_close - Close conversion descriptor
     *
     * Releases resources associated with a conversion descriptor.
     *
     * @cd: Conversion descriptor to close
     *
     * Returns 0 on success, -1 on error.
     */
    int iconv_close(iconv_t cd);

#ifdef __cplusplus
}
#endif

#endif /* _ICONV_H */
