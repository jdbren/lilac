// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include "timer.h"

#include <lilac/timer.h>
#include <acpi/hpet.h>
#include <mm/kmm.h>
#include <lilac/port.h>
#include <lilac/panic.h>
#include <lilac/log.h>

#include <asm/segments.h>
#include <asm/cpuid-bits.h>

#include "apic.h"
#include "idt.h"


extern void timer_handler(void);
extern void init_PIT(int freq);

static s64 boot_unix_time;
volatile u32 timer_cnt;
extern volatile u32 system_timer_fractions;
extern volatile u32 system_timer_ms;

static int hpet_enabled = 0;

static u64 tsc_freq_hz = 0;

u64 rdtsc(void)
{
    unsigned long eax, edx;
    __asm__ ("rdtsc" : "=a"(eax), "=d"(edx));
    return ((u64)edx << 32) | eax;
}

static inline u64 cpuid_get_tsc_hz(void)
{
    u32 a, b, c, d;
    __cpuid(0x15, a, b, c, d);
    return a ? ((c ? c : (unsigned long)24e6) * (b / a)) : 0;
}

static bool invariant_tsc(void)
{
    u32 a, b, c, d;
    __cpuid(0x8000'0007, a, b, c, d);
    klog(LOG_DEBUG, "CPUID 0x8000'0007: a=%x, b=%x, c=%x, d=%x\n", a, b, c, d);
    return d & (1<<8); // Check if invariant TSC bit is set
}

static u64 calc_tsc_hz(void)
{
    unsigned long tsc_hz = cpuid_get_tsc_hz();
    if (tsc_hz > 0)
        return tsc_hz;

    asm ("sti");
    u64 init_read = rdtsc();
    u64 ticks_in_10ms = 0;
    usleep(10000);
    ticks_in_10ms = rdtsc() - init_read;
    asm ("cli");
    return ticks_in_10ms * 100; // Convert to Hz
}

// TODO: Make sure HPET is supported
void timer_init(u32 ms, struct hpet_info *info)
{
    idt_entry(0x20, (uintptr_t)timer_handler, __KERNEL_CS, 0, INT_GATE);
    ioapic_entry(0, 0x20, 0, 0);
    hpet_init(ms, info);
    hpet_enabled = 1;
    tsc_freq_hz = calc_tsc_hz();
    boot_unix_time = rtc_init() - rdtsc() / tsc_freq_hz;
    klog(LOG_INFO, "TSC frequency: %llu Hz\n", tsc_freq_hz);
    klog(LOG_INFO, "Boot Unix time: %lld seconds since epoch\n", boot_unix_time);
    // invariant_tsc();
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
    if (!hpet_enabled)
        return 0;
    u64 counter = hpet_read();
    u64 time_ns = (counter * hpet_get_clk_period()) / 1000000ULL;
    return time_ns;
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
