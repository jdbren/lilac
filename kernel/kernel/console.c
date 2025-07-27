#include <lilac/console.h>
#include <lilac/lilac.h>
#include <lilac/keyboard.h>
#include <lilac/signal.h>
#include <lilac/timer.h>
#include <lilac/device.h>
#include <lilac/sched.h>
#include <lilac/wait.h>
#include <lilac/fs.h>
#include <drivers/framebuffer.h>
#include <mm/kmm.h>
#include <utility/keymap.h>

#include <cpuid.h>


static struct console consoles[5] = {0};
static unsigned active_console = 0;
#if (!defined(DEBUG_KMM) && !defined(DEBUG_PAGING))
int write_to_screen = 1;
#else
int write_to_screen = 0;
#endif

struct file_operations console_fops = {
    .read = console_read,
    .write = console_write
};

void init_con(int num)
{
    struct console *con = &consoles[num];
    con->cx = 0;
    con->cy = 0;
    con->height = 30;
    con->width = 80;
}

void console_newline(struct console *con)
{
    con->cx = 0;
    if ((con->cy += 1) >= con->height) {
        graphics_scroll();
        con->cy = con->height - 1;
    }
}

void console_putchar(struct console *con, int c)
{
    if (c == '\n') {
        console_newline(con);
        return;
    } else if (c == '\t') {
        do {
            console_putchar(con, ' ');
        } while (con->cx % 8 && con->cx < con->width);
        return;
    } else if (c == '\b') {
        if (con->cx > 0) {
            con->cx--;
            graphics_putc(' ', con->cx, con->cy);
        }
        return;
    }
    graphics_putc((u16)c, con->cx, con->cy);
    if (++(con->cx) >= con->width) {
        console_newline(con);
    }
}

void console_writestring(struct console *con, const char *data)
{
    while (*data) {
        console_putchar(con, *data++);
    }
}

void console_clear(struct console *con)
{
    graphics_clear();
    con->cx = 0;
    con->cy = 0;
}

void console_init(void)
{
    add_device("/dev/console", &console_fops);
    set_console(1);

    struct console *con = &consoles[active_console];

    console_clear(con);
    graphics_setcolor(RGB_MAGENTA, RGB_BLACK);
    console_writestring(con, "LilacOS v0.1.0\n\n");

    graphics_setcolor(RGB_CYAN, RGB_BLACK);
    unsigned int regs[12];
    char str[sizeof(regs) + 1];
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

    size_t mem_sz_mb = arch_get_mem_sz() / 0x400;
    printf("Memory: %u MB\n", mem_sz_mb);

    u64 sys_time_ms = get_sys_time() / 1000000;
    printf("System clock running for %llu ms\n", sys_time_ms);
    struct timestamp ts = get_timestamp();
    printf("Current time: " TIME_FORMAT "\n\n",
        ts.year, ts.month, ts.day, ts.hour, ts.minute, ts.second);
    graphics_setcolor(RGB_WHITE, RGB_BLACK);
    write_to_screen = 0;
}


static struct waitqueue console_wq = {
    .lock = SPINLOCK_INIT,
    .task_list = LIST_HEAD_INIT(console_wq.task_list)
};

ssize_t console_read(struct file *file, void *buf, size_t count)
{
    u32 target;
    int c;
    char cbuf;
    struct console *con = &consoles[active_console];

    target = count;
    while(count > 0) {
        // wait until interrupt handler has put some
        // input into cons.buffer.
        if (con->input.rpos == con->input.wpos) {
            klog(LOG_DEBUG, "console_read: proc %d waiting for input\n", get_pid());
            sleep_on(&console_wq);
        }

        c = con->input.buf[con->input.rpos++ % INPUT_BUF_SIZE];

        // if(c == C('D')){  // end-of-file
        //     if(count < target){
        //         // Save ^D for next time, to make sure
        //         // caller gets a 0-byte result.
        //         con.rpos--;
        //     }
        //     break;
        // }

        // copy the input byte to the buffer.
        cbuf = c;
        memcpy(buf, &cbuf, 1);

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
    struct console *con = &consoles[active_console];
    for (size_t i = 0; i < count; i++)
        console_putchar(con, bufp[i] & 0xff);

    return count;
}

void console_intr(struct kbd_event event)
{
    char c = keyboard_map[event.keycode];
    struct console *con = &consoles[active_console];

    if (event.status & KB_ALT) {
        // if (c == '1')
        //     graphics_setfb(0);
        // else if (c == '2')
        //     graphics_setfb(1);
        // return;
    } else if (event.status & KB_CTRL) {
        switch(c) {
        case 'c':
            console_writestring(con, "^C\n");
            do_raise(current, SIGINT);
            break;
        case 'd':
            console_writestring(con, "^D\n");
            break;
        case '\\':
            console_writestring(con, "^\\\n");
            do_raise(current, SIGQUIT);
            break;
        }
        return;
    } else if (event.status & KB_SHIFT) {
        c = keyboard_map_shift[event.keycode];
    }
    switch(c) {
    case '\b': // Backspace
    case '\x7f': // Delete key
        if(con->input.epos > con->input.wpos) {
            con->input.epos--;
            console_putchar(con, c);
        }
    break;
    default:
        if (c != 0 && con->input.epos - con->input.rpos < INPUT_BUF_SIZE) {
            c = (c == '\r') ? '\n' : c;

            // store for consumption by consoleread().
            con->input.buf[con->input.epos++ % INPUT_BUF_SIZE] = c;

            // echo back to the user.
            console_putchar(con, c);

            if (c == '\n' || con->input.epos - con->input.rpos == INPUT_BUF_SIZE) {
                // wake up console_read() if a whole line (or end-of-file)
                // has arrived.
                con->input.wpos = con->input.epos;
                wake_first(&console_wq);
            }
        }
    }
}
