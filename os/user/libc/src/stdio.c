#include "../include/stdio.h"
#include "../include/string.h"
#include "../include/unistd.h"

/* stdarg implementation for AArch64 */
typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v, l) __builtin_va_arg(v, l)

/* Syscall helpers */
extern long __syscall1(long num, long arg0);
#define SYS_GETCHAR 0xF1
#define SYS_PUTCHAR 0xF2

static int vsnprintf_internal(char *str, size_t size, const char *format, va_list ap)
{
    size_t written = 0;

#define PUTC(c)                  \
    do                           \
    {                            \
        if (written < size - 1)  \
        {                        \
            str[written] = (c);  \
        }                        \
        written++;               \
    } while (0)

    while (*format && written < size)
    {
        if (*format != '%')
        {
            PUTC(*format++);
            continue;
        }

        format++; /* skip '%' */

        /* Parse flags */
        int zero_pad = 0;
        int width = 0;
        int left_justify = 0;

        if (*format == '-')
        {
            left_justify = 1;
            format++;
        }
        if (*format == '0')
        {
            zero_pad = 1;
            format++;
        }

        /* Parse width */
        while (*format >= '0' && *format <= '9')
        {
            width = width * 10 + (*format - '0');
            format++;
        }

        /* Parse format specifier */
        char buf[32];
        const char *s;
        int len;

        switch (*format)
        {
            case 'd':
            case 'i':
            {
                int val = va_arg(ap, int);
                int neg = 0;
                if (val < 0)
                {
                    neg = 1;
                    val = -val;
                }
                char *p = buf + sizeof(buf) - 1;
                *p = '\0';
                do
                {
                    *--p = '0' + (val % 10);
                    val /= 10;
                } while (val);
                if (neg)
                    *--p = '-';
                s = p;
                len = (buf + sizeof(buf) - 1) - p;
                goto output_string;
            }

            case 'u':
            {
                unsigned int val = va_arg(ap, unsigned int);
                char *p = buf + sizeof(buf) - 1;
                *p = '\0';
                do
                {
                    *--p = '0' + (val % 10);
                    val /= 10;
                } while (val);
                s = p;
                len = (buf + sizeof(buf) - 1) - p;
                goto output_string;
            }

            case 'x':
            case 'X':
            {
                unsigned int val = va_arg(ap, unsigned int);
                char *p = buf + sizeof(buf) - 1;
                *p = '\0';
                const char *digits = (*format == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                do
                {
                    *--p = digits[val & 0xF];
                    val >>= 4;
                } while (val);
                s = p;
                len = (buf + sizeof(buf) - 1) - p;
                goto output_string;
            }

            case 'p':
            {
                unsigned long val = (unsigned long)va_arg(ap, void *);
                char *p = buf + sizeof(buf) - 1;
                *p = '\0';
                do
                {
                    *--p = "0123456789abcdef"[val & 0xF];
                    val >>= 4;
                } while (val);
                *--p = 'x';
                *--p = '0';
                s = p;
                len = (buf + sizeof(buf) - 1) - p;
                goto output_string;
            }

            case 'l':
            {
                format++;
                if (*format == 'x' || *format == 'X')
                {
                    unsigned long val = va_arg(ap, unsigned long);
                    char *p = buf + sizeof(buf) - 1;
                    *p = '\0';
                    const char *digits =
                        (*format == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                    do
                    {
                        *--p = digits[val & 0xF];
                        val >>= 4;
                    } while (val);
                    s = p;
                    len = (buf + sizeof(buf) - 1) - p;
                    goto output_string;
                }
                else if (*format == 'd')
                {
                    long val = va_arg(ap, long);
                    int neg = 0;
                    if (val < 0)
                    {
                        neg = 1;
                        val = -val;
                    }
                    char *p = buf + sizeof(buf) - 1;
                    *p = '\0';
                    do
                    {
                        *--p = '0' + (val % 10);
                        val /= 10;
                    } while (val);
                    if (neg)
                        *--p = '-';
                    s = p;
                    len = (buf + sizeof(buf) - 1) - p;
                    goto output_string;
                }
                else if (*format == 'u')
                {
                    unsigned long val = va_arg(ap, unsigned long);
                    char *p = buf + sizeof(buf) - 1;
                    *p = '\0';
                    do
                    {
                        *--p = '0' + (val % 10);
                        val /= 10;
                    } while (val);
                    s = p;
                    len = (buf + sizeof(buf) - 1) - p;
                    goto output_string;
                }
                break;
            }

            case 's':
            {
                s = va_arg(ap, const char *);
                if (!s)
                    s = "(null)";
                len = strlen(s);
                goto output_string;
            }

            case 'c':
            {
                buf[0] = (char)va_arg(ap, int);
                buf[1] = '\0';
                s = buf;
                len = 1;
                goto output_string;
            }

            case '%':
                PUTC('%');
                break;

            output_string:
            {
                int pad = width - len;
                if (!left_justify)
                {
                    while (pad-- > 0)
                        PUTC(zero_pad ? '0' : ' ');
                }
                while (len--)
                    PUTC(*s++);
                if (left_justify)
                {
                    while (pad-- > 0)
                        PUTC(' ');
                }
                break;
            }

            default:
                PUTC('%');
                PUTC(*format);
                break;
        }

        format++;
    }

    if (size > 0)
    {
        str[written < size ? written : size - 1] = '\0';
    }

    return (int)written;

#undef PUTC
}

int snprintf(char *str, size_t size, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int result = vsnprintf_internal(str, size, format, ap);
    va_end(ap);
    return result;
}

int sprintf(char *str, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int result = vsnprintf_internal(str, 0x7FFFFFFF, format, ap);
    va_end(ap);
    return result;
}

int printf(const char *format, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, format);
    int result = vsnprintf_internal(buf, sizeof(buf), format, ap);
    va_end(ap);

    if (result > 0)
    {
        write(STDOUT_FILENO, buf, result);
    }

    return result;
}

int puts(const char *s)
{
    size_t len = strlen(s);
    write(STDOUT_FILENO, s, len);
    write(STDOUT_FILENO, "\n", 1);
    return 0;
}

int putchar(int c)
{
    char ch = (char)c;
    write(STDOUT_FILENO, &ch, 1);
    return c;
}

int getchar(void)
{
    long result = __syscall1(SYS_GETCHAR, 0);
    if (result < 0)
        return EOF;
    return (int)result;
}
