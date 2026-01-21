/**
 * @file cmd_fs.cpp
 * @brief Filesystem shell commands for vinit.
 *
 * Uses fsclient to route all file operations through fsd (microkernel path).
 * Two-disk architecture: /sys paths go to kernel VFS, other paths to fsd.
 */
#include "vinit.hpp"

#include "fsclient.hpp"

// Global fsclient instance for all filesystem operations
static fsclient::Client g_fsd;

// =============================================================================
// Path Helpers for Two-Disk Architecture
// =============================================================================

/**
 * @brief Check if path is exactly "/sys" or starts with "/sys/".
 */
static bool is_sys_path(const char *path)
{
    if (!path || path[0] != '/')
        return false;
    if (path[1] == 's' && path[2] == 'y' && path[3] == 's')
    {
        // /sys or /sys/...
        return path[4] == '\0' || path[4] == '/';
    }
    return false;
}

/**
 * @brief Check if path is the root directory "/".
 */
static bool is_root_path(const char *path)
{
    return path && path[0] == '/' && path[1] == '\0';
}

/**
 * @brief Normalize a path, resolving . and .. components.
 * @param path Input path (absolute or relative).
 * @param cwd Current working directory (used for relative paths).
 * @param out Output buffer for normalized path.
 * @param out_size Size of output buffer.
 * @return true on success.
 */
bool normalize_path(const char *path, const char *cwd, char *out, usize out_size)
{
    if (!path || !out || out_size < 2)
        return false;

    // Start with cwd for relative paths
    char buf[512];
    usize pos = 0;

    if (path[0] == '/')
    {
        // Absolute path
        buf[pos++] = '/';
        path++;
    }
    else
    {
        // Relative path - start from cwd
        for (usize i = 0; cwd[i] && pos < sizeof(buf) - 1; i++)
        {
            buf[pos++] = cwd[i];
        }
        if (pos > 0 && buf[pos - 1] != '/')
        {
            buf[pos++] = '/';
        }
    }

    // Process path components
    while (*path && pos < sizeof(buf) - 1)
    {
        // Skip leading slashes
        while (*path == '/')
            path++;
        if (*path == '\0')
            break;

        // Find end of component
        const char *start = path;
        while (*path && *path != '/')
            path++;
        usize len = path - start;

        if (len == 1 && start[0] == '.')
        {
            // "." - skip
            continue;
        }
        else if (len == 2 && start[0] == '.' && start[1] == '.')
        {
            // ".." - go up one level
            if (pos > 1)
            {
                pos--; // Remove trailing slash
                while (pos > 1 && buf[pos - 1] != '/')
                    pos--;
            }
        }
        else
        {
            // Normal component
            for (usize i = 0; i < len && pos < sizeof(buf) - 1; i++)
            {
                buf[pos++] = start[i];
            }
            if (pos < sizeof(buf) - 1)
            {
                buf[pos++] = '/';
            }
        }
    }

    // Remove trailing slash unless root
    if (pos > 1 && buf[pos - 1] == '/')
        pos--;
    buf[pos] = '\0';

    // Copy to output
    for (usize i = 0; i <= pos && i < out_size; i++)
    {
        out[i] = buf[i];
    }
    out[out_size - 1] = '\0';
    return true;
}

/**
 * @brief Check if fsd is available and connected.
 *
 * Two-disk architecture: First checks if fsd registered at startup,
 * then tries to connect. In system-only mode (no user disk), this
 * will always fail.
 */
static bool fsd_available()
{
    // Quick check if fsd never started
    if (!is_fsd_available())
        return false;

    // Try to connect (may fail if fsd crashed)
    return g_fsd.connect() == 0;
}


void cmd_cd(const char *args)
{
    const char *path = "/";
    if (args && args[0])
        path = args;

    // Normalize the path first
    char normalized[256];
    if (!normalize_path(path, current_dir, normalized, sizeof(normalized)))
    {
        print_str("CD: invalid path\n");
        last_rc = RC_ERROR;
        last_error = "Invalid path";
        return;
    }

    // Two-disk architecture: route by path
    if (is_sys_path(normalized))
    {
        // /sys paths - use kernel chdir
        i32 result = sys::chdir(normalized);
        if (result < 0)
        {
            print_str("CD: ");
            print_str(normalized);
            print_str(": No such directory\n");
            last_rc = RC_ERROR;
            last_error = "Directory not found";
            return;
        }
        refresh_current_dir();
    }
    else if (is_root_path(normalized))
    {
        // Root "/" - always valid (shows both sys and user dirs)
        // Update current_dir directly - kernel doesn't know about unified root
        current_dir[0] = '/';
        current_dir[1] = '\0';
    }
    else
    {
        // User paths - validate directory exists
        bool valid = false;

        if (fsd_available())
        {
            // Use FSD to verify
            u32 dir_id = 0;
            i32 err = g_fsd.open(normalized, 0, &dir_id);
            if (err == 0)
            {
                g_fsd.close(dir_id);
                valid = true;
            }
        }
        else
        {
            // Monolithic mode: use kernel VFS
            i32 fd = sys::open(normalized, sys::O_RDONLY);
            if (fd >= 0)
            {
                sys::close(fd);
                valid = true;
            }
        }

        if (!valid)
        {
            print_str("CD: ");
            print_str(normalized);
            print_str(": No such directory\n");
            last_rc = RC_ERROR;
            last_error = "Directory not found";
            return;
        }

        // Update current_dir directly
        usize i = 0;
        while (normalized[i] && i < MAX_PATH_LEN - 1)
        {
            current_dir[i] = normalized[i];
            i++;
        }
        current_dir[i] = '\0';
    }

    last_rc = RC_OK;
}

void cmd_pwd()
{
    // Two-disk architecture: we track cwd ourselves for user paths
    print_str(current_dir);
    print_str("\n");
    last_rc = RC_OK;
}

/**
 * @brief Print a single directory entry in compact format.
 */
static void print_dir_entry(const char *name, bool is_dir, usize *col)
{
    // Build entry in a buffer to send as one message
    char entry[32];
    char *p = entry;

    // Leading spaces
    *p++ = ' ';
    *p++ = ' ';

    // Copy name
    const char *n = name;
    usize namelen = 0;
    while (*n && namelen < 17)
    {
        *p++ = *n++;
        namelen++;
    }

    // Add "/" for directories
    if (is_dir && namelen < 17)
    {
        *p++ = '/';
        namelen++;
    }

    // Pad to 18 chars
    while (namelen < 18)
    {
        *p++ = ' ';
        namelen++;
    }

    *p = '\0';
    print_str(entry);

    (*col)++;
    if (*col >= 3)
    {
        print_str("\n");
        *col = 0;
    }
}

/**
 * @brief List /sys directory using kernel VFS.
 */
static void dir_sys_directory(const char *path, usize *count, usize *col)
{
    // Open via kernel VFS
    i32 fd = sys::open(path, sys::O_RDONLY);
    if (fd < 0)
    {
        print_str("Dir: cannot open \"");
        print_str(path);
        print_str("\"\n");
        return;
    }

    // Read directory entries using kernel readdir
    // Use larger buffer to fit all entries (DirEnt is ~268 bytes each)
    u8 buf[4096];
    i64 bytes = sys::readdir(fd, buf, sizeof(buf));
    if (bytes > 0)
    {
        usize offset = 0;
        while (offset < static_cast<usize>(bytes))
        {
            sys::DirEnt *ent = reinterpret_cast<sys::DirEnt *>(buf + offset);
            if (ent->reclen == 0)
                break;

            // Skip . and ..
            if (!(ent->name[0] == '.' && (ent->name[1] == '\0' ||
                                          (ent->name[1] == '.' && ent->name[2] == '\0'))))
            {
                bool is_dir = (ent->type == 2);
                print_dir_entry(ent->name, is_dir, col);
                (*count)++;
            }

            offset += ent->reclen;
        }
    }

    sys::close(fd);
}

void cmd_dir(const char *path)
{
    if (!path || *path == '\0')
        path = current_dir;

    // Normalize the path
    char normalized[256];
    if (!normalize_path(path, current_dir, normalized, sizeof(normalized)))
    {
        print_str("Dir: invalid path\n");
        last_rc = RC_ERROR;
        return;
    }

    usize count = 0;
    usize col = 0;

    // Two-disk architecture: handle different path types
    if (is_root_path(normalized))
    {
        // Root "/" - show synthetic /sys plus user disk root contents
        // First, show /sys as virtual directory
        print_dir_entry("sys", true, &col);
        count++;

        // Then show user disk root contents
        if (fsd_available())
        {
            // Use FSD if available
            u32 dir_id = 0;
            if (g_fsd.open("/", 0, &dir_id) == 0)
            {
                while (true)
                {
                    u64 ino = 0;
                    u8 type = 0;
                    char name[256];
                    i32 rc = g_fsd.readdir_one(dir_id, &ino, &type, name, sizeof(name));
                    if (rc <= 0)
                        break;

                    // Skip . and ..
                    if (name[0] == '.' && (name[1] == '\0' ||
                                           (name[1] == '.' && name[2] == '\0')))
                        continue;

                    print_dir_entry(name, type == 2, &col);
                    count++;
                }
                g_fsd.close(dir_id);
            }
        }
        else
        {
            // Monolithic mode: use kernel VFS for user disk root
            dir_sys_directory("/", &count, &col);
        }
    }
    else if (is_sys_path(normalized))
    {
        // /sys paths - use kernel VFS
        dir_sys_directory(normalized, &count, &col);
    }
    else
    {
        // User paths - use fsd if available, otherwise kernel VFS
        if (fsd_available())
        {
            u32 dir_id = 0;
            i32 err = g_fsd.open(normalized, 0, &dir_id);
            if (err != 0)
            {
                print_str("Dir: cannot open \"");
                print_str(normalized);
                print_str("\"\n");
                last_rc = RC_ERROR;
                last_error = "Directory not found";
                return;
            }

            while (true)
            {
                u64 ino = 0;
                u8 type = 0;
                char name[256];
                i32 rc = g_fsd.readdir_one(dir_id, &ino, &type, name, sizeof(name));
                if (rc <= 0)
                    break;

                // Skip . and ..
                if (name[0] == '.' && (name[1] == '\0' ||
                                       (name[1] == '.' && name[2] == '\0')))
                    continue;

                print_dir_entry(name, type == 2, &col);
                count++;
            }

            g_fsd.close(dir_id);
        }
        else
        {
            // Monolithic mode: use kernel VFS directly
            dir_sys_directory(normalized, &count, &col);
        }
    }

    if (col > 0)
        print_str("\n");
    put_num(static_cast<i64>(count));
    print_str(" entries\n");

    last_rc = RC_OK;
}

/**
 * @brief Print a single directory entry in detailed format.
 */
static void print_list_entry(const char *name, bool is_dir, bool readonly)
{
    // Build the entire line in a buffer to send as one message
    char line[128];
    char *p = line;

    // Copy name
    const char *n = name;
    while (*n && (p - line) < 32)
        *p++ = *n++;

    // Pad to 32 chars
    while ((p - line) < 32)
        *p++ = ' ';

    // Directory marker
    if (is_dir)
    {
        const char *dir_marker = "  <dir>    ";
        while (*dir_marker)
            *p++ = *dir_marker++;
    }
    else
    {
        const char *spaces = "           ";
        while (*spaces)
            *p++ = *spaces++;
    }

    // Permissions
    if (readonly)
    {
        *p++ = 'r'; *p++ = '-'; *p++ = '-'; *p++ = 'e';
    }
    else
    {
        *p++ = 'r'; *p++ = 'w'; *p++ = 'e'; *p++ = 'd';
    }

    *p++ = '\n';
    *p = '\0';

    print_str(line);
}

/**
 * @brief List /sys directory entries in detailed format.
 */
// List directory using kernel syscalls (for both /sys and user paths in monolithic mode)
static void list_kernel_directory(const char *path, usize *file_count, usize *dir_count, bool readonly)
{
    i32 fd = sys::open(path, sys::O_RDONLY);
    if (fd < 0)
    {
        print_str("List: cannot open \"");
        print_str(path);
        print_str("\"\n");
        return;
    }

    // Use a larger buffer to fit all directory entries
    // DirEnt is ~268 bytes, user disk may have many entries
    u8 buf[4096];
    i64 bytes = sys::readdir(fd, buf, sizeof(buf));
    if (bytes > 0)
    {
        usize offset = 0;
        while (offset < static_cast<usize>(bytes))
        {
            sys::DirEnt *ent = reinterpret_cast<sys::DirEnt *>(buf + offset);
            if (ent->reclen == 0)
                break;

            // Skip . and ..
            if (!(ent->name[0] == '.' && (ent->name[1] == '\0' ||
                                          (ent->name[1] == '.' && ent->name[2] == '\0'))))
            {
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

void cmd_list(const char *path)
{
    if (!path || *path == '\0')
        path = current_dir;

    // Normalize the path
    char normalized[256];
    if (!normalize_path(path, current_dir, normalized, sizeof(normalized)))
    {
        print_str("List: invalid path\n");
        last_rc = RC_ERROR;
        return;
    }

    print_str("Directory \"");
    print_str(normalized);
    print_str("\"\n\n");

    usize file_count = 0;
    usize dir_count = 0;

    // Two-disk architecture: handle different path types
    if (is_root_path(normalized))
    {
        // Root "/" - show synthetic /sys plus user disk root contents
        print_list_entry("sys", true, true); // readonly system directory
        dir_count++;

        // Then show user disk root contents
        if (fsd_available())
        {
            // Use FSD if available
            u32 dir_id = 0;
            if (g_fsd.open("/", 0, &dir_id) == 0)
            {
                while (true)
                {
                    u64 ino = 0;
                    u8 type = 0;
                    char name[256];
                    i32 rc = g_fsd.readdir_one(dir_id, &ino, &type, name, sizeof(name));
                    if (rc <= 0)
                        break;

                    // Skip . and ..
                    if (name[0] == '.' && (name[1] == '\0' ||
                                           (name[1] == '.' && name[2] == '\0')))
                        continue;

                    bool is_dir = (type == 2);
                    print_list_entry(name, is_dir, false);

                    if (is_dir)
                        dir_count++;
                    else
                        file_count++;
                }
                g_fsd.close(dir_id);
            }
        }
        else
        {
            // Monolithic mode: use kernel VFS for user disk root
            list_kernel_directory("/", &file_count, &dir_count, false);
        }
    }
    else if (is_sys_path(normalized))
    {
        // /sys paths - use kernel VFS (always readonly)
        list_kernel_directory(normalized, &file_count, &dir_count, true);
    }
    else
    {
        // User paths - use fsd if available, otherwise kernel VFS
        if (fsd_available())
        {
            u32 dir_id = 0;
            i32 err = g_fsd.open(normalized, 0, &dir_id);
            if (err != 0)
            {
                print_str("List: cannot open \"");
                print_str(normalized);
                print_str("\"\n");
                last_rc = RC_ERROR;
                last_error = "Directory not found";
                return;
            }

            while (true)
            {
                u64 ino = 0;
                u8 type = 0;
                char name[256];
                i32 rc = g_fsd.readdir_one(dir_id, &ino, &type, name, sizeof(name));
                if (rc <= 0)
                    break;

                // Skip . and ..
                if (name[0] == '.' && (name[1] == '\0' ||
                                       (name[1] == '.' && name[2] == '\0')))
                    continue;

                bool is_dir = (type == 2);
                print_list_entry(name, is_dir, false);

                if (is_dir)
                    dir_count++;
                else
                    file_count++;
            }

            g_fsd.close(dir_id);
        }
        else
        {
            // Monolithic mode: use kernel VFS directly
            list_kernel_directory(normalized, &file_count, &dir_count, false);
        }
    }

    print_str("\n");
    put_num(static_cast<i64>(file_count));
    print_str(" file");
    if (file_count != 1)
        print_str("s");
    print_str(", ");
    put_num(static_cast<i64>(dir_count));
    print_str(" director");
    if (dir_count != 1)
        print_str("ies");
    else
        print_str("y");
    print_str("\n");

    last_rc = RC_OK;
}

void cmd_type(const char *path)
{
    if (!path || *path == '\0')
    {
        print_str("Type: missing file argument\n");
        last_rc = RC_ERROR;
        last_error = "Missing filename";
        return;
    }

    // Normalize path (handle relative paths)
    char normalized[256];
    if (!normalize_path(path, current_dir, normalized, sizeof(normalized)))
    {
        print_str("Type: invalid path\n");
        last_rc = RC_ERROR;
        last_error = "Invalid path";
        return;
    }

    // Use kernel VFS (works for both /sys and user paths)
    i32 fd = sys::open(normalized, sys::O_RDONLY);
    if (fd < 0)
    {
        print_str("Type: cannot open \"");
        print_str(normalized);
        print_str("\"\n");
        last_rc = RC_ERROR;
        last_error = "File not found";
        return;
    }

    char buf[512];
    while (true)
    {
        i64 bytes = sys::read(fd, buf, sizeof(buf) - 1);
        if (bytes <= 0)
            break;
        buf[bytes] = '\0';
        print_str(buf);
    }

    print_str("\n");
    sys::close(fd);
    last_rc = RC_OK;
}

void cmd_copy(const char *args)
{
    if (!args || *args == '\0')
    {
        print_str("Copy: missing arguments\n");
        print_str("Usage: Copy <source> <dest>\n");
        last_rc = RC_ERROR;
        last_error = "Missing arguments";
        return;
    }

    // Simple source/dest parsing
    static char source[256], dest[256];
    const char *p = args;
    int i = 0;

    // Get source
    while (*p && *p != ' ' && i < 255)
        source[i++] = *p++;
    source[i] = '\0';

    // Skip whitespace and optional "TO"
    while (*p == ' ')
        p++;
    if (strstart(p, "TO ") || strstart(p, "to "))
        p += 3;
    while (*p == ' ')
        p++;

    // Get dest
    i = 0;
    while (*p && *p != ' ' && i < 255)
        dest[i++] = *p++;
    dest[i] = '\0';

    if (dest[0] == '\0')
    {
        print_str("Copy: missing destination\n");
        last_rc = RC_ERROR;
        return;
    }

    // Normalize paths (handle relative paths)
    char norm_src[256], norm_dst[256];
    if (!normalize_path(source, current_dir, norm_src, sizeof(norm_src)))
    {
        print_str("Copy: invalid source path\n");
        last_rc = RC_ERROR;
        return;
    }
    if (!normalize_path(dest, current_dir, norm_dst, sizeof(norm_dst)))
    {
        print_str("Copy: invalid destination path\n");
        last_rc = RC_ERROR;
        return;
    }

    // Use kernel VFS
    i32 src_fd = sys::open(norm_src, sys::O_RDONLY);
    if (src_fd < 0)
    {
        print_str("Copy: cannot open \"");
        print_str(norm_src);
        print_str("\"\n");
        last_rc = RC_ERROR;
        return;
    }

    i32 dst_fd = sys::open(norm_dst, sys::O_WRONLY | sys::O_CREAT | sys::O_TRUNC);
    if (dst_fd < 0)
    {
        print_str("Copy: cannot create \"");
        print_str(norm_dst);
        print_str("\"\n");
        sys::close(src_fd);
        last_rc = RC_ERROR;
        return;
    }

    char buf[1024];
    i64 total = 0;

    while (true)
    {
        i64 bytes = sys::read(src_fd, buf, sizeof(buf));
        if (bytes <= 0)
            break;

        i64 written = sys::write(dst_fd, buf, static_cast<usize>(bytes));
        if (written != bytes)
        {
            print_str("Copy: write error\n");
            sys::close(src_fd);
            sys::close(dst_fd);
            last_rc = RC_ERROR;
            return;
        }
        total += bytes;
    }

    sys::close(src_fd);
    sys::close(dst_fd);

    print_str("Copied ");
    put_num(total);
    print_str(" bytes\n");
    last_rc = RC_OK;
}

void cmd_delete(const char *args)
{
    if (!args || *args == '\0')
    {
        print_str("Delete: missing file argument\n");
        last_rc = RC_ERROR;
        return;
    }

    // Normalize path (handle relative paths)
    char normalized[256];
    if (!normalize_path(args, current_dir, normalized, sizeof(normalized)))
    {
        print_str("Delete: invalid path\n");
        last_rc = RC_ERROR;
        return;
    }

    // Use kernel VFS
    if (sys::unlink(normalized) != 0)
    {
        print_str("Delete: cannot delete \"");
        print_str(normalized);
        print_str("\"\n");
        last_rc = RC_ERROR;
        return;
    }

    print_str("Deleted \"");
    print_str(normalized);
    print_str("\"\n");
    last_rc = RC_OK;
}

void cmd_makedir(const char *args)
{
    if (!args || *args == '\0')
    {
        print_str("MakeDir: missing directory name\n");
        last_rc = RC_ERROR;
        return;
    }

    // Normalize path (handle relative paths)
    char normalized[256];
    if (!normalize_path(args, current_dir, normalized, sizeof(normalized)))
    {
        print_str("MakeDir: invalid path\n");
        last_rc = RC_ERROR;
        return;
    }

    // Use kernel VFS
    if (sys::mkdir(normalized) != 0)
    {
        print_str("MakeDir: cannot create \"");
        print_str(normalized);
        print_str("\"\n");
        last_rc = RC_ERROR;
        return;
    }

    print_str("Created \"");
    print_str(normalized);
    print_str("\"\n");
    last_rc = RC_OK;
}

void cmd_rename(const char *args)
{
    if (!args || *args == '\0')
    {
        print_str("Rename: missing arguments\n");
        print_str("Usage: Rename <old> <new>\n");
        last_rc = RC_ERROR;
        return;
    }

    static char oldname[256], newname[256];
    const char *p = args;
    int i = 0;

    while (*p && *p != ' ' && i < 255)
        oldname[i++] = *p++;
    oldname[i] = '\0';

    while (*p == ' ')
        p++;
    if (strstart(p, "AS ") || strstart(p, "as "))
        p += 3;
    while (*p == ' ')
        p++;

    i = 0;
    while (*p && *p != ' ' && i < 255)
        newname[i++] = *p++;
    newname[i] = '\0';

    if (newname[0] == '\0')
    {
        print_str("Rename: missing new name\n");
        last_rc = RC_ERROR;
        return;
    }

    // Normalize paths (handle relative paths)
    char norm_old[256], norm_new[256];
    if (!normalize_path(oldname, current_dir, norm_old, sizeof(norm_old)))
    {
        print_str("Rename: invalid source path\n");
        last_rc = RC_ERROR;
        return;
    }
    if (!normalize_path(newname, current_dir, norm_new, sizeof(norm_new)))
    {
        print_str("Rename: invalid destination path\n");
        last_rc = RC_ERROR;
        return;
    }

    // Use kernel VFS
    if (sys::rename(norm_old, norm_new) != 0)
    {
        print_str("Rename: failed\n");
        last_rc = RC_ERROR;
        return;
    }

    print_str("Renamed \"");
    print_str(norm_old);
    print_str("\" to \"");
    print_str(norm_new);
    print_str("\"\n");
    last_rc = RC_OK;
}
