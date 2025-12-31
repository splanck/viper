/**
 * @file vinit.cpp
 * @brief ViperOS init process entry point.
 *
 * @details
 * `vinit` is the first user-space process started by the kernel. It provides
 * an interactive shell for debugging and demos.
 *
 * At startup, vinit launches the microkernel user-space servers:
 * - blkd: Block device server (VirtIO-blk driver)
 * - netd: Network server (VirtIO-net driver + TCP/IP stack)
 * - fsd:  Filesystem server (ViperFS) - depends on blkd
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
 * @brief Spawn a server process in the background (don't wait).
 *
 * @param path Path to the server executable.
 * @param name Display name for logging.
 * @return PID on success, negative error code on failure.
 */
static i64 spawn_server(const char *path, const char *name)
{
    u64 pid = 0;
    u64 tid = 0;
    i64 err = sys::spawn(path, nullptr, &pid, &tid, nullptr);

    if (err < 0)
    {
        print_str("[vinit] Failed to start ");
        print_str(name);
        print_str(": error ");
        put_num(err);
        print_str("\n");
        return err;
    }

    print_str("[vinit] Started ");
    print_str(name);
    print_str(" (pid ");
    put_num(static_cast<i64>(pid));
    print_str(")\n");

    return static_cast<i64>(pid);
}

/**
 * @brief Wait for a service to become available via the assign system.
 *
 * @param name Service name (e.g., "BLKD").
 * @param timeout_ms Maximum time to wait in milliseconds.
 * @return true if service is available, false on timeout.
 */
static bool wait_for_service(const char *name, u32 timeout_ms)
{
    u32 waited = 0;
    const u32 interval = 10; // Check every 10ms

    while (waited < timeout_ms)
    {
        u32 handle = 0xFFFFFFFFu;
        if (sys::assign_get(name, &handle) == 0 && handle != 0xFFFFFFFFu)
        {
            // Service is registered, close the handle we just got
            sys::channel_close(static_cast<i32>(handle));
            return true;
        }

        // Yield and wait a bit
        sys::yield();
        waited += interval;
    }

    return false;
}

/**
 * @brief Start all microkernel servers (optional).
 *
 * @details
 * Attempts to start microkernel user-space servers. These servers provide
 * an IPC-based interface to system services:
 * - blkd: Block device access
 * - netd: Network stack
 * - fsd:  Filesystem
 *
 * NOTE: In the current hybrid microkernel model, the kernel already provides
 * these services directly via syscalls. The user-space servers require
 * dedicated hardware (separate VirtIO devices) to function. When hardware
 * is already claimed by the kernel, servers will fail to start - this is
 * expected and the system falls back to kernel-provided services.
 */
static void start_servers()
{
    // Check if server ELFs exist before attempting to start them
    sys::Stat st;
    bool have_blkd = (sys::stat("/c/blkd.elf", &st) == 0);
    bool have_netd = (sys::stat("/c/netd.elf", &st) == 0);
    bool have_fsd = (sys::stat("/c/fsd.elf", &st) == 0);

    if (!have_blkd && !have_netd && !have_fsd)
    {
        print_str("[vinit] No microkernel servers found, using kernel services\n\n");
        return;
    }

    print_str("[vinit] Starting microkernel servers...\n");
    print_str("[vinit] (Note: servers require dedicated VirtIO devices)\n");

    u32 registered = 0;
    bool blkd_ready = false;
    bool netd_ready = false;
    bool fsd_ready = false;

    // Start block device server first (fsd depends on it)
    if (have_blkd)
    {
        i64 blkd_pid = spawn_server("/c/blkd.elf", "blkd");
        if (blkd_pid > 0 && wait_for_service("BLKD", 1000))
        {
            print_str("[vinit] BLKD: ready\n");
            registered++;
            blkd_ready = true;
        }
    }

    // Start network server (independent of block device)
    if (have_netd)
    {
        i64 netd_pid = spawn_server("/c/netd.elf", "netd");
        if (netd_pid > 0 && wait_for_service("NETD", 1000))
        {
            print_str("[vinit] NETD: ready\n");
            registered++;
            netd_ready = true;
        }
    }

    // Start filesystem server (needs blkd)
    if (have_fsd && blkd_ready)
    {
        i64 fsd_pid = spawn_server("/c/fsd.elf", "fsd");
        if (fsd_pid > 0 && wait_for_service("FSD", 1000))
        {
            print_str("[vinit] FSD: ready\n");
            registered++;
            fsd_ready = true;
        }
    }

    (void)netd_ready;
    (void)fsd_ready;

    if (registered == 0)
    {
        print_str("[vinit] Servers unavailable, using kernel services\n");
    }
    print_str("\n");
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

    // Start microkernel servers (blkd, netd, fsd)
    start_servers();

    // Run the shell
    shell_loop();

    print_str("[vinit] EndShell - Shutting down.\n");
    sys::exit(0);
}
