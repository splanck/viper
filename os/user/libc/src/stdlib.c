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

void exit(int status)
{
    __syscall1(SYS_TASK_EXIT, status);
    while (1)
        ; /* Should never reach here */
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

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
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
