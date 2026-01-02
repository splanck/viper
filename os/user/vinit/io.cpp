/**
 * @file io.cpp
 * @brief Console I/O and string helpers for vinit.
 */
#include "vinit.hpp"

// =============================================================================
// String Helpers
// =============================================================================

usize strlen(const char *s)
{
    usize len = 0;
    while (s[len])
        len++;
    return len;
}

bool streq(const char *a, const char *b)
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

bool strstart(const char *s, const char *prefix)
{
    while (*prefix)
    {
        if (*s != *prefix)
            return false;
        s++;
        prefix++;
    }
    return true;
}

static char to_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
}

bool strcaseeq(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (to_lower(*a) != to_lower(*b))
            return false;
        a++;
        b++;
    }
    return *a == *b;
}

bool strcasestart(const char *s, const char *prefix)
{
    while (*prefix)
    {
        if (to_lower(*s) != to_lower(*prefix))
            return false;
        s++;
        prefix++;
    }
    return true;
}

// =============================================================================
// Paging State
// =============================================================================

static bool g_paging = false;
static bool g_page_quit = false;
static int g_page_line = 0;

// =============================================================================
// Paging Support
// =============================================================================

bool page_wait()
{
    // Use shell color (yellow) after reverse video ends
    sys::print("\x1b[7m-- More (Space=page, Enter=line, Q=quit) --\x1b[0m\x1b[33m");

    int c = sys::getchar();

    // Clear the prompt
    sys::print("\r\x1b[K");

    if (c == 'q' || c == 'Q')
    {
        g_page_quit = true;
        return false;
    }
    else if (c == ' ')
    {
        g_page_line = 0;
        return true;
    }
    else if (c == '\r' || c == '\n')
    {
        g_page_line = SCREEN_HEIGHT - 1;
        return true;
    }
    else
    {
        g_page_line = 0;
        return true;
    }
}

static void paged_print(const char *s)
{
    if (!g_paging || g_page_quit)
    {
        if (!g_page_quit)
            sys::print(s);
        return;
    }

    while (*s)
    {
        if (g_page_quit)
            return;

        sys::putchar(*s);

        if (*s == '\n')
        {
            g_page_line++;
            if (g_page_line >= SCREEN_HEIGHT - 1)
            {
                if (!page_wait())
                    return;
            }
        }
        s++;
    }
}

void paging_enable()
{
    g_paging = true;
    g_page_line = 0;
    g_page_quit = false;
}

void paging_disable()
{
    g_paging = false;
    g_page_line = 0;
    g_page_quit = false;
}

// =============================================================================
// Console Output
// =============================================================================

void print_str(const char *s)
{
    if (g_paging)
    {
        paged_print(s);
    }
    else
    {
        sys::print(s);
    }
}

void print_char(char c)
{
    sys::putchar(c);
}

void put_num(i64 n)
{
    char buf[32];
    char *p = buf + 31;
    *p = '\0';

    bool neg = false;
    if (n < 0)
    {
        neg = true;
        n = -n;
    }

    do
    {
        *--p = '0' + (n % 10);
        n /= 10;
    } while (n > 0);

    if (neg)
        *--p = '-';

    print_str(p);
}

void put_hex(u32 n)
{
    print_str("0x");
    char buf[16];
    char *p = buf + 15;
    *p = '\0';

    do
    {
        int digit = n & 0xF;
        *--p = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        n >>= 4;
    } while (n > 0);

    print_str(p);
}

// Memory routines (memcpy, memmove, memset) are provided by viperlibc
