//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

namespace consoled {

/// Get the current working directory for the embedded shell.
const char *shell_current_dir();

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

// System commands
void cmd_help();
void cmd_echo(const char *args);
void cmd_version();
void cmd_uptime();
void cmd_why();
void cmd_clear();

// Program execution
void cmd_run(const char *cmdline);

} // namespace consoled
