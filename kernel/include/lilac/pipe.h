#ifndef LILAC_PIPE_H
#define LILAC_PIPE_H

#include <lilac/sync.h>
#include <lilac/wait.h>

struct file;
struct inode;

union pipe_index {
    unsigned long read_write;
    struct {
        unsigned int read_pos;      // read position in buffer
        unsigned int write_pos;     // write position in buffer
    };
};

struct pipe_buf {
    char *buffer;               // data buffer
    unsigned int buf_size;      // size of buffer
    union pipe_index idx;       // read and write positions in buffer
    unsigned int data_size;     // amount of data currently in buffer
};

struct pipe {
    struct pipe_buf pb;
    struct inode *p_inode;
    struct waitqueue read_wq;   // wait queue for readers
    struct waitqueue write_wq;  // wait queue for writers
    spinlock_t lock;            // lock to protect pipe structure
    unsigned int files;         // number of open file handles
    unsigned int n_readers;     // number of readers
    unsigned int n_writers;     // number of writers
};

unsigned int pipe_buf_out(struct pipe_buf *p, void *buf, size_t count);
unsigned int pipe_buf_in(struct pipe_buf *p, const void *buf, size_t count);

#endif // LILAC_PIPE_H
