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
