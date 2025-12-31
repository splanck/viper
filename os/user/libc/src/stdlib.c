#include "../include/stdlib.h"
#include "../include/string.h"

/* Syscall helpers - defined in syscall.S */
extern long __syscall1(long num, long arg0);
extern long __syscall2(long num, long arg0, long arg1);

/* Syscall numbers from viperos/syscall_nums.hpp */
#define SYS_TASK_EXIT 0x01
#define SYS_SBRK 0x0A

static void *sbrk(long increment)
{
    long result = __syscall1(SYS_SBRK, increment);
    if (result < 0)
    {
        return (void *)-1;
    }
    return (void *)result;
}

/* Simple block header for malloc */
struct block_header
{
    size_t size;
    struct block_header *next;
    int free;
};

static struct block_header *free_list = NULL;

void *malloc(size_t size)
{
    if (size == 0)
        return NULL;

    /* Align size to 16 bytes */
    size = (size + 15) & ~15UL;

    /* First check free list */
    struct block_header *prev = NULL;
    struct block_header *curr = free_list;
    while (curr)
    {
        if (curr->free && curr->size >= size)
        {
            curr->free = 0;
            return (void *)(curr + 1);
        }
        prev = curr;
        curr = curr->next;
    }

    /* Need to allocate new block from heap */
    size_t total = sizeof(struct block_header) + size;
    struct block_header *block = (struct block_header *)sbrk(total);
    if (block == (void *)-1)
    {
        return NULL;
    }

    block->size = size;
    block->next = NULL;
    block->free = 0;

    /* Add to list */
    if (prev)
    {
        prev->next = block;
    }
    else
    {
        free_list = block;
    }

    return (void *)(block + 1);
}

void free(void *ptr)
{
    if (!ptr)
        return;

    struct block_header *block = ((struct block_header *)ptr) - 1;
    block->free = 1;

    /* TODO: coalesce adjacent free blocks */
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr)
    {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr)
        return malloc(size);
    if (size == 0)
    {
        free(ptr);
        return NULL;
    }

    struct block_header *block = ((struct block_header *)ptr) - 1;
    if (block->size >= size)
    {
        return ptr;
    }

    void *new_ptr = malloc(size);
    if (new_ptr)
    {
        memcpy(new_ptr, ptr, block->size);
        free(ptr);
    }
    return new_ptr;
}

/* atexit handlers */
#define ATEXIT_MAX 32
static void (*atexit_handlers[ATEXIT_MAX])(void);
static int atexit_count = 0;

int atexit(void (*function)(void))
{
    if (atexit_count >= ATEXIT_MAX || !function)
        return -1;

    atexit_handlers[atexit_count++] = function;
    return 0;
}

void exit(int status)
{
    /* Call atexit handlers in reverse order of registration */
    while (atexit_count > 0)
    {
        atexit_count--;
        if (atexit_handlers[atexit_count])
            atexit_handlers[atexit_count]();
    }

    /* Flush stdio buffers */
    /* (Note: fflush(NULL) would flush all streams if we had proper stdio) */

    __syscall1(SYS_TASK_EXIT, status);
    while (1)
        ; /* Should never reach here */
}

void _Exit(int status)
{
    /* Exit immediately without cleanup */
    __syscall1(SYS_TASK_EXIT, status);
    while (1)
        ;
}

void _exit(int status)
{
    /* POSIX _exit - exit immediately without cleanup */
    __syscall1(SYS_TASK_EXIT, status);
    while (1)
        ;
}

void abort(void)
{
    exit(134); /* SIGABRT-like exit code */
}

int atoi(const char *nptr)
{
    return (int)atol(nptr);
}

long atol(const char *nptr)
{
    long result = 0;
    int neg = 0;

    while (*nptr == ' ' || *nptr == '\t')
        nptr++;

    if (*nptr == '-')
    {
        neg = 1;
        nptr++;
    }
    else if (*nptr == '+')
    {
        nptr++;
    }

    while (*nptr >= '0' && *nptr <= '9')
    {
        result = result * 10 + (*nptr - '0');
        nptr++;
    }

    return neg ? -result : result;
}

long long atoll(const char *nptr)
{
    long long result = 0;
    int neg = 0;

    while (*nptr == ' ' || *nptr == '\t')
        nptr++;

    if (*nptr == '-')
    {
        neg = 1;
        nptr++;
    }
    else if (*nptr == '+')
    {
        nptr++;
    }

    while (*nptr >= '0' && *nptr <= '9')
    {
        result = result * 10 + (*nptr - '0');
        nptr++;
    }

    return neg ? -result : result;
}

static int char_to_digit(char c, int base)
{
    int val;
    if (c >= '0' && c <= '9')
    {
        val = c - '0';
    }
    else if (c >= 'a' && c <= 'z')
    {
        val = c - 'a' + 10;
    }
    else if (c >= 'A' && c <= 'Z')
    {
        val = c - 'A' + 10;
    }
    else
    {
        return -1;
    }
    return (val < base) ? val : -1;
}

long strtol(const char *nptr, char **endptr, int base)
{
    const char *s = nptr;
    long result = 0;
    int neg = 0;

    /* Skip whitespace */
    while (*s == ' ' || *s == '\t' || *s == '\n')
        s++;

    /* Handle sign */
    if (*s == '-')
    {
        neg = 1;
        s++;
    }
    else if (*s == '+')
    {
        s++;
    }

    /* Handle base prefix */
    if (base == 0)
    {
        if (*s == '0')
        {
            if (s[1] == 'x' || s[1] == 'X')
            {
                base = 16;
                s += 2;
            }
            else
            {
                base = 8;
                s++;
            }
        }
        else
        {
            base = 10;
        }
    }
    else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        s += 2;
    }

    /* Convert */
    while (*s)
    {
        int digit = char_to_digit(*s, base);
        if (digit < 0)
            break;
        result = result * base + digit;
        s++;
    }

    if (endptr)
        *endptr = (char *)s;

    return neg ? -result : result;
}

unsigned long strtoul(const char *nptr, char **endptr, int base)
{
    const char *s = nptr;
    unsigned long result = 0;

    /* Skip whitespace */
    while (*s == ' ' || *s == '\t' || *s == '\n')
        s++;

    /* Skip optional + */
    if (*s == '+')
        s++;

    /* Handle base prefix */
    if (base == 0)
    {
        if (*s == '0')
        {
            if (s[1] == 'x' || s[1] == 'X')
            {
                base = 16;
                s += 2;
            }
            else
            {
                base = 8;
                s++;
            }
        }
        else
        {
            base = 10;
        }
    }
    else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        s += 2;
    }

    /* Convert */
    while (*s)
    {
        int digit = char_to_digit(*s, base);
        if (digit < 0)
            break;
        result = result * base + digit;
        s++;
    }

    if (endptr)
        *endptr = (char *)s;

    return result;
}

long long strtoll(const char *nptr, char **endptr, int base)
{
    return (long long)strtol(nptr, endptr, base);
}

unsigned long long strtoull(const char *nptr, char **endptr, int base)
{
    return (unsigned long long)strtoul(nptr, endptr, base);
}

int abs(int n)
{
    return (n < 0) ? -n : n;
}

long labs(long n)
{
    return (n < 0) ? -n : n;
}

long long llabs(long long n)
{
    return (n < 0) ? -n : n;
}

div_t div(int numer, int denom)
{
    div_t result;
    result.quot = numer / denom;
    result.rem = numer % denom;
    return result;
}

ldiv_t ldiv(long numer, long denom)
{
    ldiv_t result;
    result.quot = numer / denom;
    result.rem = numer % denom;
    return result;
}

lldiv_t lldiv(long long numer, long long denom)
{
    lldiv_t result;
    result.quot = numer / denom;
    result.rem = numer % denom;
    return result;
}

/* Simple swap helper for qsort */
static void swap_bytes(void *a, void *b, size_t size)
{
    unsigned char *pa = (unsigned char *)a;
    unsigned char *pb = (unsigned char *)b;
    while (size--)
    {
        unsigned char tmp = *pa;
        *pa++ = *pb;
        *pb++ = tmp;
    }
}

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *))
{
    /* Simple insertion sort for now - works well for small arrays */
    unsigned char *arr = (unsigned char *)base;

    for (size_t i = 1; i < nmemb; i++)
    {
        size_t j = i;
        while (j > 0 && compar(arr + (j - 1) * size, arr + j * size) > 0)
        {
            swap_bytes(arr + (j - 1) * size, arr + j * size, size);
            j--;
        }
    }
}

void *bsearch(const void *key,
              const void *base,
              size_t nmemb,
              size_t size,
              int (*compar)(const void *, const void *))
{
    const unsigned char *arr = (const unsigned char *)base;
    size_t lo = 0;
    size_t hi = nmemb;

    while (lo < hi)
    {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = compar(key, arr + mid * size);
        if (cmp < 0)
        {
            hi = mid;
        }
        else if (cmp > 0)
        {
            lo = mid + 1;
        }
        else
        {
            return (void *)(arr + mid * size);
        }
    }

    return NULL;
}

/* Simple linear congruential generator for rand() */
static unsigned int rand_seed = 1;

int rand(void)
{
    rand_seed = rand_seed * 1103515245 + 12345;
    return (int)((rand_seed / 65536) % 32768);
}

void srand(unsigned int seed)
{
    rand_seed = seed;
}

/*
 * Environment variables
 *
 * Simple implementation using a static array.
 * Each entry is "NAME=value" format.
 */
#define ENV_MAX 64
#define ENV_ENTRY_MAX 256

static char env_storage[ENV_MAX][ENV_ENTRY_MAX];
static char *environ_ptrs[ENV_MAX + 1];
static int env_count = 0;
static int env_initialized = 0;

/* Global environ pointer (required by POSIX) */
char **environ = environ_ptrs;

static void init_environ(void)
{
    if (!env_initialized)
    {
        for (int i = 0; i <= ENV_MAX; i++)
            environ_ptrs[i] = NULL;
        env_initialized = 1;
    }
}

static int env_find(const char *name)
{
    size_t len = 0;
    while (name[len] && name[len] != '=')
        len++;

    for (int i = 0; i < env_count; i++)
    {
        if (environ_ptrs[i] && strncmp(environ_ptrs[i], name, len) == 0 &&
            environ_ptrs[i][len] == '=')
        {
            return i;
        }
    }
    return -1;
}

char *getenv(const char *name)
{
    if (!name)
        return NULL;

    init_environ();

    int idx = env_find(name);
    if (idx < 0)
        return NULL;

    /* Return pointer to the value part (after '=') */
    char *p = environ_ptrs[idx];
    while (*p && *p != '=')
        p++;
    if (*p == '=')
        p++;
    return p;
}

int setenv(const char *name, const char *value, int overwrite)
{
    if (!name || !*name || strchr(name, '='))
        return -1;

    init_environ();

    int idx = env_find(name);
    if (idx >= 0)
    {
        if (!overwrite)
            return 0;
    }
    else
    {
        if (env_count >= ENV_MAX)
            return -1;
        idx = env_count++;
    }

    /* Build "NAME=value" string */
    size_t name_len = strlen(name);
    size_t value_len = value ? strlen(value) : 0;
    if (name_len + 1 + value_len + 1 > ENV_ENTRY_MAX)
        return -1;

    char *entry = env_storage[idx];
    memcpy(entry, name, name_len);
    entry[name_len] = '=';
    if (value)
        memcpy(entry + name_len + 1, value, value_len + 1);
    else
        entry[name_len + 1] = '\0';

    environ_ptrs[idx] = entry;
    return 0;
}

int unsetenv(const char *name)
{
    if (!name || !*name || strchr(name, '='))
        return -1;

    init_environ();

    int idx = env_find(name);
    if (idx < 0)
        return 0; /* Not found is not an error */

    /* Shift remaining entries down */
    for (int i = idx; i < env_count - 1; i++)
    {
        memcpy(env_storage[i], env_storage[i + 1], ENV_ENTRY_MAX);
        environ_ptrs[i] = env_storage[i];
    }
    env_count--;
    environ_ptrs[env_count] = NULL;

    return 0;
}

int putenv(char *string)
{
    if (!string)
        return -1;

    char *eq = strchr(string, '=');
    if (!eq)
        return -1;

    /* Extract name */
    size_t name_len = eq - string;
    char name[256];
    if (name_len >= sizeof(name))
        return -1;
    memcpy(name, string, name_len);
    name[name_len] = '\0';

    return setenv(name, eq + 1, 1);
}

/*
 * Floating-point string conversion
 */

/* Helper: check if character is whitespace */
static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

double strtod(const char *nptr, char **endptr)
{
    const char *s = nptr;
    double result = 0.0;
    int sign = 1;
    int exp_sign = 1;
    int exponent = 0;
    int has_digits = 0;

    /* Skip leading whitespace */
    while (is_space(*s))
        s++;

    /* Handle sign */
    if (*s == '-')
    {
        sign = -1;
        s++;
    }
    else if (*s == '+')
    {
        s++;
    }

    /* Handle special values */
    if ((s[0] == 'i' || s[0] == 'I') && (s[1] == 'n' || s[1] == 'N') &&
        (s[2] == 'f' || s[2] == 'F'))
    {
        s += 3;
        if ((s[0] == 'i' || s[0] == 'I') && (s[1] == 'n' || s[1] == 'N') &&
            (s[2] == 'i' || s[2] == 'I') && (s[3] == 't' || s[3] == 'T') &&
            (s[4] == 'y' || s[4] == 'Y'))
        {
            s += 5;
        }
        if (endptr)
            *endptr = (char *)s;
        return sign > 0 ? __builtin_inf() : -__builtin_inf();
    }

    if ((s[0] == 'n' || s[0] == 'N') && (s[1] == 'a' || s[1] == 'A') &&
        (s[2] == 'n' || s[2] == 'N'))
    {
        s += 3;
        if (endptr)
            *endptr = (char *)s;
        return __builtin_nan("");
    }

    /* Parse integer part */
    while (*s >= '0' && *s <= '9')
    {
        result = result * 10.0 + (*s - '0');
        s++;
        has_digits = 1;
    }

    /* Parse fractional part */
    if (*s == '.')
    {
        s++;
        double fraction = 0.1;
        while (*s >= '0' && *s <= '9')
        {
            result += (*s - '0') * fraction;
            fraction *= 0.1;
            s++;
            has_digits = 1;
        }
    }

    /* Parse exponent */
    if (has_digits && (*s == 'e' || *s == 'E'))
    {
        s++;
        if (*s == '-')
        {
            exp_sign = -1;
            s++;
        }
        else if (*s == '+')
        {
            s++;
        }

        while (*s >= '0' && *s <= '9')
        {
            exponent = exponent * 10 + (*s - '0');
            s++;
        }

        /* Apply exponent */
        double exp_mult = 1.0;
        while (exponent > 0)
        {
            exp_mult *= 10.0;
            exponent--;
        }
        if (exp_sign > 0)
            result *= exp_mult;
        else
            result /= exp_mult;
    }

    if (endptr)
        *endptr = has_digits ? (char *)s : (char *)nptr;

    return sign * result;
}

float strtof(const char *nptr, char **endptr)
{
    return (float)strtod(nptr, endptr);
}

/*
 * Note: In freestanding environment without compiler-rt, long double
 * operations require runtime support (__extenddftf2) we don't have.
 * This implementation uses a union to avoid the compiler generating
 * a call to the soft-float conversion routine.
 */
long double strtold(const char *nptr, char **endptr)
{
    /* Parse as double - same precision we'll output */
    double result = strtod(nptr, endptr);

    /*
     * Avoid implicit double->long double conversion.
     * On AArch64 without compiler-rt, we simply store the double
     * value in the low 64 bits of the long double return register.
     * This is imprecise but avoids missing symbol errors.
     */
    union
    {
        double d;
        long double ld;
    } u = {0};

    u.d = result;
    return u.ld;
}

double atof(const char *nptr)
{
    return strtod(nptr, (char **)0);
}

/*
 * Integer to string conversion
 */

static char *unsigned_to_str(unsigned long value, char *str, int base, int is_negative)
{
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char *p = str;
    char *start;

    if (base < 2 || base > 36)
    {
        *p = '\0';
        return str;
    }

    /* Handle zero */
    if (value == 0 && !is_negative)
    {
        *p++ = '0';
        *p = '\0';
        return str;
    }

    /* Build string in reverse */
    start = p;
    if (is_negative)
        p++; /* Reserve space for sign */

    char *digit_start = p;
    while (value > 0)
    {
        *p++ = digits[value % base];
        value /= base;
    }
    *p = '\0';

    /* Reverse the digits */
    char *end = p - 1;
    while (digit_start < end)
    {
        char tmp = *digit_start;
        *digit_start = *end;
        *end = tmp;
        digit_start++;
        end--;
    }

    /* Add sign if needed */
    if (is_negative)
        *start = '-';

    return str;
}

char *itoa(int value, char *str, int base)
{
    if (value < 0 && base == 10)
    {
        return unsigned_to_str((unsigned long)(-(long)value), str, base, 1);
    }
    return unsigned_to_str((unsigned long)(unsigned int)value, str, base, 0);
}

char *ltoa(long value, char *str, int base)
{
    if (value < 0 && base == 10)
    {
        return unsigned_to_str((unsigned long)(-value), str, base, 1);
    }
    return unsigned_to_str((unsigned long)value, str, base, 0);
}

char *ultoa(unsigned long value, char *str, int base)
{
    return unsigned_to_str(value, str, base, 0);
}
