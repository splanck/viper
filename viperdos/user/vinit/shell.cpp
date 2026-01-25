//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file shell.cpp
 * @brief Main shell loop and command dispatch for vinit.
 *
 * This file implements the main command shell for ViperDOS, including
 * the read-eval-print loop and command parsing/dispatch.
 *
 * ## Shell Loop
 *
 * The shell_loop() function is the core of the interactive shell:
 * 1. Display prompt (e.g., "SYS:/> ")
 * 2. Read a line of input via readline()
 * 3. Parse command and arguments
 * 4. Dispatch to appropriate cmd_* handler
 * 5. Repeat
 *
 * ## Command Format
 *
 * Commands are case-insensitive. Arguments are separated by whitespace.
 *
 * ```
 * COMMAND [argument1] [argument2] ...
 * ```
 *
 * ## Built-in Commands
 *
 * | Command   | Description                    | Handler        |
 * |-----------|--------------------------------|----------------|
 * | CD        | Change directory               | cmd_cd()       |
 * | PWD       | Print working directory        | cmd_pwd()      |
 * | Dir       | List directory (compact)       | cmd_dir()      |
 * | List      | List directory (detailed)      | cmd_list()     |
 * | Type      | Display file contents          | cmd_type()     |
 * | Copy      | Copy a file                    | cmd_copy()     |
 * | Delete    | Delete a file                  | cmd_delete()   |
 * | MakeDir   | Create directory               | cmd_makedir()  |
 * | Rename    | Rename file/directory          | cmd_rename()   |
 * | Run       | Execute a program              | cmd_run()      |
 * | Assign    | List/manage assigns            | cmd_assign()   |
 * | Path      | Resolve assign path            | cmd_path()     |
 * | Fetch     | HTTP/HTTPS client              | cmd_fetch()    |
 * | Help      | Show help information          | (inline)       |
 * | Ver       | Show version                   | (inline)       |
 * | Exit      | Exit shell (reboot)            | (inline)       |
 *
 * ## Prompt Format
 *
 * The prompt shows the current working directory prefixed with "SYS:":
 * - Root: `SYS:> `
 * - Directory: `SYS:/c/games> `
 *
 * @see vinit.hpp for global state
 * @see cmd_fs.cpp for filesystem commands
 * @see cmd_misc.cpp for run, assign, path, fetch
 */
//===----------------------------------------------------------------------===//

#include "vinit.hpp"

// Paging control (defined in io.cpp)
extern void paging_enable();
extern void paging_disable();

// ANSI escape to reset to default colors (from centralized viper_colors.h)
static constexpr const char *SHELL_COLOR = ANSI_RESET;

void shell_loop() {
    char line[256];

    // Set shell text color to white
    print_str(SHELL_COLOR);
    print_str("\nViperDOS Shell\n\n");

    // Enable cursor visibility
    print_str("\x1B[?25h");

    // Initialize current_dir from kernel's CWD
    refresh_current_dir();

    while (true) {
        // Shell prompt
        if (current_dir[0] == '/' && current_dir[1] == '\0') {
            print_str("SYS:");
        } else {
            print_str("SYS:");
            print_str(current_dir);
        }
        print_str("> ");
        flush_console();

        usize len = readline(line, sizeof(line));
        if (len == 0)
            continue;

        // Add to history
        history_add(line);

        // Check for "read" prefix for paging
        bool do_paging = false;
        char *cmd_line = line;
        if (strcasestart(line, "read ")) {
            do_paging = true;
            cmd_line = const_cast<char *>(get_args(line, 5));
            if (!cmd_line || *cmd_line == '\0') {
                print_str("Read: missing command\n");
                last_rc = RC_ERROR;
                continue;
            }
            paging_enable();
        }

        // Parse and execute command (case-insensitive)
        if (strcaseeq(cmd_line, "help") || strcaseeq(cmd_line, "?")) {
            cmd_help();
        } else if (strcaseeq(cmd_line, "cls") || strcaseeq(cmd_line, "clear")) {
            cmd_cls();
        } else if (strcasestart(cmd_line, "echo ") || strcaseeq(cmd_line, "echo")) {
            cmd_echo(get_args(cmd_line, 5));
        } else if (strcaseeq(cmd_line, "version")) {
            cmd_version();
        } else if (strcaseeq(cmd_line, "uptime")) {
            cmd_uptime();
        } else if (strcaseeq(cmd_line, "history")) {
            cmd_history();
        } else if (strcaseeq(cmd_line, "why")) {
            cmd_why();
        } else if (strcaseeq(cmd_line, "chdir") || strcasestart(cmd_line, "chdir ")) {
            cmd_cd(get_args(cmd_line, 6));
        } else if (strcaseeq(cmd_line, "cd") || strcasestart(cmd_line, "cd ")) {
            cmd_cd(get_args(cmd_line, 3));
        } else if (strcaseeq(cmd_line, "cwd") || strcaseeq(cmd_line, "pwd")) {
            cmd_pwd();
        } else if (strcaseeq(cmd_line, "avail")) {
            cmd_avail();
        } else if (strcaseeq(cmd_line, "status")) {
            cmd_status();
        } else if (strcaseeq(cmd_line, "servers")) {
            cmd_servers(nullptr);
        } else if (strcasestart(cmd_line, "servers ")) {
            cmd_servers(get_args(cmd_line, 8));
        } else if (strcasestart(cmd_line, "run ")) {
            cmd_run(get_args(cmd_line, 4));
        } else if (strcaseeq(cmd_line, "run")) {
            print_str("Run: missing program path\n");
            last_rc = RC_ERROR;
        } else if (strcaseeq(cmd_line, "caps") || strcasestart(cmd_line, "caps ")) {
            cmd_caps(get_args(cmd_line, 5));
        } else if (strcaseeq(cmd_line, "date")) {
            cmd_date();
        } else if (strcaseeq(cmd_line, "time")) {
            cmd_time();
        } else if (strcasestart(cmd_line, "assign ") || strcaseeq(cmd_line, "assign")) {
            cmd_assign(get_args(cmd_line, 7));
        } else if (strcasestart(cmd_line, "path ") || strcaseeq(cmd_line, "path")) {
            cmd_path(get_args(cmd_line, 5));
        } else if (strcaseeq(cmd_line, "dir") || strcasestart(cmd_line, "dir ")) {
            cmd_dir(get_args(cmd_line, 4));
        } else if (strcaseeq(cmd_line, "list") || strcasestart(cmd_line, "list ")) {
            cmd_list(get_args(cmd_line, 5));
        } else if (strcasestart(cmd_line, "type ")) {
            cmd_type(get_args(cmd_line, 5));
        } else if (strcaseeq(cmd_line, "type")) {
            print_str("Type: missing file argument\n");
            last_rc = RC_ERROR;
        } else if (strcasestart(cmd_line, "copy ") || strcaseeq(cmd_line, "copy")) {
            cmd_copy(get_args(cmd_line, 5));
        } else if (strcasestart(cmd_line, "delete ") || strcaseeq(cmd_line, "delete")) {
            cmd_delete(get_args(cmd_line, 7));
        } else if (strcasestart(cmd_line, "makedir ") || strcaseeq(cmd_line, "makedir")) {
            cmd_makedir(get_args(cmd_line, 8));
        } else if (strcasestart(cmd_line, "rename ") || strcaseeq(cmd_line, "rename")) {
            cmd_rename(get_args(cmd_line, 7));
        } else if (strcasestart(cmd_line, "fetch ")) {
            cmd_fetch(get_args(cmd_line, 6));
        } else if (strcaseeq(cmd_line, "fetch")) {
            print_str("Fetch: usage: Fetch <hostname>\n");
            last_rc = RC_ERROR;
        } else if (strcaseeq(cmd_line, "endshell") || strcaseeq(cmd_line, "exit") ||
                   strcaseeq(cmd_line, "quit")) {
            print_str("Goodbye!\n");
            if (do_paging)
                paging_disable();
            break;
        }
        // Legacy command aliases
        else if (strcaseeq(cmd_line, "ls") || strcasestart(cmd_line, "ls ")) {
            print_str("Note: Use 'Dir' or 'List' instead of 'ls'\n");
            cmd_dir(get_args(cmd_line, 3));
        } else if (strcasestart(cmd_line, "cat ")) {
            print_str("Note: Use 'Type' instead of 'cat'\n");
            cmd_type(get_args(cmd_line, 4));
        } else {
            print_str("Unknown command: ");
            print_str(cmd_line);
            print_str("\nType 'Help' for available commands.\n");
            last_rc = RC_WARN;
            last_error = "Unknown command";
        }

        if (do_paging) {
            paging_disable();
        }
    }
}
