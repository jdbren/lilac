// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include "timer.h"

#include <lilac/timer.h>
#include <acpi/hpet.h>
#include <mm/kmm.h>
#include <lilac/port.h>
#include <lilac/panic.h>
#include <lilac/log.h>
#include "apic.h"
#include "idt.h"

#define HPET_ID_REG 0x0
#define HPET_CONFIG_REG 0x10
#define HPET_INTR_REG 0x20
#define HPET_COUNTER_REG 0xf0
#define HPET_TIMER_CONF_REG(N) (0x100 + 0x20 * N)
#define HPET_TIMER_COMP_REG(N) (0x108 + 0x20 * N)

struct hpet_id_reg {
    u8 rev_id;
    u8 flags;
    u16 vendor_id;
    u32 counter_clk_period;
} __packed;

extern void timer_handler(void);
extern void init_PIT(int freq);

static s64 boot_unix_time;
volatile u32 timer_cnt;
extern volatile u32 system_timer_fractions;
extern volatile u32 system_timer_ms;

volatile u64 *hpet_base;
u32 hpet_clk_period;
u64 hpet_frq;

static u64 read_reg(const u32 offset)
{
    return *(volatile u64*)((u32)hpet_base + offset);
}

static u32 read_reg32(const u32 offset)
{
    return *(volatile u32*)((u32)hpet_base + offset);
}

static void write_reg(const u32 offset, const u64 val)
{
    *(volatile u64*)((u32)hpet_base + offset) = val;
}

// time in ms
void hpet_init(u32 time, struct hpet_info *info)
{
    if (time < 1 || time > 100)
        kerror("Invalid timer interval\n");

    u32 desired_freq = 1000 / time; // in Hz

    hpet_base = (volatile u64*)(uintptr_t)info->address;
    map_to_self((void*)hpet_base, 0x1000, PG_WRITE | PG_CACHE_DISABLE);

    struct hpet_id_reg *id = (struct hpet_id_reg*)hpet_base;
    hpet_clk_period = id->counter_clk_period;
    hpet_frq = 1000000000000000ULL / hpet_clk_period; // in Hz

    time = hpet_frq / desired_freq;

    // Int enable, periodic, write to accumulator bit, force 32-bit mode
    u64 timer_reg = read_reg(HPET_TIMER_CONF_REG(info->hpet_number));
    if (!(timer_reg & (1 << 4)))
        kerror("HPET does not allow periodic mode\n");
    u64 val = (1 << 2) | (1 << 3) | (1 << 6) | (1 << 8);

    write_reg(HPET_CONFIG_REG, 0x2); // Enable legacy replacement
    write_reg(HPET_COUNTER_REG, 0);	 // Reset counter
    write_reg(HPET_TIMER_CONF_REG(info->hpet_number), val);
    write_reg(HPET_TIMER_COMP_REG(info->hpet_number), time);
    write_reg(HPET_CONFIG_REG, 0x3); // Enable counter

    klog(LOG_INFO, "HPET set at %d Hz\n", desired_freq);
}

u32 hpet_read(void)
{
    return read_reg32(HPET_COUNTER_REG);
}

// TODO: Make sure HPET is supported
void timer_init(u32 ms, struct hpet_info *info)
{
    idt_entry(0x20, (u32)timer_handler, 0x08, INT_GATE);
    ioapic_entry(0, 0x20, 0, 0);
    hpet_init(ms, info);
    boot_unix_time = rtc_init();
    kstatus(STATUS_OK, "HPET initialized\n");
}

void sleep(u32 millis)
{
    timer_cnt = millis;
    while (timer_cnt > 0) {
        asm volatile("nop");
    }
}

void usleep(u32 micros)
{
    // Get current time
    u64 start = get_sys_time();
    u64 end = start + micros * 1000;
    while (get_sys_time() < end) {
        asm volatile("nop");
    }
}

// Get system timer in 1 ns intervals
u64 get_sys_time(void)
{
    u32 counter = hpet_read();
    u32 frq = hpet_frq / 100000000U;
    u64 time = counter / frq * 10;
    return time;
}

s64 get_unix_time(void)
{
    return boot_unix_time + get_sys_time() / 1000000000ull;
}

static int is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int days_in_month(int month, int year) {
    if (month == 2) {
        return is_leap_year(year) ? 29 : 28;
    }
    int days_in_months[] = { 31, 30, 31, 30, 31, 31, 31, 30, 31, 30, 31, 31 };
    return days_in_months[month - 1]; // month is 1-12
}

static void unix_time_to_date(long long unix_time, struct timestamp *ts) {
    // Start from the epoch time
    s64 remaining_seconds = unix_time;

    // Calculate the year
    ts->year = 1970;
    while (remaining_seconds >= (is_leap_year(ts->year) ? 31622400 : 31536000)) {
        remaining_seconds -= (is_leap_year(ts->year) ? 31622400 : 31536000);
        (ts->year)++;
    }

    // Calculate the month
    ts->month = 1;
    while (remaining_seconds >= days_in_month(ts->month, ts->year) * 24 * 60 * 60) {
        remaining_seconds -= days_in_month(ts->month, ts->year) * 24 * 60 * 60;
        (ts->month)++;
    }

    // Calculate the day
    ts->day = 1;
    while (remaining_seconds >= 24 * 60 * 60) {
        remaining_seconds -= 24 * 60 * 60;
        (ts->day)++;
    }

    // Calculate hours, minutes, and seconds
    ts->hour = remaining_seconds / 3600;
    remaining_seconds %= 3600;
    ts->minute = remaining_seconds / 60;
    ts->second = remaining_seconds % 60;
}


struct timestamp get_timestamp(void)
{
    struct timestamp ts;
    long long unix_time = get_unix_time();
    unix_time_to_date(unix_time, &ts);
    return ts;
}

// unsigned read_pit_count(void) {
// 	unsigned count = 0;

// 	// Disable interrupts
// 	asm("cli");

// 	// al = channel in bits 6 and 7, remaining bits clear
// 	outb(0x43, 0);

// 	count = inb(0x40);			// Low byte
// 	count |= inb(0x40)<<8;		// High byte

// 	return count;
// }

// void set_pit_count(unsigned count) {
// 	// Disable interrupts
// 	asm("cli");

// 	// Set low byte
// 	outb(0x40,count&0xFF);			// Low byte
// 	outb(0x40,(count&0xFF00)>>8);	// High byte
// }
