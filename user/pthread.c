#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

int thread_func(void* arg)
{
    char *s = arg;
    printf("Thread function received argument: %s\n", s);
    return 0;
}

int main()
{
    char *str = "hello world";
    pid_t tid = gettid();
    printf("Main thread TID: %d\n", tid);

    pthread_t thread;
    int ret = pthread_create(&thread, NULL, (void*(*)(void*))thread_func, str);
    if (ret != 0) {
        fprintf(stderr, "pthread_create failed: %d\n", ret);
        return 1;
    }
    printf("New thread created with pthread_create\n");
    pthread_join(thread, NULL);

    return 0;
}
