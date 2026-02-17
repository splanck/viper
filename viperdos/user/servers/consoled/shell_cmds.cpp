//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file shell_cmds.cpp
 * @brief Shell command implementations for the embedded consoled shell.
 *
 * All commands adapted from vinit's cmd_fs.cpp, cmd_system.cpp, cmd_misc.cpp
 * to use shell_print() for output instead of IPC-based print_str().
 */
//===----------------------------------------------------------------------===//

#include "shell_cmds.hpp"
#include "../../../version.h"
#include "../../syscall.hpp"
#include "embedded_shell.hpp"
#include "shell_io.hpp"

namespace consoled {

// =========================================================================
// Global State
// =========================================================================

static char s_current_dir[256] = "/";
static int s_last_rc = 0;
static const char *s_last_error = nullptr;
static EmbeddedShell *s_shell_instance = nullptr;

void shell_set_instance(EmbeddedShell *shell) {
    s_shell_instance = shell;
}

const char *shell_current_dir() {
    return s_current_dir;
}

// =========================================================================
// Path Helpers
// =========================================================================

static bool is_sys_path(const char *path) {
    if (!path || path[0] != '/')
        return false;
    if (path[1] == 's' && path[2] == 'y' && path[3] == 's') {
        return path[4] == '\0' || path[4] == '/';
    }
    return false;
}

static bool is_root_path(const char *path) {
    return path && path[0] == '/' && path[1] == '\0';
}

static bool normalize_path(const char *path, const char *cwd, char *out, size_t out_size) {
    if (!path || !out || out_size < 2)
        return false;

    char buf[512];
    size_t pos = 0;

    if (path[0] == '/') {
        buf[pos++] = '/';
        path++;
    } else {
        for (size_t i = 0; cwd[i] && pos < sizeof(buf) - 1; i++) {
            buf[pos++] = cwd[i];
        }
        if (pos > 0 && buf[pos - 1] != '/') {
            buf[pos++] = '/';
        }
    }

    while (*path && pos < sizeof(buf) - 1) {
        while (*path == '/')
            path++;
        if (*path == '\0')
            break;

        const char *start = path;
        while (*path && *path != '/')
            path++;
        size_t len = static_cast<size_t>(path - start);

        if (len == 1 && start[0] == '.') {
            continue;
        } else if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (pos > 1) {
                pos--;
                while (pos > 1 && buf[pos - 1] != '/')
                    pos--;
            }
        } else {
            for (size_t i = 0; i < len && pos < sizeof(buf) - 1; i++) {
                buf[pos++] = start[i];
            }
            if (pos < sizeof(buf) - 1) {
                buf[pos++] = '/';
            }
        }
    }

    if (pos > 1 && buf[pos - 1] == '/')
        pos--;
    buf[pos] = '\0';

    for (size_t i = 0; i <= pos && i < out_size; i++) {
        out[i] = buf[i];
    }
    out[out_size - 1] = '\0';
    return true;
}

// =========================================================================
// CD / PWD
// =========================================================================

void cmd_cd(const char *args) {
    const char *path = "/";
    if (args && args[0])
        path = args;

    char normalized[256];
    if (!normalize_path(path, s_current_dir, normalized, sizeof(normalized))) {
        shell_print("CD: invalid path\n");
        s_last_rc = 10;
        s_last_error = "Invalid path";
        return;
    }

    if (is_sys_path(normalized)) {
        int32_t result = sys::chdir(normalized);
        if (result < 0) {
            shell_print("CD: ");
            shell_print(normalized);
            shell_print(": No such directory\n");
            s_last_rc = 10;
            s_last_error = "Directory not found";
            return;
        }
        // Refresh current_dir from kernel
        sys::getcwd(s_current_dir, sizeof(s_current_dir));
    } else if (is_root_path(normalized)) {
        s_current_dir[0] = '/';
        s_current_dir[1] = '\0';
    } else {
        int32_t fd = sys::open(normalized, sys::O_RDONLY);
        if (fd >= 0) {
            sys::close(fd);
            shell_strcpy(s_current_dir, normalized, sizeof(s_current_dir));
        } else {
            shell_print("CD: ");
            shell_print(normalized);
            shell_print(": No such directory\n");
            s_last_rc = 10;
            s_last_error = "Directory not found";
            return;
        }
    }

    s_last_rc = 0;
}

void cmd_pwd() {
    shell_print(s_current_dir);
    shell_print("\n");
    s_last_rc = 0;
}

// =========================================================================
// Dir (compact listing)
// =========================================================================

static void print_dir_entry(const char *name, bool is_dir, size_t *col) {
    char entry[32];
    char *p = entry;

    *p++ = ' ';
    *p++ = ' ';

    const char *n = name;
    size_t namelen = 0;
    while (*n && namelen < 17) {
        *p++ = *n++;
        namelen++;
    }

    if (is_dir && namelen < 17) {
        *p++ = '/';
        namelen++;
    }

    while (namelen < 18) {
        *p++ = ' ';
        namelen++;
    }

    *p = '\0';
    shell_print(entry);

    (*col)++;
    if (*col >= 3) {
        shell_print("\n");
        *col = 0;
    }
}

static void dir_kernel_directory(const char *path, size_t *count, size_t *col) {
    int32_t fd = sys::open(path, sys::O_RDONLY);
    if (fd < 0) {
        shell_print("Dir: cannot open \"");
        shell_print(path);
        shell_print("\"\n");
        return;
    }

    uint8_t buf[4096];
    int64_t bytes;

    while ((bytes = sys::readdir(fd, buf, sizeof(buf))) > 0) {
        size_t offset = 0;
        while (offset < static_cast<size_t>(bytes)) {
            sys::DirEnt *ent = reinterpret_cast<sys::DirEnt *>(buf + offset);
            if (ent->reclen == 0)
                break;

            if (!(ent->name[0] == '.' &&
                  (ent->name[1] == '\0' || (ent->name[1] == '.' && ent->name[2] == '\0')))) {
                bool is_dir = (ent->type == 2);
                print_dir_entry(ent->name, is_dir, col);
                (*count)++;
            }

            offset += ent->reclen;
        }
    }

    sys::close(fd);
}

void cmd_dir(const char *path) {
    if (!path || *path == '\0')
        path = s_current_dir;

    char normalized[256];
    if (!normalize_path(path, s_current_dir, normalized, sizeof(normalized))) {
        shell_print("Dir: invalid path\n");
        s_last_rc = 10;
        return;
    }

    size_t count = 0;
    size_t col = 0;

    if (is_root_path(normalized)) {
        print_dir_entry("sys", true, &col);
        count++;
        dir_kernel_directory("/", &count, &col);
    } else {
        dir_kernel_directory(normalized, &count, &col);
    }

    if (col > 0)
        shell_print("\n");
    shell_put_num(static_cast<int64_t>(count));
    shell_print(" entries\n");

    s_last_rc = 0;
}

// =========================================================================
// List (detailed listing)
// =========================================================================

static void print_list_entry(const char *name, bool is_dir, bool readonly) {
    char line[128];
    char *p = line;

    const char *n = name;
    while (*n && (p - line) < 32)
        *p++ = *n++;

    while ((p - line) < 32)
        *p++ = ' ';

    if (is_dir) {
        const char *dir_marker = "  <dir>    ";
        while (*dir_marker)
            *p++ = *dir_marker++;
    } else {
        const char *spaces = "           ";
        while (*spaces)
            *p++ = *spaces++;
    }

    if (readonly) {
        *p++ = 'r';
        *p++ = '-';
        *p++ = '-';
        *p++ = 'e';
    } else {
        *p++ = 'r';
        *p++ = 'w';
        *p++ = 'e';
        *p++ = 'd';
    }

    *p++ = '\n';
    *p = '\0';

    shell_print(line);
}

static void list_kernel_directory(const char *path,
                                  size_t *file_count,
                                  size_t *dir_count,
                                  bool readonly) {
    int32_t fd = sys::open(path, sys::O_RDONLY);
    if (fd < 0) {
        shell_print("List: cannot open \"");
        shell_print(path);
        shell_print("\"\n");
        return;
    }

    uint8_t buf[4096];
    int64_t bytes;

    while ((bytes = sys::readdir(fd, buf, sizeof(buf))) > 0) {
        size_t offset = 0;
        while (offset < static_cast<size_t>(bytes)) {
            sys::DirEnt *ent = reinterpret_cast<sys::DirEnt *>(buf + offset);
            if (ent->reclen == 0)
                break;

            if (!(ent->name[0] == '.' &&
                  (ent->name[1] == '\0' || (ent->name[1] == '.' && ent->name[2] == '\0')))) {
                bool is_dir = (ent->type == 2);
                print_list_entry(ent->name, is_dir, readonly);

                if (is_dir)
                    (*dir_count)++;
                else
                    (*file_count)++;
            }

            offset += ent->reclen;
        }
    }

    sys::close(fd);
}

void cmd_list(const char *path) {
    if (!path || *path == '\0')
        path = s_current_dir;

    char normalized[256];
    if (!normalize_path(path, s_current_dir, normalized, sizeof(normalized))) {
        shell_print("List: invalid path\n");
        s_last_rc = 10;
        return;
    }

    shell_print("Directory \"");
    shell_print(normalized);
    shell_print("\"\n\n");

    size_t file_count = 0;
    size_t dir_count = 0;

    if (is_root_path(normalized)) {
        print_list_entry("sys", true, true);
        dir_count++;
        list_kernel_directory("/", &file_count, &dir_count, false);
    } else if (is_sys_path(normalized)) {
        list_kernel_directory(normalized, &file_count, &dir_count, true);
    } else {
        list_kernel_directory(normalized, &file_count, &dir_count, false);
    }

    shell_print("\n");
    shell_put_num(static_cast<int64_t>(file_count));
    shell_print(" file");
    if (file_count != 1)
        shell_print("s");
    shell_print(", ");
    shell_put_num(static_cast<int64_t>(dir_count));
    shell_print(" director");
    if (dir_count != 1)
        shell_print("ies");
    else
        shell_print("y");
    shell_print("\n");

    s_last_rc = 0;
}

// =========================================================================
// Type (display file)
// =========================================================================

void cmd_type(const char *path) {
    if (!path || *path == '\0') {
        shell_print("Type: missing file argument\n");
        s_last_rc = 10;
        s_last_error = "Missing filename";
        return;
    }

    char normalized[256];
    if (!normalize_path(path, s_current_dir, normalized, sizeof(normalized))) {
        shell_print("Type: invalid path\n");
        s_last_rc = 10;
        s_last_error = "Invalid path";
        return;
    }

    int32_t fd = sys::open(normalized, sys::O_RDONLY);
    if (fd < 0) {
        shell_print("Type: cannot open \"");
        shell_print(normalized);
        shell_print("\"\n");
        s_last_rc = 10;
        s_last_error = "File not found";
        return;
    }

    char buf[512];
    while (true) {
        int64_t bytes = sys::read(fd, buf, sizeof(buf) - 1);
        if (bytes <= 0)
            break;
        buf[bytes] = '\0';
        shell_print(buf);
    }

    shell_print("\n");
    sys::close(fd);
    s_last_rc = 0;
}

// =========================================================================
// Copy
// =========================================================================

void cmd_copy(const char *args) {
    if (!args || *args == '\0') {
        shell_print("Copy: missing arguments\n");
        shell_print("Usage: Copy <source> <dest>\n");
        s_last_rc = 10;
        s_last_error = "Missing arguments";
        return;
    }

    char source[256], dest[256];
    const char *p = args;
    int i = 0;

    while (*p && *p != ' ' && i < 255)
        source[i++] = *p++;
    source[i] = '\0';

    while (*p == ' ')
        p++;
    if (shell_strstart(p, "TO ") || shell_strstart(p, "to "))
        p += 3;
    while (*p == ' ')
        p++;

    i = 0;
    while (*p && *p != ' ' && i < 255)
        dest[i++] = *p++;
    dest[i] = '\0';

    if (dest[0] == '\0') {
        shell_print("Copy: missing destination\n");
        s_last_rc = 10;
        return;
    }

    char norm_src[256], norm_dst[256];
    if (!normalize_path(source, s_current_dir, norm_src, sizeof(norm_src))) {
        shell_print("Copy: invalid source path\n");
        s_last_rc = 10;
        return;
    }
    if (!normalize_path(dest, s_current_dir, norm_dst, sizeof(norm_dst))) {
        shell_print("Copy: invalid destination path\n");
        s_last_rc = 10;
        return;
    }

    int32_t src_fd = sys::open(norm_src, sys::O_RDONLY);
    if (src_fd < 0) {
        shell_print("Copy: cannot open \"");
        shell_print(norm_src);
        shell_print("\"\n");
        s_last_rc = 10;
        return;
    }

    int32_t dst_fd = sys::open(norm_dst, sys::O_WRONLY | sys::O_CREAT | sys::O_TRUNC);
    if (dst_fd < 0) {
        shell_print("Copy: cannot create \"");
        shell_print(norm_dst);
        shell_print("\"\n");
        sys::close(src_fd);
        s_last_rc = 10;
        return;
    }

    char buf[1024];
    int64_t total = 0;

    while (true) {
        int64_t bytes = sys::read(src_fd, buf, sizeof(buf));
        if (bytes <= 0)
            break;

        int64_t written = sys::write(dst_fd, buf, static_cast<size_t>(bytes));
        if (written != bytes) {
            shell_print("Copy: write error\n");
            sys::close(src_fd);
            sys::close(dst_fd);
            s_last_rc = 10;
            return;
        }
        total += bytes;
    }

    sys::close(src_fd);
    sys::close(dst_fd);

    shell_print("Copied ");
    shell_put_num(total);
    shell_print(" bytes\n");
    s_last_rc = 0;
}

// =========================================================================
// Delete
// =========================================================================

void cmd_delete(const char *args) {
    if (!args || *args == '\0') {
        shell_print("Delete: missing file argument\n");
        s_last_rc = 10;
        return;
    }

    char normalized[256];
    if (!normalize_path(args, s_current_dir, normalized, sizeof(normalized))) {
        shell_print("Delete: invalid path\n");
        s_last_rc = 10;
        return;
    }

    if (sys::unlink(normalized) != 0) {
        shell_print("Delete: cannot delete \"");
        shell_print(normalized);
        shell_print("\"\n");
        s_last_rc = 10;
        return;
    }

    shell_print("Deleted \"");
    shell_print(normalized);
    shell_print("\"\n");
    s_last_rc = 0;
}

// =========================================================================
// MakeDir
// =========================================================================

void cmd_makedir(const char *args) {
    if (!args || *args == '\0') {
        shell_print("MakeDir: missing directory name\n");
        s_last_rc = 10;
        return;
    }

    char normalized[256];
    if (!normalize_path(args, s_current_dir, normalized, sizeof(normalized))) {
        shell_print("MakeDir: invalid path\n");
        s_last_rc = 10;
        return;
    }

    if (sys::mkdir(normalized) != 0) {
        shell_print("MakeDir: cannot create \"");
        shell_print(normalized);
        shell_print("\"\n");
        s_last_rc = 10;
        return;
    }

    shell_print("Created \"");
    shell_print(normalized);
    shell_print("\"\n");
    s_last_rc = 0;
}

// =========================================================================
// Rename
// =========================================================================

void cmd_rename(const char *args) {
    if (!args || *args == '\0') {
        shell_print("Rename: missing arguments\n");
        shell_print("Usage: Rename <old> <new>\n");
        s_last_rc = 10;
        return;
    }

    char oldname[256], newname[256];
    const char *p = args;
    int i = 0;

    while (*p && *p != ' ' && i < 255)
        oldname[i++] = *p++;
    oldname[i] = '\0';

    while (*p == ' ')
        p++;
    if (shell_strstart(p, "AS ") || shell_strstart(p, "as "))
        p += 3;
    while (*p == ' ')
        p++;

    i = 0;
    while (*p && *p != ' ' && i < 255)
        newname[i++] = *p++;
    newname[i] = '\0';

    if (newname[0] == '\0') {
        shell_print("Rename: missing new name\n");
        s_last_rc = 10;
        return;
    }

    char norm_old[256], norm_new[256];
    if (!normalize_path(oldname, s_current_dir, norm_old, sizeof(norm_old))) {
        shell_print("Rename: invalid source path\n");
        s_last_rc = 10;
        return;
    }
    if (!normalize_path(newname, s_current_dir, norm_new, sizeof(norm_new))) {
        shell_print("Rename: invalid destination path\n");
        s_last_rc = 10;
        return;
    }

    if (sys::rename(norm_old, norm_new) != 0) {
        shell_print("Rename: failed\n");
        s_last_rc = 10;
        return;
    }

    shell_print("Renamed \"");
    shell_print(norm_old);
    shell_print("\" to \"");
    shell_print(norm_new);
    shell_print("\"\n");
    s_last_rc = 0;
}

// =========================================================================
// Help
// =========================================================================

void cmd_help() {
    shell_print("\nViperDOS Shell Commands:\n\n");
    shell_print("  CD [path]      - Change directory (default: /)\n");
    shell_print("  PWD            - Print current working directory\n");
    shell_print("  Dir [path]     - Brief directory listing\n");
    shell_print("  List [path]    - Detailed directory listing\n");
    shell_print("  Type <file>    - Display file contents\n");
    shell_print("  Copy           - Copy files\n");
    shell_print("  Delete         - Delete files/directories\n");
    shell_print("  MakeDir        - Create directory\n");
    shell_print("  Rename         - Rename files\n");
    shell_print("  Cls            - Clear screen\n");
    shell_print("  Echo [text]    - Print text\n");
    shell_print("  Version        - Show system version\n");
    shell_print("  Uptime         - Show system uptime\n");
    shell_print("  Run <path>     - Execute program\n");
    shell_print("  Why            - Explain last error\n");
    shell_print("  Help           - Show this help\n");
    shell_print("\nLine Editing:\n");
    shell_print("  Left/Right     - Move cursor\n");
    shell_print("  Up/Down        - History navigation\n");
    shell_print("  Home/End       - Jump to start/end\n");
    shell_print("  Ctrl+U         - Clear line\n");
    shell_print("\n");
}

// =========================================================================
// Echo
// =========================================================================

void cmd_echo(const char *args) {
    if (args)
        shell_print(args);
    shell_print("\n");
    s_last_rc = 0;
}

// =========================================================================
// Version
// =========================================================================

void cmd_version() {
    shell_print(VIPERDOS_VERSION_FULL " (" VIPERDOS_BUILD_DATE ")\n");
    shell_print("Platform: AArch64\n");
    s_last_rc = 0;
}

// =========================================================================
// Uptime
// =========================================================================

void cmd_uptime() {
    uint64_t ms = sys::uptime();
    uint64_t secs = ms / 1000;
    uint64_t mins = secs / 60;
    uint64_t hours = mins / 60;
    uint64_t days = hours / 24;

    shell_print("Uptime: ");
    if (days > 0) {
        shell_put_num(static_cast<int64_t>(days));
        shell_print(" day");
        if (days != 1)
            shell_print("s");
        shell_print(", ");
    }
    if (hours > 0 || days > 0) {
        shell_put_num(static_cast<int64_t>(hours % 24));
        shell_print(" hour");
        if ((hours % 24) != 1)
            shell_print("s");
        shell_print(", ");
    }
    shell_put_num(static_cast<int64_t>(mins % 60));
    shell_print(" minute");
    if ((mins % 60) != 1)
        shell_print("s");
    shell_print(", ");
    shell_put_num(static_cast<int64_t>(secs % 60));
    shell_print(" second");
    if ((secs % 60) != 1)
        shell_print("s");
    shell_print("\n");
    s_last_rc = 0;
}

// =========================================================================
// Why (explain last error)
// =========================================================================

void cmd_why() {
    if (s_last_rc == 0) {
        shell_print("No error.\n");
    } else {
        shell_print("Last return code: ");
        shell_put_num(s_last_rc);
        if (s_last_error) {
            shell_print(" - ");
            shell_print(s_last_error);
        }
        shell_print("\n");
    }
}

// =========================================================================
// Clear
// =========================================================================

void cmd_clear() {
    TextBuffer *buf = shell_get_buffer();
    if (buf) {
        buf->clear();
        buf->set_cursor(0, 0);
        buf->redraw_all();
    } else {
        // PTY mode: send ANSI clear screen sequence
        shell_print("\033[2J\033[H");
    }
    s_last_rc = 0;
}

// =========================================================================
// Run (fire-and-forget — does NOT waitpid to avoid blocking the event loop)
// =========================================================================

static void build_spawn_args(const char *args, char *out, size_t out_size) {
    size_t pos = 0;

    const char *prefix = "PWD=";
    while (*prefix && pos < out_size - 1)
        out[pos++] = *prefix++;

    const char *cwd = s_current_dir;
    while (*cwd && pos < out_size - 1)
        out[pos++] = *cwd++;

    if (args && *args) {
        if (pos < out_size - 1)
            out[pos++] = ';';
        while (*args && pos < out_size - 1)
            out[pos++] = *args++;
    }

    out[pos] = '\0';
}

static void run_program(const char *path, const char *args) {
    char spawn_args[512];
    build_spawn_args(args, spawn_args, sizeof(spawn_args));

    uint64_t pid = 0;
    uint64_t tid = 0;
    uint32_t bootstrap_send = 0xFFFFFFFFu;
    int64_t err = sys::spawn(path, nullptr, &pid, &tid, spawn_args, &bootstrap_send);

    if (err < 0) {
        shell_print("Run: failed to spawn \"");
        shell_print(path);
        shell_print("\" (error ");
        shell_put_num(err);
        shell_print(")\n");
        s_last_rc = 20;
        s_last_error = "Spawn failed";
        return;
    }

    if (bootstrap_send != 0xFFFFFFFFu) {
        sys::channel_close(static_cast<int32_t>(bootstrap_send));
    }

    // Enter foreground mode: forward keyboard to child via kernel TTY,
    // detect child exit via non-blocking waitpid
    if (s_shell_instance) {
        s_shell_instance->enter_foreground(pid, tid);
    }
    s_last_rc = 0;
}

void cmd_run(const char *cmdline) {
    if (!cmdline || *cmdline == '\0') {
        shell_print("Run: missing program path\n");
        s_last_rc = 10;
        s_last_error = "No path specified";
        return;
    }

    char path_buf[256];
    char args_buf[256];
    size_t path_len = 0;
    size_t args_len = 0;

    const char *p = cmdline;
    while (*p == ' ')
        p++;

    while (*p && *p != ' ' && path_len < 255)
        path_buf[path_len++] = *p++;
    path_buf[path_len] = '\0';

    while (*p == ' ')
        p++;

    while (*p && args_len < 255)
        args_buf[args_len++] = *p++;
    args_buf[args_len] = '\0';

    const char *path = path_buf;
    const char *args = args_len > 0 ? args_buf : nullptr;

    if (!is_sys_path(path) && path[0] != '/') {
        // Try current directory
        char normalized[256];
        normalize_path(path, s_current_dir, normalized, sizeof(normalized));

        int32_t fd = sys::open(normalized, sys::O_RDONLY);
        if (fd >= 0) {
            sys::close(fd);
            run_program(normalized, args);
            return;
        }

        // Try /c/ prefix
        char search_path[256];
        size_t i = 0;
        search_path[i++] = '/';
        search_path[i++] = 'c';
        search_path[i++] = '/';

        const char *q = path;
        while (*q && i < 250)
            search_path[i++] = *q++;
        search_path[i] = '\0';

        fd = sys::open(search_path, sys::O_RDONLY);
        if (fd >= 0) {
            sys::close(fd);
            run_program(search_path, args);
            return;
        }

        // Try with .prg extension
        if (i + 4 < 255) {
            search_path[i++] = '.';
            search_path[i++] = 'p';
            search_path[i++] = 'r';
            search_path[i++] = 'g';
            search_path[i] = '\0';

            fd = sys::open(search_path, sys::O_RDONLY);
            if (fd >= 0) {
                sys::close(fd);
                run_program(search_path, args);
                return;
            }
        }
    }

    // Normalize and try directly
    char normalized[256];
    normalize_path(path, s_current_dir, normalized, sizeof(normalized));
    run_program(normalized, args);
}

} // namespace consoled
