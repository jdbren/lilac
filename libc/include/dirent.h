#ifndef DIRENT_H
#define DIRENT_H

typedef struct {
    int             d_ino;       /* Inode number */
    unsigned short  d_reclen;    /* Length of this record */
    char            d_name[32]; /* Null-terminated filename */
} dirent;

typedef struct {
    int fd;
    dirent ent;
} DIR;

DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);

int getdents(unsigned int fd, struct dirent *dirp, unsigned int buf_size);


#endif
