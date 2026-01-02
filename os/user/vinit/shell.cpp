/**
 * @file shell.cpp
 * @brief Main shell loop and command dispatch for vinit.
 */
#include "vinit.hpp"

// Paging control (defined in io.cpp)
extern void paging_enable();
extern void paging_disable();

// ANSI escape to set shell foreground color (yellow)
static constexpr const char *SHELL_COLOR = "\033[33m";

void shell_loop()
{
    char line[256];

    // Set shell text color to yellow
    print_str(SHELL_COLOR);
    print_str("\n========================================\n");
    print_str("        ViperOS 0.2.0 Shell\n");
    print_str("========================================\n");
    print_str("Type 'Help' for available commands.\n\n");

    // Enable cursor visibility
    print_str("\x1B[?25h");

    // Initialize current_dir from kernel's CWD
    refresh_current_dir();

    while (true)
    {
        // Shell prompt
        if (current_dir[0] == '/' && current_dir[1] == '\0')
        {
            print_str("SYS:");
        }
        else
        {
            print_str("SYS:");
            print_str(current_dir);
        }
        print_str("> ");

        usize len = readline(line, sizeof(line));
        if (len == 0)
            continue;

        // Add to history
        history_add(line);

        // Check for "read" prefix for paging
        bool do_paging = false;
        char *cmd_line = line;
        if (strcasestart(line, "read "))
        {
            do_paging = true;
            cmd_line = const_cast<char *>(get_args(line, 5));
            if (!cmd_line || *cmd_line == '\0')
            {
                print_str("Read: missing command\n");
                last_rc = RC_ERROR;
                continue;
            }
            paging_enable();
        }

        // Parse and execute command (case-insensitive)
        if (strcaseeq(cmd_line, "help") || strcaseeq(cmd_line, "?"))
        {
            cmd_help();
        }
        else if (strcaseeq(cmd_line, "cls") || strcaseeq(cmd_line, "clear"))
        {
            cmd_cls();
        }
        else if (strcasestart(cmd_line, "echo ") || strcaseeq(cmd_line, "echo"))
        {
            cmd_echo(get_args(cmd_line, 5));
        }
        else if (strcaseeq(cmd_line, "version"))
        {
            cmd_version();
        }
        else if (strcaseeq(cmd_line, "uptime"))
        {
            cmd_uptime();
        }
        else if (strcaseeq(cmd_line, "history"))
        {
            cmd_history();
        }
        else if (strcaseeq(cmd_line, "why"))
        {
            cmd_why();
        }
        else if (strcaseeq(cmd_line, "chdir") || strcasestart(cmd_line, "chdir "))
        {
            cmd_cd(get_args(cmd_line, 6));
        }
        else if (strcaseeq(cmd_line, "cd") || strcasestart(cmd_line, "cd "))
        {
            cmd_cd(get_args(cmd_line, 3));
        }
        else if (strcaseeq(cmd_line, "cwd") || strcaseeq(cmd_line, "pwd"))
        {
            cmd_pwd();
        }
        else if (strcaseeq(cmd_line, "avail"))
        {
            cmd_avail();
        }
        else if (strcaseeq(cmd_line, "status"))
        {
            cmd_status();
        }
        else if (strcaseeq(cmd_line, "servers"))
        {
            cmd_servers(nullptr);
        }
        else if (strcasestart(cmd_line, "servers "))
        {
            cmd_servers(get_args(cmd_line, 8));
        }
        else if (strcasestart(cmd_line, "runfsd "))
        {
            cmd_run_fsd(get_args(cmd_line, 7));
        }
        else if (strcaseeq(cmd_line, "runfsd"))
        {
            print_str("RunFSD: missing program path\n");
            last_rc = RC_ERROR;
        }
        else if (strcasestart(cmd_line, "run "))
        {
            cmd_run(get_args(cmd_line, 4));
        }
        else if (strcaseeq(cmd_line, "run"))
        {
            print_str("Run: missing program path\n");
            last_rc = RC_ERROR;
        }
        else if (strcaseeq(cmd_line, "caps") || strcasestart(cmd_line, "caps "))
        {
            cmd_caps(get_args(cmd_line, 5));
        }
        else if (strcaseeq(cmd_line, "date"))
        {
            cmd_date();
        }
        else if (strcaseeq(cmd_line, "time"))
        {
            cmd_time();
        }
        else if (strcasestart(cmd_line, "assign ") || strcaseeq(cmd_line, "assign"))
        {
            cmd_assign(get_args(cmd_line, 7));
        }
        else if (strcasestart(cmd_line, "path ") || strcaseeq(cmd_line, "path"))
        {
            cmd_path(get_args(cmd_line, 5));
        }
        else if (strcaseeq(cmd_line, "dir") || strcasestart(cmd_line, "dir "))
        {
            cmd_dir(get_args(cmd_line, 4));
        }
        else if (strcaseeq(cmd_line, "list") || strcasestart(cmd_line, "list "))
        {
            cmd_list(get_args(cmd_line, 5));
        }
        else if (strcasestart(cmd_line, "type "))
        {
            cmd_type(get_args(cmd_line, 5));
        }
        else if (strcaseeq(cmd_line, "type"))
        {
            print_str("Type: missing file argument\n");
            last_rc = RC_ERROR;
        }
        else if (strcasestart(cmd_line, "copy ") || strcaseeq(cmd_line, "copy"))
        {
            cmd_copy(get_args(cmd_line, 5));
        }
        else if (strcasestart(cmd_line, "delete ") || strcaseeq(cmd_line, "delete"))
        {
            cmd_delete(get_args(cmd_line, 7));
        }
        else if (strcasestart(cmd_line, "makedir ") || strcaseeq(cmd_line, "makedir"))
        {
            cmd_makedir(get_args(cmd_line, 8));
        }
        else if (strcasestart(cmd_line, "rename ") || strcaseeq(cmd_line, "rename"))
        {
            cmd_rename(get_args(cmd_line, 7));
        }
        else if (strcasestart(cmd_line, "fetch "))
        {
            cmd_fetch(get_args(cmd_line, 6));
        }
        else if (strcaseeq(cmd_line, "fetch"))
        {
            print_str("Fetch: usage: Fetch <hostname>\n");
            last_rc = RC_ERROR;
        }
        else if (strcaseeq(cmd_line, "endshell") || strcaseeq(cmd_line, "exit") ||
                 strcaseeq(cmd_line, "quit"))
        {
            print_str("Goodbye!\n");
            if (do_paging)
                paging_disable();
            break;
        }
        // Legacy command aliases
        else if (strcaseeq(cmd_line, "ls") || strcasestart(cmd_line, "ls "))
        {
            print_str("Note: Use 'Dir' or 'List' instead of 'ls'\n");
            cmd_dir(get_args(cmd_line, 3));
        }
        else if (strcasestart(cmd_line, "cat "))
        {
            print_str("Note: Use 'Type' instead of 'cat'\n");
            cmd_type(get_args(cmd_line, 4));
        }
        else
        {
            print_str("Unknown command: ");
            print_str(cmd_line);
            print_str("\nType 'Help' for available commands.\n");
            last_rc = RC_WARN;
            last_error = "Unknown command";
        }

        if (do_paging)
        {
            paging_disable();
        }
    }
}
