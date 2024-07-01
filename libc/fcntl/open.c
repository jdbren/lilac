


int open(const char *pathname, int flags)
{
    int ret;
    asm volatile ("int $0x80"
                  : "=a" (ret)
                  : "a" (5), "b" (pathname), "d" (flags)
                  : "memory");
    return ret;
}