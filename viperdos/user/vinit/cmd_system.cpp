/**
 * @file cmd_system.cpp
 * @brief System-related shell commands for vinit.
 */
#include "vinit.hpp"
#include "../../version.h"

void cmd_help() {
    print_str("\nViperDOS Shell Commands:\n\n");
    print_str("  chdir [path]   - Change directory (default: /)\n");
    print_str("  cwd            - Print current working directory\n");
    print_str("  Dir [path]     - Brief directory listing\n");
    print_str("  List [path]    - Detailed directory listing\n");
    print_str("  Type <file>    - Display file contents\n");
    print_str("  Copy           - Copy files\n");
    print_str("  Delete         - Delete files/directories\n");
    print_str("  MakeDir        - Create directory\n");
    print_str("  Rename         - Rename files\n");
    print_str("  Cls            - Clear screen\n");
    print_str("  Echo [text]    - Print text\n");
    print_str("  Fetch <url>    - Fetch webpage (HTTP/HTTPS)\n");
    print_str("  Version        - Show system version\n");
    print_str("  Uptime         - Show system uptime\n");
    print_str("  Avail          - Show memory availability\n");
    print_str("  Status         - Show running tasks\n");
    print_str("  Run <path>     - Execute program\n");
    print_str("  RunFSD <path>  - Execute program via fsd (spawn from SHM)\n");
    print_str("  Caps [handle]  - Show capabilities\n");
    print_str("  Date           - Show current date\n");
    print_str("  Time           - Show current time\n");
    print_str("  Assign         - Manage logical devices\n");
    print_str("  Path           - Manage command path\n");
    print_str("  History        - Show command history\n");
    print_str("  Why            - Explain last error\n");
    print_str("  Help           - Show this help\n");
    print_str("  EndShell       - Exit shell\n");
    print_str("\nReturn Codes: OK=0, WARN=5, ERROR=10, FAIL=20\n");
    print_str("\nLine Editing:\n");
    print_str("  Left/Right     - Move cursor\n");
    print_str("  Up/Down        - History navigation\n");
    print_str("  Home/End       - Jump to start/end\n");
    print_str("  Tab            - Command completion\n");
    print_str("  Ctrl+U         - Clear line\n");
    print_str("  Ctrl+K         - Kill to end\n\n");
}

void cmd_history() {
    for (usize i = 0; i < HISTORY_SIZE; i++) {
        const char *hist = history_get(i);
        if (hist) {
            print_str("  ");
            put_num(static_cast<i64>(i + 1));
            print_str("  ");
            print_str(hist);
            print_str("\n");
        }
    }
}

void cmd_cls() {
    print_str("\033[2J\033[H");
    last_rc = RC_OK;
}

void cmd_echo(const char *args) {
    if (args)
        print_str(args);
    print_str("\n");
    last_rc = RC_OK;
}

void cmd_version() {
    print_str(VIPERDOS_VERSION_FULL " (" VIPERDOS_BUILD_DATE ")\n");
    print_str("Platform: AArch64\n");
    last_rc = RC_OK;
}

void cmd_uptime() {
    u64 ms = sys::uptime();
    u64 secs = ms / 1000;
    u64 mins = secs / 60;
    u64 hours = mins / 60;
    u64 days = hours / 24;

    print_str("Uptime: ");
    if (days > 0) {
        put_num(static_cast<i64>(days));
        print_str(" day");
        if (days != 1)
            print_str("s");
        print_str(", ");
    }
    if (hours > 0 || days > 0) {
        put_num(static_cast<i64>(hours % 24));
        print_str(" hour");
        if ((hours % 24) != 1)
            print_str("s");
        print_str(", ");
    }
    put_num(static_cast<i64>(mins % 60));
    print_str(" minute");
    if ((mins % 60) != 1)
        print_str("s");
    print_str(", ");
    put_num(static_cast<i64>(secs % 60));
    print_str(" second");
    if ((secs % 60) != 1)
        print_str("s");
    print_str("\n");
    last_rc = RC_OK;
}

void cmd_why() {
    if (last_rc == RC_OK) {
        print_str("No error.\n");
    } else {
        print_str("Last return code: ");
        put_num(last_rc);
        if (last_error) {
            print_str(" - ");
            print_str(last_error);
        }
        print_str("\n");
    }
}

void cmd_avail() {
    MemInfo info;
    if (sys::mem_info(&info) != 0) {
        print_str("AVAIL: Failed to get memory info\n");
        last_rc = RC_ERROR;
        last_error = "Memory info syscall failed";
        return;
    }

    print_str("\nType      Available         In-Use          Total\n");
    print_str("-------  ----------     ----------     ----------\n");

    u64 free_kb = info.free_bytes / 1024;
    u64 used_kb = info.used_bytes / 1024;
    u64 total_kb = info.total_bytes / 1024;

    print_str("chip     ");
    put_num(static_cast<i64>(free_kb));
    print_str(" K       ");
    put_num(static_cast<i64>(used_kb));
    print_str(" K       ");
    put_num(static_cast<i64>(total_kb));
    print_str(" K\n\n");

    print_str("Memory: ");
    put_num(static_cast<i64>(info.free_pages));
    print_str(" pages free (");
    put_num(static_cast<i64>(info.total_pages));
    print_str(" total, ");
    put_num(static_cast<i64>(info.page_size));
    print_str(" bytes/page)\n");

    last_rc = RC_OK;
}

void cmd_status() {
    TaskInfo tasks[16];
    i32 count = sys::task_list(tasks, 16);

    if (count < 0) {
        print_str("STATUS: Failed to get task list\n");
        last_rc = RC_ERROR;
        last_error = "Task list syscall failed";
        return;
    }

    print_str("\nProcess Status:\n\n");
    print_str("  ID  State     Pri  Name\n");
    print_str("  --  --------  ---  --------------------------------\n");

    for (i32 i = 0; i < count; i++) {
        TaskInfo &t = tasks[i];

        print_str("  ");
        if (t.id < 10)
            print_str(" ");
        if (t.id < 100)
            print_str(" ");
        put_num(t.id);
        print_str("  ");

        switch (t.state) {
            case TASK_STATE_READY:
                print_str("Ready   ");
                break;
            case TASK_STATE_RUNNING:
                print_str("Running ");
                break;
            case TASK_STATE_BLOCKED:
                print_str("Blocked ");
                break;
            case TASK_STATE_EXITED:
                print_str("Exited  ");
                break;
            default:
                print_str("Unknown ");
                break;
        }
        print_str("  ");

        if (t.priority < 10)
            print_str(" ");
        if (t.priority < 100)
            print_str(" ");
        put_num(t.priority);
        print_str("  ");

        print_str(t.name);

        if (t.flags & TASK_FLAG_IDLE)
            print_str(" [idle]");
        if (t.flags & TASK_FLAG_KERNEL)
            print_str(" [kernel]");
        print_str("\n");
    }

    print_str("\n");
    put_num(count);
    print_str(" task");
    if (count != 1)
        print_str("s");
    print_str(" total\n");

    last_rc = RC_OK;
}

void cmd_caps(const char *args) {
    (void)args; // Reserved for future filtering
    i32 count = sys::cap_list(nullptr, 0);
    if (count < 0) {
        print_str("CAPS: Failed to get capability list\n");
        last_rc = RC_ERROR;
        last_error = "Capability list syscall failed";
        return;
    }

    if (count == 0) {
        print_str("No capabilities registered.\n");
        last_rc = RC_OK;
        return;
    }

    CapListEntry caps[32];
    i32 actual = sys::cap_list(caps, 32);

    if (actual < 0) {
        print_str("CAPS: Failed to list capabilities\n");
        last_rc = RC_ERROR;
        return;
    }

    print_str("\nCapability Table:\n\n");
    print_str("  Handle   Kind        Rights       Gen\n");
    print_str("  ------   ---------   ---------    ---\n");

    for (i32 i = 0; i < actual; i++) {
        CapListEntry &c = caps[i];

        print_str("  ");
        put_hex(c.handle);
        print_str("  ");

        const char *kind_name = sys::cap_kind_name(c.kind);
        print_str(kind_name);
        usize klen = strlen(kind_name);
        while (klen < 10) {
            print_char(' ');
            klen++;
        }
        print_str("  ");

        char rights_buf[16];
        sys::cap_rights_str(c.rights, rights_buf, sizeof(rights_buf));
        print_str(rights_buf);
        print_str("    ");

        put_num(c.generation);
        print_str("\n");
    }

    print_str("\n");
    put_num(actual);
    print_str(" capabilit");
    if (actual != 1)
        print_str("ies");
    else
        print_str("y");
    print_str(" total\n");

    last_rc = RC_OK;
}

void cmd_date() {
    print_str("DATE: Date/time not yet available\n");
    last_rc = RC_OK;
}

void cmd_time() {
    print_str("TIME: Date/time not yet available\n");
    last_rc = RC_OK;
}

void cmd_servers(const char *args) {
    // If argument provided, restart that server
    if (args && *args) {
        print_str("Restarting server: ");
        print_str(args);
        print_str("...\n");

        if (restart_server(args)) {
            print_str("Server restarted successfully.\n");
            last_rc = RC_OK;
        } else {
            print_str("SERVERS: Failed to restart server\n");
            last_rc = RC_ERROR;
            last_error = "Server restart failed";
        }
        return;
    }

    // No arguments - show server status
    print_str("\nMicrokernel Server Status:\n\n");
    print_str("  Name   Assign  PID    Running  Available\n");
    print_str("  -----  ------  -----  -------  ---------\n");

    usize count = get_server_count();
    for (usize i = 0; i < count; i++) {
        const char *name = nullptr;
        const char *assign = nullptr;
        i64 pid = 0;
        bool running = false;
        bool available = false;

        get_server_status(i, &name, &assign, &pid, &running, &available);

        print_str("  ");
        print_str(name);
        // Pad to 7 chars
        usize namelen = strlen(name);
        for (usize j = namelen; j < 7; j++)
            print_str(" ");

        print_str(assign);
        print_str("   ");

        if (pid > 0) {
            if (pid < 10)
                print_str("    ");
            else if (pid < 100)
                print_str("   ");
            else if (pid < 1000)
                print_str("  ");
            else if (pid < 10000)
                print_str(" ");
            put_num(pid);
        } else {
            print_str("    -");
        }
        print_str("  ");

        if (running)
            print_str("yes    ");
        else
            print_str("no     ");
        print_str("  ");

        if (available)
            print_str("yes");
        else
            print_str("no");

        print_str("\n");
    }

    print_str("\nUse 'servers <name>' to restart a crashed server.\n");
    last_rc = RC_OK;
}
