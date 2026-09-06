#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <string.h>
#include <stdlib.h>

int main(void)
{
    size_t page_size = 4096;
    char *private = mmap(NULL, page_size * 2, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (private == MAP_FAILED)
        return 1;

    strcpy(private, "parent-page-0");
    strcpy(private + page_size, "parent-page-1");

    int parent_to_child[2], child_to_parent[2];
    pipe(parent_to_child);
    pipe(child_to_parent);

    pid_t pid = fork();
    if (pid == 0) {
        char token;
        read(parent_to_child[0], &token, 1);

        /* Parent's post-fork write must not be visible. */
        if (strcmp(private, "parent-after-fork") == 0)
            _exit(2);

        strcpy(private + page_size, "child-after-fork");
        write(child_to_parent[1], "x", 1);

        read(parent_to_child[0], &token, 1);

        /* Child's write must remain private. */
        if (strcmp(private + page_size, "child-after-fork") != 0)
            _exit(3);
        _exit(0);
    }

    strcpy(private, "parent-after-fork");
    write(parent_to_child[1], "x", 1);

    char token;
    read(child_to_parent[0], &token, 1);

    /* Child's write must not be visible to parent. */
    if (strcmp(private + page_size, "parent-page-1") != 0)
        return 4;

    write(parent_to_child[1], "x", 1);

    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 5;
}
