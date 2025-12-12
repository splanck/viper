//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_guid.c
// Purpose: UUID version 4 (random) generation and manipulation per RFC 4122.
// Key invariants: GUIDs are formatted as lowercase hex with dashes;
//                 version 4 and variant bits are properly set; uses
//                 cryptographically secure random source where available.
// Ownership/Lifetime: Returned strings are newly allocated.
// Links: docs/viperlib.md
//
//===----------------------------------------------------------------------===//

#include "rt_guid.h"

#include "rt_bytes.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <wincrypt.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#endif

/// @brief Fill buffer with cryptographically random bytes.
/// @param buf Destination buffer.
/// @param len Number of bytes to fill.
static void get_random_bytes(uint8_t *buf, size_t len)
{
#if defined(_WIN32)
    HCRYPTPROV hProv;
    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
    {
        CryptGenRandom(hProv, (DWORD)len, buf);
        CryptReleaseContext(hProv, 0);
    }
    else
    {
        // Fallback: less secure but functional
        srand((unsigned int)GetTickCount());
        for (size_t i = 0; i < len; i++)
        {
            buf[i] = (uint8_t)(rand() & 0xFF);
        }
    }
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0)
    {
        ssize_t result = read(fd, buf, len);
        close(fd);
        if (result == (ssize_t)len)
        {
            return;
        }
    }
    // Fallback: less secure but functional
    srand((unsigned int)time(NULL));
    for (size_t i = 0; i < len; i++)
    {
        buf[i] = (uint8_t)(rand() & 0xFF);
    }
#endif
}

/// @brief Convert hex character to integer value.
/// @param c Hex character (0-9, a-f, A-F).
/// @return Value 0-15, or -1 if invalid.
static int hex_digit_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

//=============================================================================
// Public API
//=============================================================================

/// @brief Generate a new random UUID v4.
/// @return Newly allocated string in format xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx.
rt_string rt_guid_new(void)
{
    uint8_t bytes[16];
    get_random_bytes(bytes, 16);

    // Set version 4 (random UUID) in byte 6: high nibble = 0100
    bytes[6] = (bytes[6] & 0x0F) | 0x40;

    // Set variant (RFC 4122) in byte 8: high bits = 10
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    // Format as lowercase hex with dashes (36 chars + null)
    char buf[37];
    snprintf(buf,
             sizeof(buf),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             bytes[0],
             bytes[1],
             bytes[2],
             bytes[3],
             bytes[4],
             bytes[5],
             bytes[6],
             bytes[7],
             bytes[8],
             bytes[9],
             bytes[10],
             bytes[11],
             bytes[12],
             bytes[13],
             bytes[14],
             bytes[15]);

    return rt_string_from_bytes(buf, 36);
}

/// @brief Return the nil UUID (all zeros).
/// @return Static string "00000000-0000-0000-0000-000000000000".
rt_string rt_guid_empty(void)
{
    return rt_const_cstr("00000000-0000-0000-0000-000000000000");
}

/// @brief Check if string is a valid GUID format.
/// @param str String to validate.
/// @return 1 if valid, 0 otherwise.
int8_t rt_guid_is_valid(rt_string str)
{
    if (!str)
    {
        return 0;
    }

    const char *s = rt_string_cstr(str);
    if (!s || strlen(s) != 36)
    {
        return 0;
    }

    // Expected format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    // Dash positions: 8, 13, 18, 23
    for (int i = 0; i < 36; i++)
    {
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            // Dash positions
            if (s[i] != '-')
            {
                return 0;
            }
        }
        else
        {
            // Hex digit positions
            if (!isxdigit((unsigned char)s[i]))
            {
                return 0;
            }
        }
    }

    return 1;
}

/// @brief Convert GUID string to 16-byte array.
/// @param str GUID string to convert.
/// @return Bytes object containing 16 bytes.
void *rt_guid_to_bytes(rt_string str)
{
    if (!rt_guid_is_valid(str))
    {
        rt_trap("Guid.ToBytes: invalid GUID format");
        return NULL;
    }

    void *bytes = rt_bytes_new(16);
    const char *s = rt_string_cstr(str);
    int str_pos = 0;
    int byte_idx = 0;

    while (s[str_pos] && byte_idx < 16)
    {
        // Skip dashes
        if (s[str_pos] == '-')
        {
            str_pos++;
            continue;
        }

        // Parse two hex digits
        int hi = hex_digit_value(s[str_pos]);
        int lo = hex_digit_value(s[str_pos + 1]);
        rt_bytes_set(bytes, byte_idx, (hi << 4) | lo);

        byte_idx++;
        str_pos += 2;
    }

    return bytes;
}

/// @brief Convert 16-byte array to GUID string.
/// @param bytes Bytes object containing exactly 16 bytes.
/// @return Newly allocated GUID string.
rt_string rt_guid_from_bytes(void *bytes)
{
    if (rt_bytes_len(bytes) != 16)
    {
        rt_trap("Guid.FromBytes: requires exactly 16 bytes");
        return NULL;
    }

    // Extract byte values
    uint8_t data[16];
    for (int i = 0; i < 16; i++)
    {
        data[i] = (uint8_t)rt_bytes_get(bytes, i);
    }

    // Format as GUID string
    char buf[37];
    snprintf(buf,
             sizeof(buf),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             data[0],
             data[1],
             data[2],
             data[3],
             data[4],
             data[5],
             data[6],
             data[7],
             data[8],
             data[9],
             data[10],
             data[11],
             data[12],
             data[13],
             data[14],
             data[15]);

    return rt_string_from_bytes(buf, 36);
}
