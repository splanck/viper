#include "../include/errno.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"

/* Simple errno storage - one per process for now (not thread-safe) */
static int __errno_value = 0;

int *__errno_location(void)
{
    return &__errno_value;
}

/* Assert failure handler */
void __assert_fail(const char *expr, const char *file, int line, const char *func)
{
    fprintf(stderr, "Assertion failed: %s, file %s, line %d", expr, file, line);
    if (func)
    {
        fprintf(stderr, ", function %s", func);
    }
    fprintf(stderr, "\n");
    abort();
}
