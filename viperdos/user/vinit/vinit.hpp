/**
 * @file vinit.hpp
 * @brief Shared declarations for the ViperDOS init process (vinit).
 */
#pragma once

#include "../include/viper_colors.h"
#include "../syscall.hpp"

// =============================================================================
// String Helpers
// =============================================================================

usize strlen(const char *s);
bool streq(const char *a, const char *b);
bool strstart(const char *s, const char *prefix);
bool strcaseeq(const char *a, const char *b);
bool strcasestart(const char *s, const char *prefix);

// =============================================================================
// Console Output
// =============================================================================

/// Initialize connection to console server (consoled).
/// Must be called after servers are started but before shell_loop().
bool init_console();

void print_str(const char *s);
void print_char(char c);
void flush_console();
void put_num(i64 n);
void put_hex(u32 n);

// Paging support
void paging_enable();
void paging_disable();
bool page_wait();

// Console input (from consoled)
bool is_console_ready();
i32 getchar_from_console();
i32 try_getchar_from_console();

// =============================================================================
// Return Codes
// =============================================================================

constexpr int RC_OK = 0;
constexpr int RC_WARN = 5;
constexpr int RC_ERROR = 10;
constexpr int RC_FAIL = 20;

extern int last_rc;
extern const char *last_error;

// =============================================================================
// Shell State
// =============================================================================

constexpr usize MAX_PATH_LEN = 256;
constexpr usize MAX_CMD_LEN = 512;
constexpr int SCREEN_HEIGHT = 24;

extern char current_dir[MAX_PATH_LEN];

void refresh_current_dir();

// =============================================================================
// Readline / History
// =============================================================================

constexpr usize HISTORY_SIZE = 16;
constexpr usize HISTORY_LINE_LEN = 256;

usize readline(char *buf, usize maxlen);
void history_add(const char *line);
const char *history_get(usize index);

// =============================================================================
// Shell Commands
// =============================================================================

// System commands
void cmd_help();
void cmd_history();
void cmd_cls();
void cmd_echo(const char *args);
void cmd_version();
void cmd_uptime();
void cmd_why();
void cmd_avail();
void cmd_status();
void cmd_caps(const char *args);
void cmd_date();
void cmd_time();

// Filesystem commands
void cmd_cd(const char *args);
void cmd_pwd();
void cmd_dir(const char *path);
void cmd_list(const char *path);
void cmd_type(const char *path);
void cmd_copy(const char *args);
void cmd_delete(const char *args);
void cmd_makedir(const char *args);
void cmd_rename(const char *args);

// Path utilities
bool normalize_path(const char *path, const char *cwd, char *out, usize out_size);

// Misc commands
void cmd_run(const char *path);
void cmd_assign(const char *args);
void cmd_path(const char *args);
void cmd_fetch(const char *url);

// Server management
void cmd_servers(const char *args);
bool restart_server(const char *name);
usize get_server_count();
void get_server_status(
    usize idx, const char **name, const char **assign, i64 *pid, bool *running, bool *available);

// =============================================================================
// Shell Loop
// =============================================================================

void shell_loop();

// =============================================================================
// Argument Parsing Helpers
// =============================================================================

/**
 * @brief Skip leading whitespace and command name, return pointer to arguments.
 * @param cmd Full command line.
 * @param skip Number of characters to skip (command length + space).
 * @return Pointer to arguments or nullptr if none.
 */
inline const char *get_args(const char *cmd, usize skip) {
    if (strlen(cmd) <= skip)
        return nullptr;
    const char *args = cmd + skip;
    // Skip any extra whitespace
    while (*args == ' ')
        args++;
    return (*args) ? args : nullptr;
}
