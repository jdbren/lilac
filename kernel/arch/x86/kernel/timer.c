// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include "timer.h"

#include <lilac/config.h>
#include <lilac/boot.h>
#include <lilac/timer.h>
#include <lilac/sched.h>
#include <acpi/hpet.h>
#include <lilac/kmm.h>
#include <lilac/port.h>
#include <lilac/panic.h>
#include <lilac/log.h>

#include <asm/segments.h>
#include "cpu-features.h"
#include "apic.h"
#include "idt.h"


extern void timer_handler(void);
extern void init_PIT(int freq);

static s64 boot_unix_time;
volatile u32 timer_cnt;
extern volatile u32 system_timer_fractions;
extern volatile u32 system_timer_ms;

static int hpet_enabled = 0;

u64 tsc_freq_hz = 0;

static inline void nop(void) {}

static void (*handle_tick)(void) = nop;


u64 rdtsc(void)
{
    unsigned long eax, edx;
    __asm__ ("rdtsc" : "=a"(eax), "=d"(edx));
    return ((u64)edx << 32) | eax;
}

static inline bool in_hypervisor(void)
{
    u32 a, b, c, d;
    cpuid(0x1, &a, &b, &c, &d);
    return c & (1 << 31);
}

static u64 cpuid_get_tsc_hz(void)
{
    u32 a, b, c, d;
    cpuid(0x15, &a, &b, &c, &d);

    if (a == 0) {
        if (in_hypervisor()) {
            // Try hypervisor-specific CPUID leaf
            cpuid(0x40000010, &a, &b, &c, &d);
            if (a != 0)
                return a * 1000ull;
        }
        return 0;
    }
    return a ? ((c ? c : (unsigned long)24e6) * (b / a)) : 0;
}

static inline bool invariant_tsc(void)
{
    u32 a, b, c, d;
    cpuid(0x80000007, &a, &b, &c, &d);
    return d & (1<<8); // Check if invariant TSC bit is set
}

static bool tsc_deadline(void)
{
    u32 a, b, c, d;
    cpuid(0x1, &a, &b, &c, &d);
    return c & bit_TSC_DL; // Check if TSC deadline timer is supported
}

static u64 calc_tsc_hz(void)
{
    unsigned long tsc_hz = cpuid_get_tsc_hz();
    if (tsc_hz > 0) {
        klog(LOG_DEBUG, "TSC frequency from CPUID: %lu Hz\n", tsc_hz);
        return tsc_hz;
    }

    u64 init_read = rdtsc();
    u64 ticks_in_10ms = 0;
    usleep(10000);
    ticks_in_10ms = rdtsc() - init_read;
    return ticks_in_10ms * 100; // Convert to Hz
}

void tsc_deadline_tick(void)
{
    // should calculate the next timer event to fire
    // for now, just set it to 1ms from now
    tsc_deadline_set(1000000);
}

void timer_tick_init(void)
{
    if (tsc_deadline()) {
        apic_tsc_deadline();
        handle_tick = tsc_deadline_tick;
    } else if (invariant_tsc()) {
        // tsc periodic
    } else {
        apic_periodic(TIMER_HZ / 1000);
    }
}

void timer_init(void)
{
    idt_entry(0x20, (uintptr_t)timer_handler, __KERNEL_CS, 0, INT_GATE);
    ioapic_entry(0, 0x20, 0, 0);

    struct hpet_info *info = boot_info.acpi.hpet;

    hpet_init(info);
    hpet_enabled = 1;

    tsc_freq_hz = calc_tsc_hz();
    boot_unix_time = rtc_init() - rdtsc() / tsc_freq_hz;
    klog(LOG_INFO, "TSC frequency: %llu Hz\n", tsc_freq_hz);
    klog(LOG_INFO, "Boot Unix time: %lld seconds since epoch\n", boot_unix_time);

    timer_tick_init();

    kstatus(STATUS_OK, "System clock initialized\n");
}


void timer_tick(void)
{
    handle_tick();
    sched_tick();
}

__attribute__((optimize("O0")))
void sleep(u32 millis)
{
    timer_cnt = millis;
    while (timer_cnt > 0) {
        asm volatile("nop");
    }
}

__attribute__((optimize("O0")))
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
    return boot_unix_time + get_sys_time() / 1'000'000'000ull;
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
