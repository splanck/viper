/**
 * @file cmd_fs.cpp
 * @brief Filesystem shell commands for vinit.
 *
 * Uses fsclient to route all file operations through fsd (microkernel path).
 */
#include "vinit.hpp"

#include "fsclient.hpp"

// Global fsclient instance for all filesystem operations
static fsclient::Client g_fsd;

// Check if fsd is available and connected
static bool fsd_available()
{
    return g_fsd.connect() == 0;
}

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

    if (!fsd_available())
    {
        print_str("Dir: filesystem not available\n");
        last_rc = RC_ERROR;
        last_error = "FSD not available";
        return;
    }

    u32 dir_id = 0;
    i32 err = g_fsd.open(path, 0, &dir_id); // O_RDONLY = 0
    if (err != 0)
    {
        print_str("Dir: cannot open \"");
        print_str(path);
        print_str("\"\n");
        last_rc = RC_ERROR;
        last_error = "Directory not found";
        return;
    }

    usize count = 0;
    usize col = 0;

    while (true)
    {
        u64 ino = 0;
        u8 type = 0;
        char name[256];
        i32 rc = g_fsd.readdir_one(dir_id, &ino, &type, name, sizeof(name));
        if (rc <= 0)
            break;

        if (type == 2) // Directory
        {
            print_str("  ");
            print_str(name);
            print_str("/");
            usize namelen = strlen(name) + 1;
            while (namelen < 18)
            {
                print_char(' ');
                namelen++;
            }
        }
        else
        {
            print_str("  ");
            print_str(name);
            usize namelen = strlen(name);
            while (namelen < 18)
            {
                print_char(' ');
                namelen++;
            }
        }

        col++;
        if (col >= 3)
        {
            print_str("\n");
            col = 0;
        }

        count++;
    }

    if (col > 0)
        print_str("\n");
    put_num(static_cast<i64>(count));
    print_str(" entries\n");

    g_fsd.close(dir_id);
    last_rc = RC_OK;
}

void cmd_list(const char *path)
{
    if (!path || *path == '\0')
        path = current_dir;

    if (!fsd_available())
    {
        print_str("List: filesystem not available\n");
        last_rc = RC_ERROR;
        last_error = "FSD not available";
        return;
    }

    u32 dir_id = 0;
    i32 err = g_fsd.open(path, 0, &dir_id); // O_RDONLY = 0
    if (err != 0)
    {
        print_str("List: cannot open \"");
        print_str(path);
        print_str("\"\n");
        last_rc = RC_ERROR;
        last_error = "Directory not found";
        return;
    }

    print_str("Directory \"");
    print_str(path);
    print_str("\"\n\n");

    usize file_count = 0;
    usize dir_count = 0;

    while (true)
    {
        u64 ino = 0;
        u8 type = 0;
        char name[256];
        i32 rc = g_fsd.readdir_one(dir_id, &ino, &type, name, sizeof(name));
        if (rc <= 0)
            break;

        print_str(name);
        usize namelen = strlen(name);
        while (namelen < 32)
        {
            print_char(' ');
            namelen++;
        }

        if (type == 2) // Directory
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

    g_fsd.close(dir_id);
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

    if (!fsd_available())
    {
        print_str("Type: filesystem not available\n");
        last_rc = RC_ERROR;
        last_error = "FSD not available";
        return;
    }

    u32 file_id = 0;
    i32 err = g_fsd.open(path, 0, &file_id); // O_RDONLY = 0
    if (err != 0)
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
        i64 bytes = g_fsd.read(file_id, buf, sizeof(buf) - 1);
        if (bytes <= 0)
            break;
        buf[bytes] = '\0';
        print_str(buf);
    }

    print_str("\n");
    g_fsd.close(file_id);
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

    if (!fsd_available())
    {
        print_str("Copy: filesystem not available\n");
        last_rc = RC_ERROR;
        last_error = "FSD not available";
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

    // fsd open flags: O_RDONLY=0, O_WRONLY=1, O_CREAT=0x40, O_TRUNC=0x200
    u32 src_id = 0;
    i32 err = g_fsd.open(source, 0, &src_id); // O_RDONLY
    if (err != 0)
    {
        print_str("Copy: cannot open \"");
        print_str(source);
        print_str("\"\n");
        last_rc = RC_ERROR;
        return;
    }

    u32 dst_id = 0;
    err = g_fsd.open(dest, 1 | 0x40 | 0x200, &dst_id); // O_WRONLY | O_CREAT | O_TRUNC
    if (err != 0)
    {
        print_str("Copy: cannot create \"");
        print_str(dest);
        print_str("\"\n");
        g_fsd.close(src_id);
        last_rc = RC_ERROR;
        return;
    }

    char buf[1024];
    i64 total = 0;

    while (true)
    {
        i64 bytes = g_fsd.read(src_id, buf, sizeof(buf));
        if (bytes <= 0)
            break;

        i64 written = g_fsd.write(dst_id, buf, static_cast<u32>(bytes));
        if (written != bytes)
        {
            print_str("Copy: write error\n");
            g_fsd.close(src_id);
            g_fsd.close(dst_id);
            last_rc = RC_ERROR;
            return;
        }
        total += bytes;
    }

    // Sync before closing to ensure data reaches disk
    g_fsd.fsync(dst_id);
    g_fsd.close(src_id);
    g_fsd.close(dst_id);

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

    if (!fsd_available())
    {
        print_str("Delete: filesystem not available\n");
        last_rc = RC_ERROR;
        last_error = "FSD not available";
        return;
    }

    if (g_fsd.unlink(args) != 0)
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

    if (!fsd_available())
    {
        print_str("MakeDir: filesystem not available\n");
        last_rc = RC_ERROR;
        last_error = "FSD not available";
        return;
    }

    if (g_fsd.mkdir(args) != 0)
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

    if (!fsd_available())
    {
        print_str("Rename: filesystem not available\n");
        last_rc = RC_ERROR;
        last_error = "FSD not available";
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

    if (g_fsd.rename(oldname, newname) != 0)
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
