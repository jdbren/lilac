#ifndef LILAC_PIPE_H
#define LILAC_PIPE_H

#include <lilac/sync.h>
#include <lilac/wait.h>

struct file;
struct inode;

struct pipe_buf {
    char *buffer;               // data buffer
    unsigned int buf_size;      // size of buffer
    unsigned int read_pos;      // read position in buffer
    unsigned int write_pos;     // write position in buffer
    unsigned int data_size;     // amount of data currently in buffer
    struct inode *p_inode;
    struct waitqueue read_wq;   // wait queue for readers
    struct waitqueue write_wq;  // wait queue for writers
    spinlock_t lock;            // lock to protect pipe structure
    unsigned int files;         // number of open file handles
    unsigned int n_readers;     // number of readers
    unsigned int n_writers;     // number of writers
};

#endif // LILAC_PIPE_H
