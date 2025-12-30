/**
 * @file cmd_fs.cpp
 * @brief Filesystem shell commands for vinit.
 */
#include "vinit.hpp"

void cmd_cd(const char *args)
{
    const char *path = "/";
    if (args && args[0])
        path = args;

    i32 result = sys::chdir(path);
    if (result < 0)
    {
        print_str("CD: ");
        print_str(path);
        print_str(": No such directory\n");
        last_rc = RC_ERROR;
        last_error = "Directory not found";
        return;
    }

    refresh_current_dir();
    last_rc = RC_OK;
}

void cmd_pwd()
{
    char buf[256];
    i64 len = sys::getcwd(buf, sizeof(buf));
    if (len < 0)
    {
        print_str("PWD: Failed to get current directory\n");
        last_rc = RC_ERROR;
        last_error = "getcwd failed";
        return;
    }
    print_str(buf);
    print_str("\n");
    last_rc = RC_OK;
}

void cmd_dir(const char *path)
{
    if (!path || *path == '\0')
        path = current_dir;

    i32 fd = sys::open(path, sys::O_RDONLY);
    if (fd < 0)
    {
        print_str("Dir: cannot open \"");
        print_str(path);
        print_str("\"\n");
        last_rc = RC_ERROR;
        last_error = "Directory not found";
        return;
    }

    u8 buf[32768];  // 32KB - holds ~120 directory entries
    i64 bytes = sys::readdir(fd, buf, sizeof(buf));

    if (bytes < 0)
    {
        print_str("Dir: not a directory\n");
        sys::close(fd);
        last_rc = RC_ERROR;
        last_error = "Not a directory";
        return;
    }

    usize offset = 0;
    usize count = 0;
    usize col = 0;

    while (offset < static_cast<usize>(bytes))
    {
        sys::DirEnt *ent = reinterpret_cast<sys::DirEnt *>(buf + offset);

        if (ent->type == 2)
        {
            print_str("  ");
            print_str(ent->name);
            print_str("/");
            usize namelen = strlen(ent->name) + 1;
            while (namelen < 18) { print_char(' '); namelen++; }
        }
        else
        {
            print_str("  ");
            print_str(ent->name);
            usize namelen = strlen(ent->name);
            while (namelen < 18) { print_char(' '); namelen++; }
        }

        col++;
        if (col >= 3)
        {
            print_str("\n");
            col = 0;
        }

        count++;
        offset += ent->reclen;
    }

    if (col > 0) print_str("\n");
    put_num(static_cast<i64>(count));
    print_str(" entries\n");

    sys::close(fd);
    last_rc = RC_OK;
}

void cmd_list(const char *path)
{
    if (!path || *path == '\0')
        path = current_dir;

    i32 fd = sys::open(path, sys::O_RDONLY);
    if (fd < 0)
    {
        print_str("List: cannot open \"");
        print_str(path);
        print_str("\"\n");
        last_rc = RC_ERROR;
        last_error = "Directory not found";
        return;
    }

    u8 buf[32768];  // 32KB - holds ~120 directory entries
    i64 bytes = sys::readdir(fd, buf, sizeof(buf));

    if (bytes < 0)
    {
        print_str("List: not a directory\n");
        sys::close(fd);
        last_rc = RC_ERROR;
        last_error = "Not a directory";
        return;
    }

    print_str("Directory \"");
    print_str(path);
    print_str("\"\n\n");

    usize offset = 0;
    usize file_count = 0;
    usize dir_count = 0;

    while (offset < static_cast<usize>(bytes))
    {
        sys::DirEnt *ent = reinterpret_cast<sys::DirEnt *>(buf + offset);

        print_str(ent->name);
        usize namelen = strlen(ent->name);
        while (namelen < 32) { print_char(' '); namelen++; }

        if (ent->type == 2)
        {
            print_str("  <dir>    rwed");
            dir_count++;
        }
        else
        {
            print_str("           rwed");
            file_count++;
        }
        print_str("\n");

        offset += ent->reclen;
    }

    print_str("\n");
    put_num(static_cast<i64>(file_count));
    print_str(" file");
    if (file_count != 1) print_str("s");
    print_str(", ");
    put_num(static_cast<i64>(dir_count));
    print_str(" director");
    if (dir_count != 1) print_str("ies"); else print_str("y");
    print_str("\n");

    sys::close(fd);
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

    i32 fd = sys::open(path, sys::O_RDONLY);
    if (fd < 0)
    {
        print_str("Type: cannot open \"");
        print_str(path);
        print_str("\"\n");
        last_rc = RC_ERROR;
        last_error = "File not found";
        return;
    }

    char buf[512];
    while (true)
    {
        i64 bytes = sys::read(fd, buf, sizeof(buf) - 1);
        if (bytes <= 0) break;
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
    while (*p == ' ') p++;
    if (strstart(p, "TO ") || strstart(p, "to "))
        p += 3;
    while (*p == ' ') p++;

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

    i32 src_fd = sys::open(source, sys::O_RDONLY);
    if (src_fd < 0)
    {
        print_str("Copy: cannot open \"");
        print_str(source);
        print_str("\"\n");
        last_rc = RC_ERROR;
        return;
    }

    i32 dst_fd = sys::open(dest, sys::O_WRONLY | sys::O_CREAT | sys::O_TRUNC);
    if (dst_fd < 0)
    {
        print_str("Copy: cannot create \"");
        print_str(dest);
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
        if (bytes <= 0) break;

        i64 written = sys::write(dst_fd, buf, bytes);
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

    if (sys::unlink(args) < 0)
    {
        print_str("Delete: cannot delete \"");
        print_str(args);
        print_str("\"\n");
        last_rc = RC_ERROR;
        return;
    }

    print_str("Deleted \"");
    print_str(args);
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

    if (sys::mkdir(args) < 0)
    {
        print_str("MakeDir: cannot create \"");
        print_str(args);
        print_str("\"\n");
        last_rc = RC_ERROR;
        return;
    }

    print_str("Created \"");
    print_str(args);
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

    while (*p == ' ') p++;
    if (strstart(p, "AS ") || strstart(p, "as "))
        p += 3;
    while (*p == ' ') p++;

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

    if (sys::rename(oldname, newname) < 0)
    {
        print_str("Rename: failed\n");
        last_rc = RC_ERROR;
        return;
    }

    print_str("Renamed \"");
    print_str(oldname);
    print_str("\" to \"");
    print_str(newname);
    print_str("\"\n");
    last_rc = RC_OK;
}
