/**
 * @file sftp.h
 * @brief SFTP client library for ViperOS.
 *
 * Implements SSH File Transfer Protocol (SFTP) version 3.
 * Provides file operations over an SSH channel.
 */

#ifndef _SFTP_H
#define _SFTP_H

#include "ssh.h"
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SFTP error codes */
typedef enum {
    SFTP_OK = 0,
    SFTP_EOF = 1,
    SFTP_NO_SUCH_FILE = 2,
    SFTP_PERMISSION_DENIED = 3,
    SFTP_FAILURE = 4,
    SFTP_BAD_MESSAGE = 5,
    SFTP_NO_CONNECTION = 6,
    SFTP_CONNECTION_LOST = 7,
    SFTP_OP_UNSUPPORTED = 8,
    SFTP_INVALID_HANDLE = 9,
    SFTP_NO_SUCH_PATH = 10,
    SFTP_FILE_ALREADY_EXISTS = 11,
    SFTP_WRITE_PROTECT = 12,
    SFTP_NO_MEDIA = 13,
} sftp_error_t;

/* SFTP file open flags */
#define SFTP_READ       0x00000001
#define SFTP_WRITE      0x00000002
#define SFTP_APPEND     0x00000004
#define SFTP_CREAT      0x00000008
#define SFTP_TRUNC      0x00000010
#define SFTP_EXCL       0x00000020

/* SFTP file types */
typedef enum {
    SFTP_TYPE_REGULAR = 1,
    SFTP_TYPE_DIRECTORY = 2,
    SFTP_TYPE_SYMLINK = 3,
    SFTP_TYPE_SPECIAL = 4,
    SFTP_TYPE_UNKNOWN = 5,
    SFTP_TYPE_SOCKET = 6,
    SFTP_TYPE_CHAR_DEVICE = 7,
    SFTP_TYPE_BLOCK_DEVICE = 8,
    SFTP_TYPE_FIFO = 9,
} sftp_filetype_t;

/* Forward declarations */
typedef struct sftp_session sftp_session_t;
typedef struct sftp_file sftp_file_t;
typedef struct sftp_dir sftp_dir_t;

/* SFTP file attributes */
typedef struct sftp_attributes {
    char *name;           /* Filename (for directory listings) */
    char *longname;       /* Long format name (ls -l style) */
    uint32_t flags;       /* Valid fields bitmask */
    uint64_t size;        /* File size */
    uint32_t uid;         /* User ID */
    uint32_t gid;         /* Group ID */
    uint32_t permissions; /* POSIX permissions */
    uint32_t atime;       /* Access time */
    uint32_t mtime;       /* Modification time */
    sftp_filetype_t type; /* File type */
} sftp_attributes_t;

/* Attribute flags */
#define SFTP_ATTR_SIZE        0x00000001
#define SFTP_ATTR_UIDGID      0x00000002
#define SFTP_ATTR_PERMISSIONS 0x00000004
#define SFTP_ATTR_ACMODTIME   0x00000008

/*=============================================================================
 * Session Management
 *===========================================================================*/

/**
 * @brief Create a new SFTP session over SSH.
 * @param ssh Authenticated SSH session.
 * @return New SFTP session or NULL on error.
 */
sftp_session_t *sftp_new(ssh_session_t *ssh);

/**
 * @brief Initialize SFTP session (open subsystem).
 * @param sftp SFTP session.
 * @return SFTP_OK on success.
 */
int sftp_init(sftp_session_t *sftp);

/**
 * @brief Free SFTP session.
 * @param sftp Session to free.
 */
void sftp_free(sftp_session_t *sftp);

/**
 * @brief Get last SFTP error code.
 * @param sftp SFTP session.
 * @return Error code.
 */
sftp_error_t sftp_get_error(sftp_session_t *sftp);

/*=============================================================================
 * File Operations
 *===========================================================================*/

/**
 * @brief Open a remote file.
 * @param sftp SFTP session.
 * @param path Remote file path.
 * @param flags Open flags (SFTP_READ, SFTP_WRITE, etc.).
 * @param mode Permissions for new files.
 * @return File handle or NULL on error.
 */
sftp_file_t *sftp_open(sftp_session_t *sftp, const char *path, int flags, mode_t mode);

/**
 * @brief Close a file.
 * @param file File to close.
 * @return SFTP_OK on success.
 */
int sftp_close(sftp_file_t *file);

/**
 * @brief Read from a file.
 * @param file Open file.
 * @param buffer Buffer for data.
 * @param count Maximum bytes to read.
 * @return Bytes read, 0 on EOF, <0 on error.
 */
ssize_t sftp_read(sftp_file_t *file, void *buffer, size_t count);

/**
 * @brief Write to a file.
 * @param file Open file.
 * @param buffer Data to write.
 * @param count Bytes to write.
 * @return Bytes written or <0 on error.
 */
ssize_t sftp_write(sftp_file_t *file, const void *buffer, size_t count);

/**
 * @brief Seek within a file.
 * @param file Open file.
 * @param offset New position.
 * @return SFTP_OK on success.
 */
int sftp_seek(sftp_file_t *file, uint64_t offset);

/**
 * @brief Get current file position.
 * @param file Open file.
 * @return Current offset.
 */
uint64_t sftp_tell(sftp_file_t *file);

/**
 * @brief Rewind to beginning of file.
 * @param file Open file.
 */
void sftp_rewind(sftp_file_t *file);

/**
 * @brief Get file attributes.
 * @param sftp SFTP session.
 * @param path Remote file path.
 * @return Attributes or NULL on error. Must be freed with sftp_attributes_free().
 */
sftp_attributes_t *sftp_stat(sftp_session_t *sftp, const char *path);

/**
 * @brief Get file attributes (don't follow symlinks).
 * @param sftp SFTP session.
 * @param path Remote file path.
 * @return Attributes or NULL on error.
 */
sftp_attributes_t *sftp_lstat(sftp_session_t *sftp, const char *path);

/**
 * @brief Get attributes of open file.
 * @param file Open file.
 * @return Attributes or NULL on error.
 */
sftp_attributes_t *sftp_fstat(sftp_file_t *file);

/**
 * @brief Set file attributes.
 * @param sftp SFTP session.
 * @param path Remote file path.
 * @param attr Attributes to set.
 * @return SFTP_OK on success.
 */
int sftp_setstat(sftp_session_t *sftp, const char *path, sftp_attributes_t *attr);

/**
 * @brief Free attributes structure.
 * @param attr Attributes to free.
 */
void sftp_attributes_free(sftp_attributes_t *attr);

/*=============================================================================
 * Directory Operations
 *===========================================================================*/

/**
 * @brief Open a directory for reading.
 * @param sftp SFTP session.
 * @param path Remote directory path.
 * @return Directory handle or NULL on error.
 */
sftp_dir_t *sftp_opendir(sftp_session_t *sftp, const char *path);

/**
 * @brief Read next directory entry.
 * @param dir Open directory.
 * @return Entry attributes or NULL at end. Must be freed.
 */
sftp_attributes_t *sftp_readdir(sftp_dir_t *dir);

/**
 * @brief Check if at end of directory.
 * @param dir Open directory.
 * @return 1 if at end, 0 otherwise.
 */
int sftp_dir_eof(sftp_dir_t *dir);

/**
 * @brief Close a directory.
 * @param dir Directory to close.
 * @return SFTP_OK on success.
 */
int sftp_closedir(sftp_dir_t *dir);

/**
 * @brief Create a directory.
 * @param sftp SFTP session.
 * @param path Remote directory path.
 * @param mode Permissions.
 * @return SFTP_OK on success.
 */
int sftp_mkdir(sftp_session_t *sftp, const char *path, mode_t mode);

/**
 * @brief Remove a directory.
 * @param sftp SFTP session.
 * @param path Remote directory path.
 * @return SFTP_OK on success.
 */
int sftp_rmdir(sftp_session_t *sftp, const char *path);

/*=============================================================================
 * File Management
 *===========================================================================*/

/**
 * @brief Remove a file.
 * @param sftp SFTP session.
 * @param path Remote file path.
 * @return SFTP_OK on success.
 */
int sftp_unlink(sftp_session_t *sftp, const char *path);

/**
 * @brief Rename a file.
 * @param sftp SFTP session.
 * @param oldpath Current path.
 * @param newpath New path.
 * @return SFTP_OK on success.
 */
int sftp_rename(sftp_session_t *sftp, const char *oldpath, const char *newpath);

/**
 * @brief Change file permissions.
 * @param sftp SFTP session.
 * @param path Remote file path.
 * @param mode New permissions.
 * @return SFTP_OK on success.
 */
int sftp_chmod(sftp_session_t *sftp, const char *path, mode_t mode);

/**
 * @brief Change file owner.
 * @param sftp SFTP session.
 * @param path Remote file path.
 * @param uid New user ID.
 * @param gid New group ID.
 * @return SFTP_OK on success.
 */
int sftp_chown(sftp_session_t *sftp, const char *path, uid_t uid, gid_t gid);

/**
 * @brief Set file modification times.
 * @param sftp SFTP session.
 * @param path Remote file path.
 * @param atime Access time.
 * @param mtime Modification time.
 * @return SFTP_OK on success.
 */
int sftp_utimes(sftp_session_t *sftp, const char *path, uint32_t atime, uint32_t mtime);

/*=============================================================================
 * Symbolic Links
 *===========================================================================*/

/**
 * @brief Create a symbolic link.
 * @param sftp SFTP session.
 * @param target Link target.
 * @param dest Link path.
 * @return SFTP_OK on success.
 */
int sftp_symlink(sftp_session_t *sftp, const char *target, const char *dest);

/**
 * @brief Read a symbolic link.
 * @param sftp SFTP session.
 * @param path Link path.
 * @return Target path or NULL on error. Must be freed.
 */
char *sftp_readlink(sftp_session_t *sftp, const char *path);

/**
 * @brief Resolve a path (expand symlinks, . and ..).
 * @param sftp SFTP session.
 * @param path Path to resolve.
 * @return Resolved path or NULL on error. Must be freed.
 */
char *sftp_realpath(sftp_session_t *sftp, const char *path);

/*=============================================================================
 * Utility Functions
 *===========================================================================*/

/**
 * @brief Get current working directory.
 * @param sftp SFTP session.
 * @return Current path or NULL. Must be freed.
 */
char *sftp_getcwd(sftp_session_t *sftp);

/**
 * @brief Canonicalize a path.
 * @param sftp SFTP session.
 * @param path Path to canonicalize.
 * @return Canonical path or NULL. Must be freed.
 */
char *sftp_canonicalize_path(sftp_session_t *sftp, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* _SFTP_H */
