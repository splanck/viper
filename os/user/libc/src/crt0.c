/**
 * @file crt0.c
 * @brief C runtime startup for ViperOS userspace.
 *
 * Provides the _start entry point that initializes the runtime
 * and calls main().
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
