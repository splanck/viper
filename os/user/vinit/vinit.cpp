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

// =============================================================================
// Server State Tracking (for crash isolation and restart)
// =============================================================================

struct ServerInfo
{
    const char *name;       // Display name (e.g., "blkd")
    const char *path;       // Executable path
    const char *assign;     // Assign name (e.g., "BLKD")
    i64 pid;                // Process ID (0 = not running)
    bool available;         // True if server registered successfully
};

static ServerInfo g_servers[] = {
    {"blkd", "/c/blkd.elf", "BLKD", 0, false},
    {"netd", "/c/netd.elf", "NETD", 0, false},
    {"fsd", "/c/fsd.elf", "FSD", 0, false},
};

static constexpr usize SERVER_COUNT = sizeof(g_servers) / sizeof(g_servers[0]);
static u32 g_device_root = 0xFFFFFFFFu;
static bool g_have_device_root = false;

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
 * @param out_bootstrap_send Output: parent bootstrap channel send handle (optional).
 * @return PID on success, negative error code on failure.
 */
static i64 spawn_server(const char *path, const char *name, u32 *out_bootstrap_send = nullptr)
{
    u64 pid = 0;
    u64 tid = 0;
    i64 err = sys::spawn(path, nullptr, &pid, &tid, nullptr, out_bootstrap_send);

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

static bool find_device_root_cap(u32 *out_handle)
{
    if (!out_handle)
        return false;

    CapListEntry entries[32];
    i32 n = sys::cap_list(entries, 32);
    if (n < 0)
    {
        return false;
    }

    for (i32 i = 0; i < n; i++)
    {
        if (entries[i].kind == CAP_KIND_DEVICE)
        {
            *out_handle = entries[i].handle;
            return true;
        }
    }

    return false;
}

static void send_server_device_caps(u32 bootstrap_send, u32 device_root)
{
    if (bootstrap_send == 0xFFFFFFFFu)
        return;

    // Derive a transferable device capability for the server.
    u32 rights = CAP_RIGHT_DEVICE_ACCESS | CAP_RIGHT_IRQ_ACCESS | CAP_RIGHT_DMA_ACCESS |
                 CAP_RIGHT_TRANSFER;
    i32 derived = sys::cap_derive(device_root, rights);
    if (derived < 0)
    {
        return;
    }

    u32 handle_to_send = static_cast<u32>(derived);
    u8 dummy = 0;
    bool sent = false;
    for (u32 i = 0; i < 2000; i++)
    {
        i64 err =
            sys::channel_send(static_cast<i32>(bootstrap_send), &dummy, 1, &handle_to_send, 1);
        if (err == 0)
        {
            sent = true;
            break;
        }
        if (err == VERR_WOULD_BLOCK)
        {
            sys::yield();
            continue;
        }
        break;
    }

    // Always close the bootstrap send endpoint; the child owns the recv endpoint.
    sys::channel_close(static_cast<i32>(bootstrap_send));

    // If we failed to send, revoke the derived cap so we don't leak it in vinit.
    if (!sent)
    {
        sys::cap_revoke(handle_to_send);
    }
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
 * @brief Check if a server process is still running.
 */
static bool is_server_running(i64 pid)
{
    if (pid <= 0)
        return false;

    TaskInfo tasks[32];
    i32 count = sys::task_list(tasks, 32);
    if (count < 0)
        return false;

    for (i32 i = 0; i < count; i++)
    {
        if (tasks[i].id == static_cast<u32>(pid))
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Start a specific server by index.
 * @return true if server started and registered successfully.
 */
static bool start_server_by_index(usize idx)
{
    if (idx >= SERVER_COUNT)
        return false;

    ServerInfo &srv = g_servers[idx];

    // Check if executable exists
    sys::Stat st;
    if (sys::stat(srv.path, &st) != 0)
    {
        print_str("[vinit] ");
        print_str(srv.name);
        print_str(": not found\n");
        return false;
    }

    // fsd depends on blkd
    if (idx == 2 && !g_servers[0].available)
    {
        print_str("[vinit] ");
        print_str(srv.name);
        print_str(": requires blkd\n");
        return false;
    }

    u32 bootstrap_send = 0xFFFFFFFFu;
    srv.pid = spawn_server(srv.path, srv.name, &bootstrap_send);
    if (g_have_device_root)
    {
        send_server_device_caps(bootstrap_send, g_device_root);
    }

    if (srv.pid > 0 && wait_for_service(srv.assign, 1000))
    {
        print_str("[vinit] ");
        print_str(srv.assign);
        print_str(": ready\n");
        srv.available = true;
        return true;
    }

    srv.available = false;
    return false;
}

/**
 * @brief Restart a crashed server.
 * @param name Server name ("blkd", "netd", "fsd").
 * @return true on success.
 */
bool restart_server(const char *name)
{
    for (usize i = 0; i < SERVER_COUNT; i++)
    {
        if (streq(g_servers[i].name, name))
        {
            g_servers[i].pid = 0;
            g_servers[i].available = false;
            return start_server_by_index(i);
        }
    }
    return false;
}

/**
 * @brief Get server status for display.
 */
void get_server_status(usize idx, const char **name, const char **assign, i64 *pid, bool *running,
                       bool *available)
{
    if (idx >= SERVER_COUNT)
        return;

    const ServerInfo &srv = g_servers[idx];
    *name = srv.name;
    *assign = srv.assign;
    *pid = srv.pid;
    *running = is_server_running(srv.pid);
    *available = srv.available;

    // Update availability if process died
    if (!*running && srv.available)
    {
        g_servers[idx].available = false;
    }
}

usize get_server_count()
{
    return SERVER_COUNT;
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
    // Check if any server ELFs exist
    sys::Stat st;
    bool have_any = false;
    for (usize i = 0; i < SERVER_COUNT; i++)
    {
        if (sys::stat(g_servers[i].path, &st) == 0)
        {
            have_any = true;
            break;
        }
    }

    if (!have_any)
    {
        print_str("[vinit] No microkernel servers found, using kernel services\n\n");
        return;
    }

    print_str("[vinit] Starting microkernel servers...\n");
    print_str("[vinit] (Note: servers require dedicated VirtIO devices)\n");

    // Find device root capability and save it for later restarts
    g_have_device_root = find_device_root_cap(&g_device_root);

    u32 registered = 0;

    // Start servers in order (blkd, netd, fsd)
    for (usize i = 0; i < SERVER_COUNT; i++)
    {
        if (start_server_by_index(i))
        {
            registered++;
        }
    }

    if (registered == 0)
    {
        print_str("[vinit] Servers unavailable, using kernel services\n");
    }
    print_str("\n");

    // When fsd is available, run a small smoke test program that exercises
    // libc file ops routed through fsd (and verifies the kernel VFS can't see
    // the created file).
    if (g_servers[2].available) // fsd
    {
        sys::Stat st;
        if (sys::stat("/c/fsd_smoke.elf", &st) == 0)
        {
            print_str("[vinit] Running fsd_smoke...\n");

            u64 pid = 0;
            u64 tid = 0;
            u32 bootstrap_send = 0xFFFFFFFFu;
            i64 err = sys::spawn("/c/fsd_smoke.elf",
                                 "fsd_smoke",
                                 &pid,
                                 &tid,
                                 nullptr,
                                 &bootstrap_send);

            if (bootstrap_send != 0xFFFFFFFFu)
            {
                sys::channel_close(static_cast<i32>(bootstrap_send));
            }

            if (err < 0)
            {
                print_str("[vinit] fsd_smoke: spawn failed (error ");
                put_num(err);
                print_str(")\n\n");
            }
            else
            {
                i32 status = 0;
                (void)sys::waitpid(static_cast<i64>(pid), &status);
                print_str("[vinit] fsd_smoke: exit ");
                put_num(static_cast<i64>(status));
                print_str("\n\n");
            }
        }
    }

    // When netd is available, run a small smoke test program that issues an IPC request
    // to NETD and validates the basic response path.
    if (g_servers[1].available) // netd
    {
        sys::Stat st;
        if (sys::stat("/c/netd_smoke.elf", &st) == 0)
        {
            print_str("[vinit] Running netd_smoke...\n");

            u64 pid = 0;
            u64 tid = 0;
            u32 bootstrap_send = 0xFFFFFFFFu;
            i64 err = sys::spawn("/c/netd_smoke.elf",
                                 "netd_smoke",
                                 &pid,
                                 &tid,
                                 nullptr,
                                 &bootstrap_send);

            if (bootstrap_send != 0xFFFFFFFFu)
            {
                sys::channel_close(static_cast<i32>(bootstrap_send));
            }

            if (err < 0)
            {
                print_str("[vinit] netd_smoke: spawn failed (error ");
                put_num(err);
                print_str(")\n\n");
            }
            else
            {
                i32 status = 0;
                (void)sys::waitpid(static_cast<i64>(pid), &status);
                print_str("[vinit] netd_smoke: exit ");
                put_num(static_cast<i64>(status));
                print_str("\n\n");
            }
        }
    }

    // Run TLS smoke test if available (tests user-space TLS library API)
    {
        sys::Stat st;
        if (sys::stat("/c/tls_smoke.elf", &st) == 0)
        {
            print_str("[vinit] Running tls_smoke...\n");

            u64 pid = 0;
            u64 tid = 0;
            u32 bootstrap_send = 0xFFFFFFFFu;
            i64 err = sys::spawn("/c/tls_smoke.elf",
                                 "tls_smoke",
                                 &pid,
                                 &tid,
                                 nullptr,
                                 &bootstrap_send);

            if (bootstrap_send != 0xFFFFFFFFu)
            {
                sys::channel_close(static_cast<i32>(bootstrap_send));
            }

            if (err < 0)
            {
                print_str("[vinit] tls_smoke: spawn failed (error ");
                put_num(err);
                print_str(")\n\n");
            }
            else
            {
                i32 status = 0;
                (void)sys::waitpid(static_cast<i64>(pid), &status);
                print_str("[vinit] tls_smoke: exit ");
                put_num(static_cast<i64>(status));
                print_str("\n\n");
            }
        }
    }
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
