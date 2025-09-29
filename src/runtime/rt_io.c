// File: src/runtime/rt_io.c
// Purpose: Implements I/O utilities and trap handling for the BASIC runtime.
// Key invariants: Output routines do not append newlines unless specified.
// Ownership/Lifetime: Caller manages strings passed to printing routines.
// Links: docs/codemap.md

#include "rt_internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Terminate the program immediately due to a fatal runtime error.
 *
 * @param msg Optional message describing the reason for the abort.
 * @return Never returns.
 */
void rt_abort(const char *msg)
{
    if (msg && *msg)
        fprintf(stderr, "%s\n", msg);
    else
        fprintf(stderr, "Trap\n");
    exit(1);
}

/**
 * Trap handler used by the VM layer. Can be overridden by hosts.
 *
 * @param msg Optional message describing the trap condition.
 * @return Never returns.
 */
__attribute__((weak)) void vm_trap(const char *msg)
{
    rt_abort(msg);
}

/**
 * Entry point for raising runtime traps from helper routines.
 *
 * @param msg Message describing the trap condition.
 * @return Never returns.
 */
void rt_trap(const char *msg)
{
    vm_trap(msg);
}

/**
 * Write a runtime string to standard output without a trailing newline.
 *
 * @param s String to print; NULL strings are ignored.
 */
void rt_print_str(rt_string s)
{
    if (!s || !s->data)
        return;
    size_t len = 0;
    if (s->heap)
    {
        assert(s->heap->kind == RT_HEAP_STRING);
        len = rt_heap_len(s->data);
    }
    else
    {
        len = s->literal_len;
    }
    if (len == 0)
        return;
    fwrite(s->data, 1, len, stdout);
}

/**
 * Print a 64-bit integer in decimal form to stdout.
 *
 * @param v Value to print.
 */
void rt_print_i64(int64_t v)
{
    printf("%lld", (long long)v);
}

/**
 * Print a double-precision floating-point number to stdout.
 *
 * @param v Value to print.
 */
void rt_print_f64(double v)
{
    printf("%g", v);
}

/**
 * Read a single line of input from stdin into a runtime string.
 *
 * @return Newly allocated runtime string containing the line without the
 * trailing newline, or NULL on EOF before any characters are read.
 */
rt_string rt_input_line(void)
{
    size_t cap = 1024;
    size_t len = 0;
    char *buf = (char *)rt_alloc(cap);
    for (;;)
    {
        int ch = fgetc(stdin);
        if (ch == EOF)
        {
            if (len == 0)
            {
                free(buf);
                return NULL;
            }
            break;
        }
        if (ch == '\n')
            break;
        if (len + 1 >= cap)
        {
            size_t new_cap = cap * 2;
            char *nbuf = (char *)realloc(buf, new_cap);
            if (!nbuf)
            {
                free(buf);
                rt_trap("out of memory");
                return NULL;
            }
            buf = nbuf;
            cap = new_cap;
        }
        buf[len++] = (char)ch;
    }
    buf[len] = '\0';
    rt_string s = (rt_string)rt_alloc(sizeof(*s));
    char *payload = (char *)rt_heap_alloc(RT_HEAP_STRING, RT_ELEM_NONE, 1, len, len + 1);
    if (!payload)
    {
        free(buf);
        rt_trap("out of memory");
        return NULL;
    }
    memcpy(payload, buf, len + 1);
    s->data = payload;
    rt_heap_hdr_t *hdr = rt_heap_hdr(payload);
    assert(hdr);
    assert(hdr->kind == RT_HEAP_STRING);
    s->heap = hdr;
    s->literal_len = 0;
    s->literal_refs = 0;
    free(buf);
    return s;
}
