#ifndef _LILAC_FDTABLE_H
#define _LILAC_FDTABLE_H

#ifdef __cplusplus
extern "C" {
#endif

struct file;

struct fdtable {
    struct file **fdarray;
    unsigned int max;
};

struct file * get_file_handle(int fd);
int get_next_fd(struct fdtable *, struct file *);
int get_fd_at(struct fdtable *files, int start);

#ifdef __cplusplus
}
#endif

#endif
