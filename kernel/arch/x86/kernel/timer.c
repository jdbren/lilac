// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include "timer.h"

#include <lilac/types.h>
#include <lilac/config.h>
#include <lilac/boot.h>
#include <lilac/timer.h>
#include <lilac/sched.h>
#include <lilac/port.h>
#include <lilac/panic.h>
#include <lilac/log.h>
#include <acpi/hpet.h>
#include <asm/segments.h>

#include "cpu-features.h"
#include "apic.h"
#include "idt.h"
#include "io.h"

#define PIT_FREQUENCY 1193182

extern void timer_handler(void);
extern void init_PIT(int freq);
u64 read_pit_count(void);

static int hpet_enabled = 0;

u64 rdtsc(void)
{
    unsigned long eax, edx;
    __asm__ ("rdtsc" : "=a"(eax), "=d"(edx));
    return ((u64)edx << 32) | eax;
}

static struct clock_source tsc_clock = {
    .name = "tsc",
    .read = rdtsc,
    .freq_hz = 0,
    .scale = { .mult = 0, .shift = 0 },
};

static struct clock_source hpet_clock = {
    .name = "hpet",
    .read = hpet_read,
    .freq_hz = 0,
    .scale = { .mult = 0, .shift = 0 },
};

static struct clock_source pit_clock = {
    .name = "pit",
    .read = read_pit_count,
    .freq_hz = PIT_FREQUENCY,
    .scale = { .mult = 0, .shift = 0 },
};

struct clock_source *__system_clock = &pit_clock;
u64 ticks_per_ms = PIT_FREQUENCY / 1000;

// Compute mult/shift so that:
//    mult fits 32-bit, maximize precision
// Formula: mult ≈ (1e9 << shift) / freq
static void set_clock_scale(struct clock_source *c)
{
    u32 shift;
    for (shift = 31; ; shift--) {
        u64 num = (1000000000ull << shift);
        u64 mult = num / c->freq_hz;

        if (mult <= UINT32_MAX) {
            c->scale.mult = (u32)mult;
            c->scale.shift = shift;
            klog(LOG_INFO, "Clock %s: scale mult=%u shift=%u\n",
                 c->name, c->scale.mult, c->scale.shift);
            return;
        }

        if (shift == 0)
            break;
    }

    // fallback: lowest precision but safe
    c->scale.mult = 1000000000ull / c->freq_hz;
    c->scale.shift = 0;
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

void tsc_deadline_tick(unsigned long x)
{
    // should calculate the next timer event to fire
    // for now, just set it to 1ms from now
    u64 tsc_now = rdtsc();
    tsc_deadline_set(tsc_now + ticks_per_ms);
}

void timer_tick_init(void)
{
    idt_entry(0x20, (uintptr_t)timer_handler, __KERNEL_CS, 0, INT_GATE);
    ioapic_entry(0, 0x20, 0, 0);

    if (tsc_deadline()) {
        apic_tsc_deadline();
        handle_tick = tsc_deadline_tick;
        set_clock_source(&tsc_clock);
    } else if (invariant_tsc()) {
        // tsc periodic
        apic_periodic(TIMER_HZ / 1000);
        set_clock_source(&tsc_clock);
    } else {
        apic_periodic(TIMER_HZ / 1000);
    }
}

void x86_timer_init(void)
{
    struct hpet_info *info = boot_info.acpi.hpet;

    hpet_clock.freq_hz = hpet_init(info);
    set_clock_scale(&hpet_clock);
    hpet_enabled = 1;
    set_clock_source(&hpet_clock);

    tsc_clock.freq_hz = calc_tsc_hz();
    set_clock_scale(&tsc_clock);
    boot_unix_time = rtc_init() - rdtsc() / tsc_clock.freq_hz;

    klog(LOG_INFO, "TSC frequency: %llu Hz\n", tsc_clock.freq_hz);
    klog(LOG_INFO, "Boot Unix time: %lld seconds since epoch\n", boot_unix_time);
}

u64 read_pit_count(void)
{
	unsigned count = 0;

	// al = channel in bits 6 and 7, remaining bits clear
	outb(0x43, 0);

	count = inb(0x40);			// Low byte
	count |= inb(0x40) << 8;		// High byte

	return count;
}

void set_pit_count(unsigned count)
{
	// Set low byte
	outb(0x40, count & 0xFF);			// Low byte
	outb(0x40, (count & 0xFF00) >> 8);	// High byte
}
