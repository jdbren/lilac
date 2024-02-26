#include <kernel/types.h>

void __do_syscall(int num)
{

}

void do_syscall(int num)
{
    printf("Syscall %d\n", num);
}