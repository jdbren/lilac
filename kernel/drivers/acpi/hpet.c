#include <acpi/hpet.h>
#include <mm/kmm.h>
#include <mm/kmalloc.h>
#include <lilac/log.h>
#include <lilac/panic.h>

#define HPET_ID_REG 0x0
#define HPET_CONFIG_REG 0x10
#define HPET_INTR_REG 0x20
#define HPET_COUNTER_REG 0xf0
#define HPET_TIMER_CONF_REG(N) (0x100 + 0x20 * N)
#define HPET_TIMER_COMP_REG(N) (0x108 + 0x20 * N)

struct __packed hpet_id_reg {
    u8 rev_id;
    u8 flags;
    u16 vendor_id;
    u32 counter_clk_period;
};

struct hpet_info *parse_hpet(struct SDTHeader *addr)
{
    struct hpet_info *info = kmalloc(sizeof(*info));
    struct hpet *hpet = (struct hpet*)addr;

    info->address = hpet->address.address;
    info->comparator_count = hpet->comparator_count;
    info->hpet_number = hpet->hpet_number;
    info->minimum_tick = hpet->minimum_tick;
    info->page_protection = hpet->page_protection;

    return info;
}

int dealloc_hpet(struct hpet_info *info)
{
    kfree(info);
    return 0;
}

static int hpet_number;
static uintptr_t hpet_base;
static u32 hpet_clk_period;
static u64 hpet_frq;

static inline u64 read_reg(const u32 offset)
{
    return *(volatile u64*)(hpet_base + offset);
}

static inline u32 read_reg32(const u32 offset)
{
    return *(volatile u32*)(hpet_base + offset);
}

static inline void write_reg(const u32 offset, const u64 val)
{
    *(volatile u64*)(hpet_base + offset) = val;
}

// time in ms
void hpet_init(struct hpet_info *info)
{
    hpet_base = (uintptr_t)map_phys((void*)(uintptr_t)info->address, PAGE_SIZE,
        PG_WRITE | PG_STRONG_UC);

    hpet_number = info->hpet_number;

    struct hpet_id_reg *id = (struct hpet_id_reg*)hpet_base;
    hpet_clk_period = id->counter_clk_period;
    hpet_frq = 1'000'000'000'000'000ULL / hpet_clk_period; // in Hz

    write_reg(HPET_CONFIG_REG, 0x0); // Disable
    write_reg(HPET_COUNTER_REG, 0);	 // Reset counter
    write_reg(HPET_CONFIG_REG, 0x3); // Enable counter

    klog(LOG_INFO, "HPET @ %p, clk period %u fs (%llu Hz), %u comparators\n",
        (void*)(uintptr_t)info->address, hpet_clk_period, hpet_frq, info->comparator_count);
}

void hpet_enable_int(u32 time)
{
    if (time < 1 || time > 100)
        kerror("Invalid timer interval\n");
    u32 desired_freq = 1000 / time; // in Hz
    time = hpet_frq / desired_freq;

    // Int enable, periodic, write to accumulator bit
    u64 timer_reg = read_reg(HPET_TIMER_CONF_REG(hpet_number));
    if (!(timer_reg & (1 << 4)))
        kerror("HPET does not allow periodic mode\n");
    u64 val = 0b1001100;

    write_reg(HPET_TIMER_CONF_REG(hpet_number), val);
    write_reg(HPET_TIMER_COMP_REG(hpet_number), time);
    klog(LOG_INFO, "HPET interrupt running at %d Hz (%u fs period)\n", desired_freq, hpet_clk_period);
}

void hpet_disable_int(void)
{
    u64 timer_reg = read_reg(HPET_TIMER_CONF_REG(hpet_number));
    timer_reg &= ~0b1001100; // Disable periodic, int enable, write to accumulator
    write_reg(HPET_TIMER_CONF_REG(hpet_number), timer_reg);
    klog(LOG_INFO, "HPET interrupt disabled\n");
}

u64 hpet_read(void)
{
    return read_reg(HPET_COUNTER_REG);
}

u32 hpet_get_clk_period(void)
{
    return hpet_clk_period;
}
