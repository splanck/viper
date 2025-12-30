/*
 * ViperOS C Library - tar.h
 * tar archive format constants
 * Defined by POSIX.1-2017 (USTAR format)
 */

#ifndef _TAR_H
#define _TAR_H

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * General definitions
 */

/* Size of a tar block */
#define TBLOCK 512

/* Size of the header block */
#define TBLOCKSIZE 512

/* Number of blocks in a record (for blocking factor) */
#define TRECORDSIZE 10240

/*
 * USTAR header magic and version
 */

/* Magic value "ustar\0" - null terminated for POSIX compliance */
#define TMAGIC "ustar"
#define TMAGLEN 6

/* Version "00" - not null terminated */
#define TVERSION "00"
#define TVERSLEN 2

/* Old (pre-POSIX) tar magic - for compatibility */
#define OLDMAGIC "ustar  "

    /*
     * File type flags (for typeflag field)
     */

#define REGTYPE '0'   /* Regular file */
#define AREGTYPE '\0' /* Regular file (old format, null byte) */
#define LNKTYPE '1'   /* Hard link */
#define SYMTYPE '2'   /* Symbolic link */
#define CHRTYPE '3'   /* Character special device */
#define BLKTYPE '4'   /* Block special device */
#define DIRTYPE '5'   /* Directory */
#define FIFOTYPE '6'  /* FIFO (named pipe) */
#define CONTTYPE '7'  /* Contiguous file (reserved, rarely used) */

/* Extended header types (POSIX.1-2001 pax format) */
#define XHDTYPE 'x' /* Extended header with meta data for next file */
#define XGLTYPE 'g' /* Global extended header with meta data */

/* GNU tar extensions */
#define GNUTYPE_DUMPDIR 'D'  /* Directory dump */
#define GNUTYPE_LONGLINK 'K' /* Long link name */
#define GNUTYPE_LONGNAME 'L' /* Long file name */
#define GNUTYPE_MULTIVOL 'M' /* Multi-volume continuation */
#define GNUTYPE_SPARSE 'S'   /* Sparse file */
#define GNUTYPE_VOLHDR 'V'   /* Volume header */

/* Solaris tar extensions */
#define SOLARIS_XHDTYPE 'X' /* Solaris extended header */

    /*
     * Mode field bits (permission bits, same as stat.h)
     */

#define TSUID 04000 /* Set UID on execution */
#define TSGID 02000 /* Set GID on execution */
#define TSVTX 01000 /* Sticky bit (save text) */

/* Owner permissions */
#define TUREAD 00400  /* Read by owner */
#define TUWRITE 00200 /* Write by owner */
#define TUEXEC 00100  /* Execute/search by owner */

/* Group permissions */
#define TGREAD 00040  /* Read by group */
#define TGWRITE 00020 /* Write by group */
#define TGEXEC 00010  /* Execute/search by group */

/* Other permissions */
#define TOREAD 00004  /* Read by others */
#define TOWRITE 00002 /* Write by others */
#define TOEXEC 00001  /* Execute/search by others */

    /*
     * Field sizes in USTAR header
     */

#define TNAMELEN 100   /* Name field length */
#define TMODELEN 8     /* Mode field length */
#define TUIDLEN 8      /* UID field length */
#define TGIDLEN 8      /* GID field length */
#define TSIZELEN 12    /* Size field length */
#define TMTIMELEN 12   /* Modification time field length */
#define TCHKSUMLEN 8   /* Checksum field length */
#define TLINKLEN 100   /* Link name field length */
#define TMAGICLEN 6    /* Magic field length (including null) */
#define TVERSIONLEN 2  /* Version field length */
#define TUNAMELEN 32   /* User name field length */
#define TGNAMELEN 32   /* Group name field length */
#define TDEVLEN 8      /* Device major/minor field length */
#define TPREFIXLEN 155 /* Prefix field length */

    /*
     * POSIX USTAR header structure
     * Total size is exactly 512 bytes
     */
    struct posix_header
    {
        char name[100];     /* File name (NUL-terminated) */
        char mode[8];       /* File mode (octal, ASCII) */
        char uid[8];        /* User ID (octal, ASCII) */
        char gid[8];        /* Group ID (octal, ASCII) */
        char size[12];      /* File size (octal, ASCII) */
        char mtime[12];     /* Modification time (octal, ASCII) */
        char chksum[8];     /* Header checksum (octal, ASCII) */
        char typeflag;      /* File type flag */
        char linkname[100]; /* Link target name */
        char magic[6];      /* "ustar\0" */
        char version[2];    /* "00" */
        char uname[32];     /* User name (NUL-terminated) */
        char gname[32];     /* Group name (NUL-terminated) */
        char devmajor[8];   /* Device major number (octal) */
        char devminor[8];   /* Device minor number (octal) */
        char prefix[155];   /* Prefix for long names */
        char padding[12];   /* Padding to 512 bytes */
    };

/*
 * Checksum calculation
 * The checksum is the sum of all bytes in the header,
 * with the checksum field treated as spaces (0x20).
 * Stored as 6 octal digits + space + null (or null + space).
 */
#define TCHKSUM_SPACE 256 /* Sum of 8 space characters */

#ifdef __cplusplus
}
#endif

#endif /* _TAR_H */
