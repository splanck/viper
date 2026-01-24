//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/fsd_stubs.c
// Purpose: Stub functions for removed fsd (filesystem daemon) backend.
// Key invariants: All functions return "not available" or fail.
// Ownership/Lifetime: Library
//
//===----------------------------------------------------------------------===//

/**
 * @file fsd_stubs.c
 * @brief Stub implementations for the removed fsd backend.
 *
 * @details
 * These stubs are provided for ABI compatibility with code that still
 * references the old fsd backend functions. All functions return
 * failure/unavailable status since fsd has been removed in favor
 * of the monolithic kernel VFS.
 */

#include <stddef.h>

/* fsd is not available - always return 0/false */
int __viper_fsd_is_available(void) {
    return 0;
}

int __viper_fsd_is_fd(int fd) {
    (void)fd;
    return 0;
}

int __viper_fsd_prepare_path(const char *in, char *out, size_t out_cap) {
    (void)in;
    (void)out;
    (void)out_cap;
    return -1; /* Route to kernel */
}

long __viper_fsd_read(int fd, void *buf, size_t count) {
    (void)fd;
    (void)buf;
    (void)count;
    return -1;
}

long __viper_fsd_write(int fd, const void *buf, size_t count) {
    (void)fd;
    (void)buf;
    (void)count;
    return -1;
}

int __viper_fsd_close(int fd) {
    (void)fd;
    return -1;
}

long __viper_fsd_lseek(int fd, long offset, int whence) {
    (void)fd;
    (void)offset;
    (void)whence;
    return -1;
}

int __viper_fsd_dup(int oldfd) {
    (void)oldfd;
    return -1;
}

int __viper_fsd_dup2(int oldfd, int newfd) {
    (void)oldfd;
    (void)newfd;
    return -1;
}

int __viper_fsd_unlink(const char *abs_path) {
    (void)abs_path;
    return -1;
}

int __viper_fsd_rmdir(const char *abs_path) {
    (void)abs_path;
    return -1;
}

int __viper_fsd_chdir(const char *path) {
    (void)path;
    return -1;
}

int __viper_fsd_getcwd(char *buf, size_t size) {
    (void)buf;
    (void)size;
    return -1;
}

int __viper_fsd_rename(const char *abs_old, const char *abs_new) {
    (void)abs_old;
    (void)abs_new;
    return -1;
}

int __viper_fsd_fsync(int fd) {
    (void)fd;
    return -1;
}

/* dirent.c stubs */
int __viper_fsd_opendir(const char *name, int *out_dir_id) {
    (void)name;
    (void)out_dir_id;
    return -1;
}

int __viper_fsd_readdir(int dir_id, char *out_name, size_t name_cap) {
    (void)dir_id;
    (void)out_name;
    (void)name_cap;
    return -1;
}

int __viper_fsd_closedir(int dir_id) {
    (void)dir_id;
    return -1;
}

/* stat.c stubs */
int __viper_fsd_stat(const char *abs_path, void *out_stat) {
    (void)abs_path;
    (void)out_stat;
    return -1;
}

int __viper_fsd_fstat(int fd, void *out_stat) {
    (void)fd;
    (void)out_stat;
    return -1;
}

/* stdio.c stubs */
int __viper_fsd_open(const char *abs_path, int mode) {
    (void)abs_path;
    (void)mode;
    return -1;
}

int __viper_fsd_mkdir(const char *abs_path, unsigned int mode) {
    (void)abs_path;
    (void)mode;
    return -1;
}

/* netd backend stubs for poll.c */
int __viper_netd_is_available(void) {
    return 0;
}

int __viper_netd_socket_status(int socket_id, int *out_readable, int *out_writable) {
    (void)socket_id;
    (void)out_readable;
    (void)out_writable;
    return -1;
}

int __viper_netd_poll_handle(int socket_id) {
    (void)socket_id;
    return -1;
}
