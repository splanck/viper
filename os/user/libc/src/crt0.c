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

/**
 * @brief Clear BSS section.
 */
static void clear_bss(void)
{
    for (char *p = __bss_start; p < __bss_end; p++) {
        *p = 0;
    }
}

/**
 * @brief C runtime entry point.
 *
 * Called by the kernel after loading the program.
 * Clears BSS, then calls main with argc=1, argv={"program"}.
 */
void _start(void)
{
    /* Clear BSS */
    clear_bss();

    /* Simple argv for now - no command line parsing yet */
    static char *argv[] = { "program", (char *)0 };
    int argc = 1;

    /* Call main */
    int ret = main(argc, argv);

    /* Exit with return value from main */
    _exit(ret);

    /* Should never reach here */
    for (;;);
}
