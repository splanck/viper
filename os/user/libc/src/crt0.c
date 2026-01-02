//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/crt0.c
// Purpose: C runtime startup code for ViperOS userspace programs.
// Key invariants: _start clears BSS, parses args, calls main, then _exit.
// Ownership/Lifetime: Library; linked as program entry point.
// Links: user/libc/include/stdlib.h (for _exit)
//
//===----------------------------------------------------------------------===//

/**
 * @file crt0.c
 * @brief C runtime startup code for ViperOS userspace programs.
 *
 * @details
 * This file provides the C runtime startup for all ViperOS programs:
 *
 * - _start: Entry point called by the kernel after loading
 * - clear_bss: Zero-initialize the BSS section
 * - parse_args: Parse command-line arguments from kernel
 *
 * The startup sequence:
 * 1. Kernel loads program and jumps to _start
 * 2. _start clears BSS section to zero
 * 3. parse_args() retrieves argv from kernel via SYS_GET_ARGS
 * 4. main(argc, argv) is called
 * 5. _exit() is called with main's return value
 *
 * Arguments are stored in static buffers (max 32 args, 512 bytes).
 */

/* Forward declaration of main */
extern int main(int argc, char *argv[]);

/* Exit syscall */
extern void _exit(int status);

/* BSS section symbols from linker script */
extern char __bss_start[];
extern char __bss_end[];

/* Syscall number for get_args */
#define SYS_GET_ARGS 0xA6

/* Max arguments and arg buffer size */
#define MAX_ARGS 32
#define ARGS_BUF_SIZE 512

/* Static storage for argv array and argument strings */
static char *g_argv[MAX_ARGS + 1];
static char g_args_buf[ARGS_BUF_SIZE];
static char g_progname[] = "program";

/**
 * @brief Make a syscall with 2 arguments.
 * Returns x1 (result) if x0 (error) is 0, otherwise returns negative error.
 */
static inline long syscall2(long num, long a0, long a1)
{
    register long x8 __asm__("x8") = num;
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    __asm__ volatile("svc #0" : "+r"(x0), "+r"(x1) : "r"(x8) : "memory");
    /* x0 = error code, x1 = result. Return result if success, else error */
    if (x0 != 0)
        return x0; /* Return negative error code */
    return x1;     /* Return result */
}

/**
 * @brief Clear BSS section.
 */
static void clear_bss(void)
{
    for (char *p = __bss_start; p < __bss_end; p++)
    {
        *p = 0;
    }
}

/**
 * @brief Parse args string into argc/argv.
 *
 * Splits the args buffer on spaces, handling the format:
 * "arg1 arg2 arg3" -> argv[1]="arg1", argv[2]="arg2", argv[3]="arg3"
 *
 * argv[0] is always set to "program".
 *
 * @return argc (number of arguments including program name)
 */
static int parse_args(void)
{
    int argc = 0;

    /* argv[0] is always the program name */
    g_argv[argc++] = g_progname;

    /* Get args from kernel */
    long result = syscall2(SYS_GET_ARGS, (long)g_args_buf, ARGS_BUF_SIZE - 1);
    if (result <= 0)
    {
        /* No args or error */
        g_argv[argc] = (char *)0;
        return argc;
    }

    /* Null-terminate */
    g_args_buf[result] = '\0';

    /* Parse args - split on spaces */
    char *p = g_args_buf;

    while (*p && argc < MAX_ARGS)
    {
        /* Skip leading spaces */
        while (*p == ' ')
            p++;

        if (*p == '\0')
            break;

        /* Start of argument */
        g_argv[argc++] = p;

        /* Find end of argument */
        while (*p && *p != ' ')
            p++;

        /* Null-terminate this argument */
        if (*p)
        {
            *p++ = '\0';
        }
    }

    /* Null-terminate argv array */
    g_argv[argc] = (char *)0;

    return argc;
}

/**
 * @brief C runtime entry point.
 *
 * Called by the kernel after loading the program.
 * Clears BSS, parses command line args, then calls main.
 */
void _start(void)
{
    /* Clear BSS */
    clear_bss();

    /* Parse command line arguments */
    int argc = parse_args();

    /* Call main */
    int ret = main(argc, g_argv);

    /* Exit with return value from main */
    _exit(ret);

    /* Should never reach here */
    for (;;)
        ;
}
