#include "../include/stdio.h"
#include "../include/string.h"
#include "../include/unistd.h"
#include "../include/fcntl.h"

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
    int buf_mode;    /* _IOFBF, _IOLBF, or _IONBF */
    char *buf;       /* Buffer pointer (NULL if none) */
    size_t buf_size; /* Size of buffer */
    size_t buf_pos;  /* Current position in buffer */
    int buf_owned;   /* 1 if we allocated the buffer */
};

/* Default buffers for stdout (line buffered) */
static char _stdout_buf[BUFSIZ];

/* Static FILE objects for standard streams */
static struct _FILE _stdin_file = {0, 0, 0, _IONBF, NULL, 0, 0, 0};
static struct _FILE _stdout_file = {1, 0, 0, _IOLBF, _stdout_buf, BUFSIZ, 0, 0};
static struct _FILE _stderr_file = {2, 0, 0, _IONBF, NULL, 0, 0, 0};

/* Standard stream pointers */
FILE *stdin = &_stdin_file;
FILE *stdout = &_stdout_file;
FILE *stderr = &_stderr_file;

static int vsnprintf_internal(char *str, size_t size, const char *format, va_list ap)
{
    size_t written = 0;

#define PUTC(c)                                                                                    \
    do                                                                                             \
    {                                                                                              \
        if (written < size - 1)                                                                    \
        {                                                                                          \
            str[written] = (c);                                                                    \
        }                                                                                          \
        written++;                                                                                 \
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
                    unsigned long long val =
                        is_longlong ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned long);
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
                    unsigned long long val =
                        is_longlong ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned long);
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
    unsigned char c = 0;
    long n = read(STDIN_FILENO, &c, 1);
    if (n <= 0)
        return EOF;
    return (int)c;
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

/* Helper to flush and write a character */
static int fputc_unbuffered(int c, FILE *stream)
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

/* Character I/O with FILE */
int fputc(int c, FILE *stream)
{
    /* No buffering or no buffer - write directly */
    if (stream->buf_mode == _IONBF || stream->buf == NULL)
    {
        return fputc_unbuffered(c, stream);
    }

    /* Add to buffer */
    stream->buf[stream->buf_pos++] = (char)c;

    /* Check if we need to flush */
    int should_flush = 0;

    if (stream->buf_pos >= stream->buf_size)
    {
        /* Buffer full */
        should_flush = 1;
    }
    else if (stream->buf_mode == _IOLBF && c == '\n')
    {
        /* Line buffered and got newline */
        should_flush = 1;
    }

    if (should_flush)
    {
        if (fflush(stream) == EOF)
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
    if (stream == NULL)
    {
        /* Flush all streams - just stdout for now */
        fflush(stdout);
        return 0;
    }

    /* If there's buffered data, write it out */
    if (stream->buf && stream->buf_pos > 0)
    {
        long result = write(stream->fd, stream->buf, stream->buf_pos);
        if (result < 0)
        {
            stream->error = 1;
            return EOF;
        }
        stream->buf_pos = 0;
    }
    return 0;
}

/* Buffering control */
int setvbuf(FILE *stream, char *buf, int mode, size_t size)
{
    /* Flush any existing buffer first */
    fflush(stream);

    /* Validate mode */
    if (mode != _IOFBF && mode != _IOLBF && mode != _IONBF)
        return -1;

    /* If we owned the old buffer, we would free it here (but we don't malloc) */
    stream->buf_owned = 0;

    if (mode == _IONBF)
    {
        /* No buffering */
        stream->buf = NULL;
        stream->buf_size = 0;
        stream->buf_pos = 0;
    }
    else
    {
        if (buf != NULL)
        {
            /* Use provided buffer */
            stream->buf = buf;
            stream->buf_size = size;
            stream->buf_owned = 0;
        }
        else if (size > 0)
        {
            /* Caller wants us to allocate, but we can't in freestanding */
            /* Fall back to unbuffered */
            stream->buf = NULL;
            stream->buf_size = 0;
            mode = _IONBF;
        }
        stream->buf_pos = 0;
    }

    stream->buf_mode = mode;
    return 0;
}

void setbuf(FILE *stream, char *buf)
{
    if (buf != NULL)
        setvbuf(stream, buf, _IOFBF, BUFSIZ);
    else
        setvbuf(stream, NULL, _IONBF, 0);
}

void setlinebuf(FILE *stream)
{
    /* Line buffering with default buffer */
    setvbuf(stream, NULL, _IOLBF, 0);
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

/* Pool of FILE structures */
#define FILE_POOL_SIZE 20
static struct _FILE file_pool[FILE_POOL_SIZE];
static int file_pool_init = 0;

static void init_file_pool(void)
{
    if (file_pool_init)
        return;
    for (int i = 0; i < FILE_POOL_SIZE; i++)
    {
        file_pool[i].fd = -1;
    }
    file_pool_init = 1;
}

static struct _FILE *alloc_file(void)
{
    init_file_pool();
    for (int i = 0; i < FILE_POOL_SIZE; i++)
    {
        if (file_pool[i].fd == -1)
        {
            return &file_pool[i];
        }
    }
    return (struct _FILE *)0;
}

static int parse_mode(const char *mode)
{
    int flags = 0;
    int has_plus = 0;

    /* Check for '+' anywhere in mode string */
    for (const char *p = mode; *p; p++)
    {
        if (*p == '+')
            has_plus = 1;
    }

    switch (mode[0])
    {
        case 'r':
            flags = has_plus ? O_RDWR : O_RDONLY;
            break;
        case 'w':
            flags = (has_plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
            break;
        case 'a':
            flags = (has_plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
            break;
        default:
            return -1;
    }
    return flags;
}

FILE *fopen(const char *pathname, const char *mode)
{
    if (!pathname || !mode)
        return (FILE *)0;

    int flags = parse_mode(mode);
    if (flags < 0)
        return (FILE *)0;

    int fd = open(pathname, flags, 0666);
    if (fd < 0)
        return (FILE *)0;

    struct _FILE *f = alloc_file();
    if (!f)
    {
        close(fd);
        return (FILE *)0;
    }

    f->fd = fd;
    f->error = 0;
    f->eof = 0;
    f->buf_mode = _IOFBF;
    f->buf = (char *)0;
    f->buf_size = 0;
    f->buf_pos = 0;
    f->buf_owned = 0;

    return f;
}

FILE *fdopen(int fd, const char *mode)
{
    if (fd < 0 || !mode)
        return (FILE *)0;

    struct _FILE *f = alloc_file();
    if (!f)
        return (FILE *)0;

    f->fd = fd;
    f->error = 0;
    f->eof = 0;
    f->buf_mode = _IOFBF;
    f->buf = (char *)0;
    f->buf_size = 0;
    f->buf_pos = 0;
    f->buf_owned = 0;

    (void)mode; /* Mode is for compatibility */
    return f;
}

FILE *freopen(const char *pathname, const char *mode, FILE *stream)
{
    if (!stream)
        return (FILE *)0;

    /* Close existing file */
    fflush(stream);
    if (stream->fd >= 0 && stream != stdin && stream != stdout && stream != stderr)
    {
        close(stream->fd);
    }

    if (!pathname)
    {
        /* Just change mode - not fully supported */
        return stream;
    }

    int flags = parse_mode(mode);
    if (flags < 0)
        return (FILE *)0;

    int fd = open(pathname, flags, 0666);
    if (fd < 0)
        return (FILE *)0;

    stream->fd = fd;
    stream->error = 0;
    stream->eof = 0;
    stream->buf_pos = 0;

    return stream;
}

int fclose(FILE *stream)
{
    if (!stream)
        return EOF;

    fflush(stream);

    int result = 0;
    if (stream->fd >= 0 && stream != stdin && stream != stdout && stream != stderr)
    {
        result = close(stream->fd);
        stream->fd = -1;
    }

    return (result < 0) ? EOF : 0;
}

int fileno(FILE *stream)
{
    if (!stream)
        return -1;
    return stream->fd;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    if (!stream || !ptr || size == 0 || nmemb == 0)
        return 0;

    size_t total = size * nmemb;
    ssize_t bytes_read = read(stream->fd, ptr, total);

    if (bytes_read < 0)
    {
        stream->error = 1;
        return 0;
    }
    if (bytes_read == 0)
    {
        stream->eof = 1;
        return 0;
    }

    return (size_t)bytes_read / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    if (!stream || !ptr || size == 0 || nmemb == 0)
        return 0;

    size_t total = size * nmemb;
    ssize_t bytes_written = write(stream->fd, ptr, total);

    if (bytes_written < 0)
    {
        stream->error = 1;
        return 0;
    }

    return (size_t)bytes_written / size;
}

int fseek(FILE *stream, long offset, int whence)
{
    if (!stream)
        return -1;

    fflush(stream);
    long result = lseek(stream->fd, offset, whence);
    if (result < 0)
        return -1;

    stream->eof = 0;
    return 0;
}

long ftell(FILE *stream)
{
    if (!stream)
        return -1L;

    fflush(stream);
    return lseek(stream->fd, 0, SEEK_CUR);
}

void rewind(FILE *stream)
{
    if (stream)
    {
        fseek(stream, 0L, SEEK_SET);
        stream->error = 0;
    }
}

int fgetpos(FILE *stream, fpos_t *pos)
{
    if (!stream || !pos)
        return -1;

    long p = ftell(stream);
    if (p < 0)
        return -1;

    *pos = p;
    return 0;
}

int fsetpos(FILE *stream, const fpos_t *pos)
{
    if (!stream || !pos)
        return -1;

    return fseek(stream, *pos, SEEK_SET);
}

/* Unget buffer - one character per stream */
static int ungetc_buf[FILE_POOL_SIZE + 3] = {EOF, EOF, EOF}; /* +3 for stdin/stdout/stderr */

static int get_stream_index(FILE *stream)
{
    if (stream == stdin)
        return 0;
    if (stream == stdout)
        return 1;
    if (stream == stderr)
        return 2;
    for (int i = 0; i < FILE_POOL_SIZE; i++)
    {
        if (stream == &file_pool[i])
            return i + 3;
    }
    return -1;
}

int ungetc(int c, FILE *stream)
{
    if (!stream || c == EOF)
        return EOF;

    int idx = get_stream_index(stream);
    if (idx < 0)
        return EOF;

    if (ungetc_buf[idx] != EOF)
        return EOF; /* Already have an unget char */

    ungetc_buf[idx] = c;
    stream->eof = 0;
    return c;
}

/* Need to include string.h for strerror */
extern char *strerror(int errnum);
extern int *__errno_location(void);
#define errno (*__errno_location())

void perror(const char *s)
{
    if (s && *s)
    {
        fputs(s, stderr);
        fputs(": ", stderr);
    }
    fputs(strerror(errno), stderr);
    fputc('\n', stderr);
}

int remove(const char *pathname)
{
    if (!pathname)
        return -1;
    return unlink(pathname);
}

int rename_file(const char *oldpath, const char *newpath)
{
    if (!oldpath || !newpath)
        return -1;
    return rename(oldpath, newpath);
}

/* Temporary file name generation */
static unsigned int tmpnam_counter = 0;

char *tmpnam(char *s)
{
    static char tmpbuf[L_tmpnam];
    char *buf = s ? s : tmpbuf;

    /* Generate name like /tmp/tmpXXXXXX */
    const char *prefix = "/tmp/tmp";
    char *p = buf;
    while (*prefix)
        *p++ = *prefix++;

    unsigned int n = tmpnam_counter++;
    for (int i = 0; i < 6; i++)
    {
        *p++ = 'A' + (n % 26);
        n /= 26;
    }
    *p = '\0';

    return buf;
}

FILE *tmpfile(void)
{
    char name[L_tmpnam];
    tmpnam(name);
    return fopen(name, "w+");
}

/* getline/getdelim implementation */
extern void *malloc(size_t size);
extern void *realloc(void *ptr, size_t size);

ssize_t getdelim(char **lineptr, size_t *n, int delim, FILE *stream)
{
    if (!lineptr || !n || !stream)
        return -1;

    if (*lineptr == (char *)0 || *n == 0)
    {
        *n = 128;
        *lineptr = (char *)malloc(*n);
        if (!*lineptr)
            return -1;
    }

    size_t pos = 0;
    int c;

    while ((c = fgetc(stream)) != EOF)
    {
        /* Ensure space for char + null terminator */
        if (pos + 2 > *n)
        {
            size_t new_size = *n * 2;
            char *new_ptr = (char *)realloc(*lineptr, new_size);
            if (!new_ptr)
                return -1;
            *lineptr = new_ptr;
            *n = new_size;
        }

        (*lineptr)[pos++] = (char)c;
        if (c == delim)
            break;
    }

    if (pos == 0 && c == EOF)
        return -1;

    (*lineptr)[pos] = '\0';
    return (ssize_t)pos;
}

ssize_t getline(char **lineptr, size_t *n, FILE *stream)
{
    return getdelim(lineptr, n, '\n', stream);
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
