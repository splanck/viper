// File: runtime/rt_io.c
// Purpose: Implements I/O utilities and trap handling for the BASIC runtime.
// Key invariants: Output routines do not append newlines unless specified.
// Ownership/Lifetime: Caller manages strings passed to printing routines.
// Links: docs/codemap.md

#include "rt_internal.h"
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
    if (msg)
        fprintf(stderr, "runtime trap: %s\n", msg);
    else
        fprintf(stderr, "runtime trap\n");
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
    if (s && s->data)
        fwrite(s->data, 1, (size_t)s->size, stdout);
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
    s->refcnt = 1;
    s->size = (int64_t)len;
    s->capacity = s->size;
    char *data = (char *)rt_alloc(len + 1);
    memcpy(data, buf, len + 1);
    s->data = data;
    free(buf);
    return s;
}
