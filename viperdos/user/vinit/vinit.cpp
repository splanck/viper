//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/vinit/vinit.cpp
// Purpose: ViperDOS init process entry point and interactive shell.
// Key invariants: First user-space process; launches display servers.
// Ownership/Lifetime: Long-running init process.
// Links: user/vinit/vinit.hpp
//
//===----------------------------------------------------------------------===//

/**
 * @file vinit.cpp
 * @brief ViperDOS init process entry point.
 *
 * @details
 * `vinit` is the first user-space process started by the kernel. It provides
 * an interactive shell for debugging and demos.
 *
 * At startup, vinit launches display servers for GUI output:
 * - displayd: Framebuffer management
 * - consoled: Console window for shell I/O
 *
 * Storage and network services are provided directly by the kernel in
 * hybrid kernel mode.
 */
#include "vinit.hpp"
#include "../../version.h"

// =============================================================================
// Server State Tracking (for crash isolation and restart)
// =============================================================================

struct ServerInfo {
    const char *name;   // Display name (e.g., "blkd")
    const char *path;   // Executable path
    const char *assign; // Assign name (e.g., "BLKD")
    i64 pid;            // Process ID (0 = not running)
    bool available;     // True if server registered successfully
};

// Two-disk architecture: servers are on system disk (/sys)
// Note: inputd removed - kernel handles keyboard input directly
// Hybrid mode: FS uses kernel VFS, network uses netd (kernel net not implemented)
static ServerInfo g_servers[] = {
    // Display server must start first - GUI apps depend on it
    {"displayd", "/sys/displayd.sys", "DISPLAY", 0, false},
    // Network server - kernel net stack not implemented, use netd
    {"netd", "/sys/netd.sys", "NETD", 0, false},
    // consoled is now launched on-demand from workbench Shell icon
    // {"consoled", "/sys/consoled.sys", "CONSOLED", 0, false},
    // Disabled - using kernel services directly (monolithic mode)
    // {"blkd", "/sys/blkd.sys", "BLKD", 0, false},
    // {"fsd", "/sys/fsd.sys", "FSD", 0, false},
};

static constexpr usize SERVER_COUNT = sizeof(g_servers) / sizeof(g_servers[0]);

static u32 g_device_root = 0xFFFFFFFFu;
static bool g_have_device_root = false;

/**
 * @brief User-space sbrk wrapper for startup malloc test.
 */
static void *vinit_sbrk(long increment) {
    sys::SyscallResult r = sys::syscall1(0x0A, static_cast<u64>(increment));
    if (r.error < 0) {
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
static i64 spawn_server(const char *path, const char *name, u32 *out_bootstrap_send = nullptr) {
    u64 pid = 0;
    u64 tid = 0;
    i64 err = sys::spawn(path, nullptr, &pid, &tid, nullptr, out_bootstrap_send);

    if (err < 0) {
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

static bool find_device_root_cap(u32 *out_handle) {
    if (!out_handle)
        return false;

    CapListEntry entries[32];
    i32 n = sys::cap_list(entries, 32);
    if (n < 0) {
        return false;
    }

    for (i32 i = 0; i < n; i++) {
        if (entries[i].kind == CAP_KIND_DEVICE) {
            *out_handle = entries[i].handle;
            return true;
        }
    }

    return false;
}

static void send_server_device_caps(u32 bootstrap_send, u32 device_root) {
    if (bootstrap_send == 0xFFFFFFFFu)
        return;

    // Derive a transferable device capability for the server.
    u32 rights =
        CAP_RIGHT_DEVICE_ACCESS | CAP_RIGHT_IRQ_ACCESS | CAP_RIGHT_DMA_ACCESS | CAP_RIGHT_TRANSFER;
    i32 derived = sys::cap_derive(device_root, rights);
    if (derived < 0) {
        return;
    }

    u32 handle_to_send = static_cast<u32>(derived);
    u8 dummy = 0;
    bool sent = false;
    for (u32 i = 0; i < 2000; i++) {
        i64 err =
            sys::channel_send(static_cast<i32>(bootstrap_send), &dummy, 1, &handle_to_send, 1);
        if (err == 0) {
            sent = true;
            break;
        }
        if (err == VERR_WOULD_BLOCK) {
            sys::yield();
            continue;
        }
        break;
    }

    // Always close the bootstrap send endpoint; the child owns the recv endpoint.
    sys::channel_close(static_cast<i32>(bootstrap_send));

    // If we failed to send, revoke the derived cap so we don't leak it in vinit.
    if (!sent) {
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
static bool wait_for_service(const char *name, u32 timeout_ms) {
    u32 waited = 0;
    const u32 interval = 10; // Check every 10ms

    while (waited < timeout_ms) {
        u32 handle = 0xFFFFFFFFu;
        if (sys::assign_get(name, &handle) == 0 && handle != 0xFFFFFFFFu) {
            // Service is registered, close the handle we just got
            sys::channel_close(static_cast<i32>(handle));
            return true;
        }

        // Actually sleep for the interval
        sys::sleep(interval);
        waited += interval;
    }

    return false;
}

/**
 * @brief Check if a server process is still running.
 */
static bool is_server_running(i64 pid) {
    if (pid <= 0)
        return false;

    TaskInfo tasks[32];
    i32 count = sys::task_list(tasks, 32);
    if (count < 0)
        return false;

    for (i32 i = 0; i < count; i++) {
        if (tasks[i].id == static_cast<u32>(pid)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Start a specific server by index.
 * @return true if server started and registered successfully.
 */
static bool start_server_by_index(usize idx) {
    if (idx >= SERVER_COUNT)
        return false;

    ServerInfo &srv = g_servers[idx];

    // Check if executable exists
    sys::Stat st;
    if (sys::stat(srv.path, &st) != 0) {
        print_str("[vinit] ");
        print_str(srv.name);
        print_str(": not found\n");
        return false;
    }

    u32 bootstrap_send = 0xFFFFFFFFu;
    srv.pid = spawn_server(srv.path, srv.name, &bootstrap_send);
    if (g_have_device_root) {
        send_server_device_caps(bootstrap_send, g_device_root);
    }

    if (srv.pid > 0 && wait_for_service(srv.assign, 1000)) {
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
bool restart_server(const char *name) {
    for (usize i = 0; i < SERVER_COUNT; i++) {
        if (streq(g_servers[i].name, name)) {
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
void get_server_status(
    usize idx, const char **name, const char **assign, i64 *pid, bool *running, bool *available) {
    if (idx >= SERVER_COUNT)
        return;

    const ServerInfo &srv = g_servers[idx];
    *name = srv.name;
    *assign = srv.assign;
    *pid = srv.pid;
    *running = is_server_running(srv.pid);
    *available = srv.available;

    // Update availability if process died
    if (!*running && srv.available) {
        g_servers[idx].available = false;
    }
}

usize get_server_count() {
    return SERVER_COUNT;
}

/**
 * @brief Start display and console servers.
 *
 * @details
 * In monolithic mode, the kernel provides all storage and network services
 * directly via syscalls. Only display and console servers are started:
 * - displayd: Framebuffer management for GUI mode
 * - consoled: Console window for shell I/O
 *
 * Storage/network servers (blkd, netd, fsd) are disabled - libc routes
 * directly to kernel syscalls for file/network operations.
 */
static void start_servers() {
    // Check if any server ELFs exist
    sys::Stat st;
    bool have_any = false;
    for (usize i = 0; i < SERVER_COUNT; i++) {
        if (sys::stat(g_servers[i].path, &st) == 0) {
            have_any = true;
            break;
        }
    }

    if (!have_any) {
        print_str("[vinit] No display servers found\n\n");
        return;
    }

    print_str("[vinit] Starting display servers...\n");

    // Find device root capability and save it for later restarts
    g_have_device_root = find_device_root_cap(&g_device_root);

    // ==========================================================================
    // PHASE 1: SPAWN ALL SERVERS (loads ELFs while kernel block driver is valid)
    // ==========================================================================
    // Store bootstrap send handles - servers wait for caps before initializing devices
    u32 bootstrap_sends[SERVER_COUNT];
    for (usize i = 0; i < SERVER_COUNT; i++) {
        bootstrap_sends[i] = 0xFFFFFFFFu;
    }

    for (usize i = 0; i < SERVER_COUNT; i++) {
        ServerInfo &srv = g_servers[i];

        // Check if executable exists
        if (sys::stat(srv.path, &st) != 0) {
            print_str("[vinit] ");
            print_str(srv.name);
            print_str(": not found\n");
            continue;
        }

        // Spawn the server (this loads the ELF from disk)
        u64 pid = 0;
        u64 tid = 0;
        i64 err = sys::spawn(srv.path, nullptr, &pid, &tid, nullptr, &bootstrap_sends[i]);

        if (err < 0) {
            print_str("[vinit] Failed to spawn ");
            print_str(srv.name);
            print_str(": error ");
            put_num(err);
            print_str("\n");
            continue;
        }

        srv.pid = static_cast<i64>(pid);
        print_str("[vinit] Spawned ");
        print_str(srv.name);
        print_str(" (pid ");
        put_num(static_cast<i64>(pid));
        print_str(")\n");
    }

    // ==========================================================================
    // PHASE 2: SEND DEVICE CAPABILITIES (unblocks servers to init their devices)
    // ==========================================================================
    // Now that all ELFs are loaded, send device caps to each server.
    // This unblocks blkd which will then reset the VirtIO block device.
    for (usize i = 0; i < SERVER_COUNT; i++) {
        if (bootstrap_sends[i] != 0xFFFFFFFFu && g_have_device_root) {
            send_server_device_caps(bootstrap_sends[i], g_device_root);
        } else if (bootstrap_sends[i] != 0xFFFFFFFFu) {
            // No device root, just close the bootstrap channel
            sys::channel_close(static_cast<i32>(bootstrap_sends[i]));
        }
    }

    // ==========================================================================
    // PHASE 3: WAIT FOR SERVERS TO REGISTER
    // ==========================================================================
    for (usize i = 0; i < SERVER_COUNT; i++) {
        ServerInfo &srv = g_servers[i];
        if (srv.pid <= 0)
            continue;

        u32 timeout = 2000;
        if (wait_for_service(srv.assign, timeout)) {
            srv.available = true;

            // When displayd is ready, disable kernel gcon BEFORE printing
            // to prevent debug text from appearing on the graphical display
            if (streq(srv.assign, "DISPLAY")) {
                sys::gcon_set_gui_mode(true);
            }

            print_str("[vinit] ");
            print_str(srv.assign);
            print_str(": ready\n");
        } else {
            print_str("[vinit] ");
            print_str(srv.assign);
            print_str(": timeout waiting for registration\n");
        }
    }

    // Monolithic mode: kernel provides all storage/network services directly
    print_str("[vinit] Monolithic kernel mode - using kernel services\n");
    print_str("\n");
}

/**
 * @brief Quick malloc test at startup.
 */
static void test_malloc_at_startup() {
    print_str("[vinit] Testing malloc/sbrk...\n");

    void *brk = vinit_sbrk(0);
    print_str("[vinit]   Initial heap: ");
    put_hex(reinterpret_cast<u64>(brk));
    print_str("\n");

    void *ptr = vinit_sbrk(1024);
    if (ptr == reinterpret_cast<void *>(-1)) {
        print_str("[vinit]   ERROR: sbrk(1024) failed!\n");
        return;
    }

    print_str("[vinit]   Allocated 1KB at: ");
    put_hex(reinterpret_cast<u64>(ptr));
    print_str("\n");

    char *cptr = static_cast<char *>(ptr);
    for (int i = 0; i < 1024; i++) {
        cptr[i] = static_cast<char>(i & 0xFF);
    }

    bool ok = true;
    for (int i = 0; i < 1024; i++) {
        if (cptr[i] != static_cast<char>(i & 0xFF)) {
            ok = false;
            break;
        }
    }

    if (ok) {
        print_str("[vinit]   Memory R/W test PASSED\n");
    } else {
        print_str("[vinit]   ERROR: Memory verification FAILED!\n");
    }
}

/**
 * @brief Check if we were spawned by consoled with bootstrap channels.
 *
 * @details
 * When consoled spawns vinit as a child shell, it sends two channel handles
 * via bootstrap:
 * - Input channel (recv endpoint): consoled sends keyboard input
 * - Output channel (send endpoint): vinit sends CON_WRITE for output
 *
 * @param out_input_ch Output: input channel handle
 * @param out_output_ch Output: output channel handle
 * @return true if bootstrap channels were received
 */
static bool try_bootstrap_channels(i32 *out_input_ch, i32 *out_output_ch) {
    constexpr i32 BOOTSTRAP_RECV = 0;
    u8 msg[16];
    u32 handles[4];
    u32 handle_count = 4;

    // Try to receive bootstrap message with handles
    // Wait up to 500ms for consoled to send the channels
    for (u32 attempt = 0; attempt < 500; attempt++) {
        handle_count = 4;
        i64 n = sys::channel_recv(BOOTSTRAP_RECV, msg, sizeof(msg), handles, &handle_count);

        if (n >= 0 && handle_count >= 2) {
            // Received bootstrap channels from consoled
            *out_input_ch = static_cast<i32>(handles[0]);
            *out_output_ch = static_cast<i32>(handles[1]);
            sys::channel_close(BOOTSTRAP_RECV);
            return true;
        }

        if (n == VERR_WOULD_BLOCK) {
            // Wait a bit for consoled to send the channels
            sys::sleep(1);
            continue;
        }

        // Check for invalid handle - means we're init process (no bootstrap channel)
        if (n == VERR_NOT_FOUND || n == VERR_INVALID_HANDLE) {
            // No bootstrap channel - we're the init process
            return false;
        }

        // Other error - keep trying for a bit
        sys::sleep(1);
    }

    return false;
}

/**
 * @brief User-space entry point for the init process.
 */
extern "C" void _start() {
    // Check if we were spawned by consoled with bootstrap channels
    // (console-attached mode for multi-window support)
    i32 input_ch = -1;
    i32 output_ch = -1;

    if (try_bootstrap_channels(&input_ch, &output_ch)) {
        // We're a shell spawned by consoled - run in attached mode
        init_console_attached(input_ch, output_ch);

        // Reset console colors
        print_str(ANSI_RESET);
        flush_console();

        // Run the shell loop directly
        shell_loop();

        // Clean up
        sys::channel_close(input_ch);
        sys::channel_close(output_ch);
        sys::exit(0);
    }

    // Original init process behavior below
    // Reset console colors to white on blue at startup (from viper_colors.h)
    print_str(ANSI_RESET);

    print_str("========================================\n");
    print_str("  " VIPERDOS_VERSION_FULL " - Init Process\n");
    print_str("========================================\n\n");

    print_str("[vinit] Starting ViperDOS...\n");
    print_str("[vinit] Loaded from SYS:viper\\vinit.vpr\n");
    print_str("[vinit] Setting up assigns...\n");
    print_str("  SYS: = D0:\\\n");
    print_str("  C:   = SYS:c\n");
    print_str("  S:   = SYS:s\n");
    print_str("  T:   = SYS:t\n");
    print_str("\n");

    // Run startup malloc test
    test_malloc_at_startup();

    // Start display servers (consoled, displayd)
    start_servers();

    // Give displayd time to fully initialize before starting workbench
    // This prevents race conditions where workbench connects before displayd is ready
    sys::sleep(100);

    // Start the Workbench desktop environment
    print_str("[vinit] Starting Workbench desktop...\n");
    u64 wb_pid = 0;
    u64 wb_tid = 0;
    i64 wb_err = sys::spawn("/sys/workbench.sys", nullptr, &wb_pid, &wb_tid, nullptr, nullptr);

    if (wb_err == 0) {
        print_str("[vinit] Workbench started (pid=");
        put_hex(static_cast<u32>(wb_pid));
        print_str(")\n");

        // Give workbench time to initialize and take over the display
        // This prevents console text from flashing on screen
        sys::sleep(200);

        print_str("[vinit] Desktop ready - click Shell icon to start console\n");

        // With the new multi-shell architecture, each consoled spawns its own shell
        // process via bootstrap channels. The init process no longer needs to run
        // a shell - it just waits for the system to shut down.
        while (true) {
            sys::sleep(1000);
        }
    } else {
        print_str("[vinit] Workbench failed to start, falling back to shell\n");
        // Fall back to text shell
        shell_loop();
    }

    print_str("[vinit] EndShell - Shutting down.\n");
    sys::exit(0);
}
