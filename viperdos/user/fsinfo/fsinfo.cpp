/**
 * @file fsinfo.cpp
 * @brief Filesystem information utility for ViperDOS.
 *
 * @details
 * This utility demonstrates the use of the libc filesystem functions
 * and provides information about files and directories.
 *
 * Uses libc for file I/O to route through fsd (microkernel path).
 *
 * Usage:
 *   fsinfo [path]   - Show information about a file or directory
 *   fsinfo          - Show information about the root directory
 */

#include "../syscall.hpp"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Format file size with appropriate units
static void format_size(u64 bytes, char *buf, size_t bufsize)
{
    if (bytes < 1024)
    {
        snprintf(buf, bufsize, "%llu B", (unsigned long long)bytes);
    }
    else if (bytes < 1024 * 1024)
    {
        snprintf(buf, bufsize, "%llu KB", (unsigned long long)(bytes / 1024));
    }
    else if (bytes < 1024 * 1024 * 1024)
    {
        snprintf(buf, bufsize, "%llu MB", (unsigned long long)(bytes / (1024 * 1024)));
    }
    else
    {
        snprintf(buf, bufsize, "%llu GB", (unsigned long long)(bytes / (1024 * 1024 * 1024)));
    }
}

// Get file type string
static const char *file_type_str(u32 mode)
{
    if (mode & 0x4000)
        return "Directory";
    if (mode & 0xA000)
        return "Symlink";
    if (mode & 0x8000)
        return "File";
    return "Unknown";
}

// Print file information
static int print_file_info(const char *path)
{
    struct stat st;
    if (stat(path, &st) < 0)
    {
        printf("fsinfo: cannot stat '%s': No such file or directory\n", path);
        return 1;
    }

    char size_str[32];
    format_size(st.st_size, size_str, sizeof(size_str));

    printf("\nFile Information: %s\n", path);
    printf("=====================================\n");
    printf("  Type:        %s\n", file_type_str(st.st_mode));
    printf("  Inode:       %llu\n", (unsigned long long)st.st_ino);
    printf("  Size:        %s (%llu bytes)\n", size_str, (unsigned long long)st.st_size);
    printf("  Blocks:      %llu\n", (unsigned long long)st.st_blocks);
    printf("  Mode:        0x%04x\n", st.st_mode);

    return 0;
}

// List directory contents with details
static int list_directory(const char *path)
{
    // Check if it's a directory first
    struct stat st;
    if (stat(path, &st) < 0)
    {
        printf("fsinfo: cannot access '%s'\n", path);
        return 1;
    }

    if (!S_ISDIR(st.st_mode))
    {
        printf("fsinfo: '%s' is not a directory\n", path);
        return 1;
    }

    DIR *dir = opendir(path);
    if (!dir)
    {
        printf("fsinfo: cannot open directory '%s'\n", path);
        return 1;
    }

    printf("\nDirectory Listing: %s\n", path);
    printf("=====================================\n");
    printf("  %-20s  %10s  %s\n", "Name", "Size", "Type");
    printf("  %-20s  %10s  %s\n", "----", "----", "----");

    u64 total_size = 0;
    int file_count = 0;
    int dir_count = 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr)
    {
        if (ent->d_ino != 0)
        {
            // Build full path for stat
            char full_path[512];
            if (strcmp(path, "/") == 0)
            {
                snprintf(full_path, sizeof(full_path), "/%s", ent->d_name);
            }
            else
            {
                snprintf(full_path, sizeof(full_path), "%s/%s", path, ent->d_name);
            }

            struct stat entry_st;
            char size_str[16] = "-";
            const char *type_str = "?";

            if (stat(full_path, &entry_st) == 0)
            {
                if (S_ISDIR(entry_st.st_mode))
                {
                    type_str = "<DIR>";
                    dir_count++;
                }
                else
                {
                    format_size(entry_st.st_size, size_str, sizeof(size_str));
                    total_size += entry_st.st_size;
                    type_str = "FILE";
                    file_count++;
                }
            }

            printf("  %-20s  %10s  %s\n", ent->d_name, size_str, type_str);
        }
    }

    closedir(dir);

    char total_str[32];
    format_size(total_size, total_str, sizeof(total_str));
    printf("\n  Total: %d files, %d directories, %s\n", file_count, dir_count, total_str);

    return 0;
}

// Show disk usage summary
static void show_usage_summary()
{
    printf("\nDisk Usage Summary\n");
    printf("=====================================\n");

    // Get memory info as a proxy for system resources
    MemInfo mem;
    if (sys::mem_info(&mem) == 0)
    {
        printf("  Page Size:       %llu bytes\n", (unsigned long long)mem.page_size);
        printf("  Total Pages:     %llu\n", (unsigned long long)mem.total_pages);
        printf("  Free Pages:      %llu\n", (unsigned long long)mem.free_pages);
        printf("  Used Pages:      %llu\n", (unsigned long long)(mem.total_pages - mem.free_pages));

        u64 total_kb = (mem.total_pages * mem.page_size) / 1024;
        u64 free_kb = (mem.free_pages * mem.page_size) / 1024;
        u64 used_kb = total_kb - free_kb;
        printf("\n  Total Memory:    %llu KB\n", (unsigned long long)total_kb);
        printf("  Free Memory:     %llu KB\n", (unsigned long long)free_kb);
        printf("  Used Memory:     %llu KB\n", (unsigned long long)used_kb);
    }
    else
    {
        printf("  (Unable to get memory info)\n");
    }
}

extern "C" void _start()
{
    printf("\n=== ViperDOS Filesystem Information Utility ===\n");

    // Get current working directory
    char cwd[256];
    if (sys::getcwd(cwd, sizeof(cwd)) > 0)
    {
        printf("Current Directory: %s\n", cwd);
    }

    // Show root directory info and listing
    print_file_info("/");
    list_directory("/");
    show_usage_summary();

    printf("\n");
    sys::exit(0);
}
