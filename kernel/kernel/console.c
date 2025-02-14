#include <lilac/console.h>
#include <lilac/tty.h>
#include <lilac/keyboard.h>
#include <lilac/log.h>
#include <lilac/lilac.h>
#include <lilac/device.h>
#include <lilac/sched.h>
#include <lilac/process.h>

#include <lilac/fs.h>
#include <mm/kmm.h>
#include <lilac/timer.h>
#include <cpuid.h>
#include <string.h>

#define INPUT_BUF_SIZE 256

struct console {
    char buf[INPUT_BUF_SIZE];
    int rpos;
    int wpos;
    int epos;
};

static struct console con;

struct file_operations console_fops = {
    .read = console_read,
    .write = console_write
};

void console_init(void)
{
    add_device("/dev/console", &console_fops);
    set_console(1);

    sleep(500);
    graphics_clear();
    graphics_setcolor(RGB_MAGENTA, RGB_BLACK);
    graphics_writestring("LilacOS v0.1.0\n\n");

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
    printf("System clock running for %llu ms\n", sys_time_ms);
    struct timestamp ts = get_timestamp();
    printf("Current time: " TIME_FORMAT "\n\n",
        ts.year, ts.month, ts.day, ts.hour, ts.minute, ts.second);
    graphics_setcolor(RGB_WHITE, RGB_BLACK);
    sleep(500);
}

static u32 io_queue[8];

static void add_to_io_queue(u32 pid)
{
    for (int i = 0; i < 8; i++) {
        if (io_queue[i] == 0) {
            io_queue[i] = pid;
            return;
        }
    }
}

static u32 pop_io_queue(void)
{
    u32 pid = io_queue[0];
    for (int i = 0; i < 7; i++)
        io_queue[i] = io_queue[i+1];
    io_queue[7] = 0;
    return pid;
}

ssize_t console_read(struct file *file, void *buf, size_t count)
{
    u32 target;
    int c;
    char cbuf;

    target = count;
    while(count > 0){
        // wait until interrupt handler has put some
        // input into cons.buffer.
        if (con.rpos == con.wpos) {
            klog(LOG_DEBUG, "console_read: proc %d waiting for input\n", get_pid());
            arch_enable_interrupts();
            current->state = TASK_SLEEPING;
            add_to_io_queue(get_pid());
            yield();
        }

        c = con.buf[con.rpos++ % INPUT_BUF_SIZE];

        // if(c == C('D')){  // end-of-file
        //     if(count < target){
        //         // Save ^D for next time, to make sure
        //         // caller gets a 0-byte result.
        //         con.rpos--;
        //     }
        //     break;
        // }

        // copy the input byte to the user-space buffer.
        cbuf = c;
        if(memcpy(buf, &cbuf, 1) == -1)
            break;

        buf++;
        --count;

        if(c == '\n')
            break;
    }

    return target - count;
}

ssize_t console_write(struct file *file, const void *buf, size_t count)
{
    char *bufp = (char*)buf;
    int i;
    for(i = 0; i < count; i++)
        graphics_putchar(bufp[i] & 0xff);

    return count;
}

void console_intr(char c)
{
    switch(c) {
    case '\b': // Backspace
    //case '\x7f': // Delete key
        if(con.epos > con.wpos){
            con.epos--;
            graphics_putchar('\b');
        }
    break;
    default:
        if (c != 0 && con.epos-con.rpos < INPUT_BUF_SIZE) {
            c = (c == '\r') ? '\n' : c;

            // echo back to the user.
            graphics_putchar(c);

            // store for consumption by consoleread().
            con.buf[con.epos++ % INPUT_BUF_SIZE] = c;

            if (c == '\n' || con.epos-con.rpos == INPUT_BUF_SIZE) {
                // wake up consoleread() if a whole line (or end-of-file)
                // has arrived.
                con.wpos = con.epos;
                u32 pid = pop_io_queue();
                wakeup(pid);
            }
        }
    }
}
