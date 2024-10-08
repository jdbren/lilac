#include <lilac/console.h>
#include <lilac/tty.h>

#include <lilac/fs.h>
#include <mm/kmm.h>
#include <lilac/timer.h>
#include <cpuid.h>
#include <string.h>

static const char *buffer_path = "/dev/console";
int console_fd;

static char line_buf[256];
static char *pos = line_buf;

void console_init(void)
{
    sleep(1000);
    graphics_clear();
    graphics_setcolor(RGB_MAGENTA, RGB_BLACK);
    graphics_writestring("LilacOS v0.1.0\n\n");
    sleep(1000);

    graphics_setcolor(RGB_CYAN, RGB_BLACK);
    unsigned int regs[12];
    char str[sizeof(regs)+1];
    memset(str, 0, sizeof(str));

    __cpuid(0, regs[0], regs[1], regs[2], regs[3]);
    memcpy(str, &regs[1], 4);
    memcpy(str + 4, &regs[3], 4);
    memcpy(str + 8, &regs[2], 4);
    str[12] = '\0';
    printf("CPU Vendor: %s, ", str);

    __cpuid(0x80000000, regs[0], regs[1], regs[2], regs[3]);

    if (regs[0] < 0x80000004)
        return;

    __cpuid(0x80000002, regs[0], regs[1], regs[2], regs[3]);
    __cpuid(0x80000003, regs[4], regs[5], regs[6], regs[7]);
    __cpuid(0x80000004, regs[8], regs[9], regs[10], regs[11]);

    memcpy(str, regs, sizeof(regs));
    str[sizeof(regs)] = '\0';
    printf("%s\n", str);

    __cpuid(0x80000005, regs[0], regs[1], regs[2], regs[3]);
    printf("Cache line: %u bytes, ", regs[2] & 0xff);
    printf("L1 Cache: %u KB, ", regs[2] >> 24);

    __cpuid(0x80000006, regs[0], regs[1], regs[2], regs[3]);
    printf("L2 Cache: %u KB\n", regs[2] >> 16);

    u32 mem_sz_mb = arch_get_mem_sz() / 0x400;
    printf("Memory: %u MB\n", mem_sz_mb);

    u64 sys_time_ms = get_sys_time() / 1000000;
    printf("System clock running for %llu ms\n\n", sys_time_ms);
    graphics_setcolor(RGB_WHITE, RGB_BLACK);
}

void console_write_char(char c)
{
    *pos++ = c;

    // if (c == '\n') {
    //     *pos = '\0';
    //     write(console_fd, line_buf, strlen(line_buf));
    //     pos = line_buf;
    // }

    graphics_putchar(c);
}
