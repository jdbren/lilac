#include <lilac/types.h>
#include <asm/io.h>

#define PIT_CH0         0x40
#define PIT_CMD         0x43
#define PIT_MODE2       0x04
#define PIT_LATCH       0x00
#define PIT_ACCESS_LOHI 0x30

struct pit_clock {
    u16 last;
    u32 reload;
    u64 ticks;
} pit;


void pit_init(void)
{
    pit.reload = UINT16_MAX;
    pit.last = UINT16_MAX;
    pit.ticks = 0;

    /* Command: channel 0, lo/hi, mode 2, binary */
    outb(PIT_CMD, PIT_ACCESS_LOHI | PIT_MODE2);

    outb(PIT_CH0, UINT16_MAX & 0xff);
    outb(PIT_CH0, UINT16_MAX >> 8);
}

static inline u16 pit_read_counter(void)
{
    outb(PIT_CMD, PIT_LATCH);   // latch channel 0
    u8 lo = inb(PIT_CH0);
    u8 hi = inb(PIT_CH0);
    return (hi << 8) | lo;
}

static void pit_update(struct pit_clock *pc)
{
    u16 cur = pit_read_counter();

    if (cur > pc->last) {
        // wrapped
        pc->ticks += pc->last + (pc->reload - cur);
    } else {
        pc->ticks += pc->last - cur;
    }

    pc->last = cur;
}

u64 pit_read_ticks(void)
{
    pit_update(&pit);
    return pit.ticks;
}
