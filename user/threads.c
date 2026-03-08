#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <sys/mman.h>

pid_t child_set_tid;

int thread_func(void* arg)
{
    char *s = arg;
    printf("Thread function received argument: %s\n", s);
    printf("Child set TID: %d\n", child_set_tid);
    return 0;
}

int main()
{
    const size_t stack_size = 1024*1024;
    void *tstack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    char *str = "hello world";
    pid_t tid = gettid();

    pid_t new_tid = clone(thread_func, tstack + stack_size,
        CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|
        CLONE_VFORK|CLONE_CHILD_SETTID,
        str, NULL, NULL, &child_set_tid);
    printf("New thread ID: %d\n", new_tid);
    return 0;
}
