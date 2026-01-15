/*
 * ViperDOS C Library - cpio.h
 * cpio archive format constants
 * Defined by POSIX.1-2017
 */

#ifndef _CPIO_H
#define _CPIO_H

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Magic numbers for cpio formats
 */

/* Binary format magic (little-endian) */
#define CPIO_BIN_MAGIC 0x71c7

/* ASCII (odc) format magic - portable character archive */
#define MAGIC "070707"

/* SVR4 ASCII with CRC format magic */
#define CMS_ASC "070701" /* SVR4 ASCII without CRC */
#define CMS_CHR "070702" /* SVR4 ASCII with CRC */
#define CMS_CRC "070702" /* Same as CMS_CHR */

/* newc format magic (same as SVR4) */
#define CPIO_NEWC_MAGIC "070701"
#define CPIO_CRC_MAGIC "070702"

    /*
     * File type constants for c_mode field
     * These are the same as the S_IF* constants from <sys/stat.h>
     * masked with C_ISMT
     */

#define C_IRUSR 0000400 /* Read by owner */
#define C_IWUSR 0000200 /* Write by owner */
#define C_IXUSR 0000100 /* Execute by owner */
#define C_IRGRP 0000040 /* Read by group */
#define C_IWGRP 0000020 /* Write by group */
#define C_IXGRP 0000010 /* Execute by group */
#define C_IROTH 0000004 /* Read by others */
#define C_IWOTH 0000002 /* Write by others */
#define C_IXOTH 0000001 /* Execute by others */

#define C_ISUID 0004000 /* Set user ID on execution */
#define C_ISGID 0002000 /* Set group ID on execution */
#define C_ISVTX 0001000 /* Sticky bit */

#define C_ISDIR 0040000  /* Directory */
#define C_ISFIFO 0010000 /* FIFO */
#define C_ISREG 0100000  /* Regular file */
#define C_ISBLK 0060000  /* Block special */
#define C_ISCHR 0020000  /* Character special */
#define C_ISCTG 0110000  /* Contiguous file (reserved) */
#define C_ISLNK 0120000  /* Symbolic link */
#define C_ISSOCK 0140000 /* Socket */

/* Mask for extracting file type */
#define C_ISMT 0170000

    /*
     * Binary cpio header structure (old format)
     * Note: This format is machine-dependent due to byte ordering
     */
    struct cpio_binary_header
    {
        unsigned short c_magic;       /* Magic number */
        unsigned short c_dev;         /* Device number */
        unsigned short c_ino;         /* Inode number */
        unsigned short c_mode;        /* File mode */
        unsigned short c_uid;         /* User ID */
        unsigned short c_gid;         /* Group ID */
        unsigned short c_nlink;       /* Number of links */
        unsigned short c_rdev;        /* Device type (if special file) */
        unsigned short c_mtime[2];    /* Modification time */
        unsigned short c_namesize;    /* Length of pathname */
        unsigned short c_filesize[2]; /* File size */
        /* Followed by pathname and file data */
    };

/*
 * ASCII (odc) cpio header - portable format
 * Uses 6-character octal ASCII for numeric fields
 * Total header size is 76 bytes before pathname
 */
#define CPIO_ODC_HEADER_SIZE 76

/*
 * SVR4/newc ASCII cpio header format
 * Uses 8-character hexadecimal ASCII for numeric fields
 * Total header size is 110 bytes before pathname
 */
#define CPIO_NEWC_HEADER_SIZE 110

/*
 * Trailer filename marking end of archive
 */
#define CPIO_TRAILER "TRAILER!!!"

#ifdef __cplusplus
}
#endif

#endif /* _CPIO_H */
