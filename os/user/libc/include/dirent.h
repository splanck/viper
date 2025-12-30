#ifndef _DIRENT_H
#define _DIRENT_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
typedef unsigned long size_t;
#endif

/* Directory entry types */
#define DT_UNKNOWN 0
#define DT_REG     1  /* Regular file */
#define DT_DIR     2  /* Directory */

/* Maximum name length */
#define NAME_MAX 255

/* Directory entry structure */
struct dirent {
    unsigned long d_ino;      /* Inode number */
    unsigned char d_type;     /* File type (DT_REG, DT_DIR, etc.) */
    char d_name[NAME_MAX+1];  /* Null-terminated filename */
};

/* Opaque directory stream type */
typedef struct _DIR DIR;

/* Directory operations */
DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);
void rewinddir(DIR *dirp);

/* Get file descriptor from DIR (extension) */
int dirfd(DIR *dirp);

#ifdef __cplusplus
}
#endif

#endif /* _DIRENT_H */
