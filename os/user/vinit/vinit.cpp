/**
 * @file vinit.cpp
 * @brief ViperOS init process entry point.
 *
 * @details
 * `vinit` is the first user-space process started by the kernel. It provides
 * an interactive shell for debugging and demos.
 */
#include "vinit.hpp"

/**
 * @brief User-space sbrk wrapper for startup malloc test.
 */
static void *vinit_sbrk(long increment)
{
    sys::SyscallResult r = sys::syscall1(0x0A, static_cast<u64>(increment));
    if (r.error < 0)
    {
        return reinterpret_cast<void *>(-1);
    }
    return reinterpret_cast<void *>(r.val0);
}

/**
 * @brief Quick malloc test at startup.
 */
static void test_malloc_at_startup()
{
    print_str("[vinit] Testing malloc/sbrk...\n");

    void *brk = vinit_sbrk(0);
    print_str("[vinit]   Initial heap: ");
    put_hex(reinterpret_cast<u64>(brk));
    print_str("\n");

    void *ptr = vinit_sbrk(1024);
    if (ptr == reinterpret_cast<void *>(-1))
    {
        print_str("[vinit]   ERROR: sbrk(1024) failed!\n");
        return;
    }

    print_str("[vinit]   Allocated 1KB at: ");
    put_hex(reinterpret_cast<u64>(ptr));
    print_str("\n");

    char *cptr = static_cast<char *>(ptr);
    for (int i = 0; i < 1024; i++)
    {
        cptr[i] = static_cast<char>(i & 0xFF);
    }

    bool ok = true;
    for (int i = 0; i < 1024; i++)
    {
        if (cptr[i] != static_cast<char>(i & 0xFF))
        {
            ok = false;
            break;
        }
    }

    if (ok)
    {
        print_str("[vinit]   Memory R/W test PASSED\n");
    }
    else
    {
        print_str("[vinit]   ERROR: Memory verification FAILED!\n");
    }
}

/**
 * @brief User-space entry point for the init process.
 */
extern "C" void _start()
{
    print_str("========================================\n");
    print_str("  ViperOS 0.2.0 - Init Process\n");
    print_str("========================================\n\n");

    print_str("[vinit] Starting ViperOS...\n");
    print_str("[vinit] Loaded from SYS:viper\\vinit.vpr\n");
    print_str("[vinit] Setting up assigns...\n");
    print_str("  SYS: = D0:\\\n");
    print_str("  C:   = SYS:c\n");
    print_str("  S:   = SYS:s\n");
    print_str("  T:   = SYS:t\n");
    print_str("\n");

    // Run startup malloc test
    test_malloc_at_startup();

    // Run the shell
    shell_loop();

    print_str("[vinit] EndShell - Shutting down.\n");
    sys::exit(0);
}
