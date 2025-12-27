#include "../include/stdio.h"
#include "../include/string.h"
#include "../include/unistd.h"

/* Syscall helpers */
extern long __syscall1(long num, long arg0);
#define SYS_GETCHAR 0xF1
#define SYS_PUTCHAR 0xF2

/* Minimal FILE structure for freestanding environment */
struct _FILE
{
    int fd;
    int error;
    int eof;
};

/* Static FILE objects for standard streams */
static struct _FILE _stdin_file = {0, 0, 0};
static struct _FILE _stdout_file = {1, 0, 0};
static struct _FILE _stderr_file = {2, 0, 0};

/* Standard stream pointers */
FILE *stdin = &_stdin_file;
FILE *stdout = &_stdout_file;
FILE *stderr = &_stderr_file;

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
                /* Check for 'll' (long long) */
                int is_longlong = 0;
                if (*format == 'l')
                {
                    is_longlong = 1;
                    format++;
                }

                if (*format == 'x' || *format == 'X')
                {
                    unsigned long long val = is_longlong ? va_arg(ap, unsigned long long)
                                                         : va_arg(ap, unsigned long);
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
                else if (*format == 'd' || *format == 'i')
                {
                    long long val = is_longlong ? va_arg(ap, long long) : va_arg(ap, long);
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
                    unsigned long long val = is_longlong ? va_arg(ap, unsigned long long)
                                                         : va_arg(ap, unsigned long);
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

/* Variadic versions */
int vsnprintf(char *str, size_t size, const char *format, va_list ap)
{
    return vsnprintf_internal(str, size, format, ap);
}

int vsprintf(char *str, const char *format, va_list ap)
{
    return vsnprintf_internal(str, 0x7FFFFFFF, format, ap);
}

int vprintf(const char *format, va_list ap)
{
    char buf[512];
    int result = vsnprintf_internal(buf, sizeof(buf), format, ap);
    if (result > 0)
    {
        write(STDOUT_FILENO, buf, result);
    }
    return result;
}

int vfprintf(FILE *stream, const char *format, va_list ap)
{
    char buf[512];
    int result = vsnprintf_internal(buf, sizeof(buf), format, ap);
    if (result > 0)
    {
        write(stream->fd, buf, result);
    }
    return result;
}

int fprintf(FILE *stream, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int result = vfprintf(stream, format, ap);
    va_end(ap);
    return result;
}

/* Character I/O with FILE */
int fputc(int c, FILE *stream)
{
    char ch = (char)c;
    long result = write(stream->fd, &ch, 1);
    if (result < 0)
    {
        stream->error = 1;
        return EOF;
    }
    return (unsigned char)c;
}

int putc(int c, FILE *stream)
{
    return fputc(c, stream);
}

int fputs(const char *s, FILE *stream)
{
    size_t len = strlen(s);
    long result = write(stream->fd, s, len);
    if (result < 0)
    {
        stream->error = 1;
        return EOF;
    }
    return (int)result;
}

int fgetc(FILE *stream)
{
    char c;
    long result = read(stream->fd, &c, 1);
    if (result <= 0)
    {
        if (result == 0)
            stream->eof = 1;
        else
            stream->error = 1;
        return EOF;
    }
    return (unsigned char)c;
}

int getc(FILE *stream)
{
    return fgetc(stream);
}

char *fgets(char *s, int size, FILE *stream)
{
    if (size <= 0)
        return NULL;

    int i = 0;
    while (i < size - 1)
    {
        int c = fgetc(stream);
        if (c == EOF)
        {
            if (i == 0)
                return NULL;
            break;
        }
        s[i++] = (char)c;
        if (c == '\n')
            break;
    }
    s[i] = '\0';
    return s;
}

/* Error handling */
int ferror(FILE *stream)
{
    return stream->error;
}

void clearerr(FILE *stream)
{
    stream->error = 0;
    stream->eof = 0;
}

int feof(FILE *stream)
{
    return stream->eof;
}

int fflush(FILE *stream)
{
    (void)stream;
    /* No buffering in this simple implementation */
    return 0;
}

/* Simple sscanf implementation */
static int skip_whitespace(const char **str)
{
    int count = 0;
    while (**str == ' ' || **str == '\t' || **str == '\n')
    {
        (*str)++;
        count++;
    }
    return count;
}

int sscanf(const char *str, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int matched = 0;
    const char *s = str;

    while (*format)
    {
        if (*format == ' ' || *format == '\t' || *format == '\n')
        {
            skip_whitespace(&s);
            format++;
            continue;
        }

        if (*format != '%')
        {
            if (*s != *format)
                break;
            s++;
            format++;
            continue;
        }

        format++; /* skip '%' */

        /* Parse width (optional) */
        int width = 0;
        while (*format >= '0' && *format <= '9')
        {
            width = width * 10 + (*format - '0');
            format++;
        }

        /* Handle format specifier */
        switch (*format)
        {
            case 'd':
            case 'i':
            {
                skip_whitespace(&s);
                int *ptr = va_arg(ap, int *);
                int neg = 0;
                long val = 0;

                if (*s == '-')
                {
                    neg = 1;
                    s++;
                }
                else if (*s == '+')
                {
                    s++;
                }

                if (!(*s >= '0' && *s <= '9'))
                    goto done;

                int digits = 0;
                while (*s >= '0' && *s <= '9')
                {
                    val = val * 10 + (*s - '0');
                    s++;
                    digits++;
                    if (width > 0 && digits >= width)
                        break;
                }

                *ptr = neg ? -val : val;
                matched++;
                break;
            }

            case 'u':
            {
                skip_whitespace(&s);
                unsigned int *ptr = va_arg(ap, unsigned int *);
                unsigned long val = 0;

                if (!(*s >= '0' && *s <= '9'))
                    goto done;

                int digits = 0;
                while (*s >= '0' && *s <= '9')
                {
                    val = val * 10 + (*s - '0');
                    s++;
                    digits++;
                    if (width > 0 && digits >= width)
                        break;
                }

                *ptr = (unsigned int)val;
                matched++;
                break;
            }

            case 'x':
            case 'X':
            {
                skip_whitespace(&s);
                unsigned int *ptr = va_arg(ap, unsigned int *);
                unsigned long val = 0;

                /* Skip optional 0x prefix */
                if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
                    s += 2;

                int digits = 0;
                while (1)
                {
                    int digit;
                    if (*s >= '0' && *s <= '9')
                        digit = *s - '0';
                    else if (*s >= 'a' && *s <= 'f')
                        digit = *s - 'a' + 10;
                    else if (*s >= 'A' && *s <= 'F')
                        digit = *s - 'A' + 10;
                    else
                        break;

                    val = val * 16 + digit;
                    s++;
                    digits++;
                    if (width > 0 && digits >= width)
                        break;
                }

                if (digits == 0)
                    goto done;

                *ptr = (unsigned int)val;
                matched++;
                break;
            }

            case 's':
            {
                skip_whitespace(&s);
                char *ptr = va_arg(ap, char *);
                int len = 0;

                while (*s && *s != ' ' && *s != '\t' && *s != '\n')
                {
                    if (width > 0 && len >= width)
                        break;
                    *ptr++ = *s++;
                    len++;
                }
                *ptr = '\0';

                if (len > 0)
                    matched++;
                else
                    goto done;
                break;
            }

            case 'c':
            {
                char *ptr = va_arg(ap, char *);
                if (!*s)
                    goto done;
                *ptr = *s++;
                matched++;
                break;
            }

            case 'n':
            {
                int *ptr = va_arg(ap, int *);
                *ptr = (int)(s - str);
                /* %n doesn't count as a matched item */
                break;
            }

            case '%':
                if (*s != '%')
                    goto done;
                s++;
                break;

            default:
                goto done;
        }

        format++;
    }

done:
    va_end(ap);
    return matched;
}
