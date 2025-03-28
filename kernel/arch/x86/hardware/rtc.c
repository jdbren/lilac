#include <lilac/log.h>
#include <lilac/timer.h>

#include "io.h"

#define CURRENT_YEAR  2024
static int century_register = 0x00;

static unsigned char second;
static unsigned char minute;
static unsigned char hour;
static unsigned char day;
static unsigned char month;
static unsigned int year;

enum {
    cmos_address = 0x70,
    cmos_data    = 0x71
};

static int get_update_in_progress_flag() {
    outb(cmos_address, 0x0A);
    return (inb(cmos_data) & 0x80);
}

static unsigned char get_RTC_register(int reg) {
    outb(cmos_address, reg);
    return inb(cmos_data);
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

static s64 calculate_unix_time(int seconds, int minutes, int hours, int day, int month, int year) {
    long total_seconds = 0;

    // Calculate seconds for the complete years since 1970
    for (int y = 1970; y < year; y++) {
        total_seconds += is_leap_year(y) ? 31622400 : 31536000; // 365 * 24 * 60 * 60 or 366 * 24 * 60 * 60
    }

    // Calculate seconds for the complete months of the current year
    for (int m = 1; m < month; m++) {
        total_seconds += days_in_month(m, year) * 24 * 60 * 60;
    }

    // Add seconds for the days in the current month
    total_seconds += (day - 1) * 24 * 60 * 60;

    // Add seconds for the current time of day
    total_seconds += hours * 3600 + minutes * 60 + seconds;

    return total_seconds;
}

static struct timestamp read_rtc(void) {
    unsigned char century = 0;
    unsigned char last_second;
    unsigned char last_minute;
    unsigned char last_hour;
    unsigned char last_day;
    unsigned char last_month;
    unsigned char last_year;
    unsigned char last_century;
    unsigned char registerB;

    // Note: This uses the "read registers until you get the same values twice in a row" technique
    //       to avoid getting dodgy/inconsistent values due to RTC updates

    while (get_update_in_progress_flag());                // Make sure an update isn't in progress
    second = get_RTC_register(0x00);
    minute = get_RTC_register(0x02);
    hour = get_RTC_register(0x04);
    day = get_RTC_register(0x07);
    month = get_RTC_register(0x08);
    year = get_RTC_register(0x09);
    if (century_register != 0) {
        century = get_RTC_register(century_register);
    }

    do {
        last_second = second;
        last_minute = minute;
        last_hour = hour;
        last_day = day;
        last_month = month;
        last_year = year;
        last_century = century;

        while (get_update_in_progress_flag());           // Make sure an update isn't in progress
        second = get_RTC_register(0x00);
        minute = get_RTC_register(0x02);
        hour = get_RTC_register(0x04);
        day = get_RTC_register(0x07);
        month = get_RTC_register(0x08);
        year = get_RTC_register(0x09);
        if (century_register != 0) {
                century = get_RTC_register(century_register);
        }
    } while( (last_second != second) || (last_minute != minute) || (last_hour != hour) ||
            (last_day != day) || (last_month != month) || (last_year != year) ||
            (last_century != century) );

    registerB = get_RTC_register(0x0B);

    // Convert BCD to binary values if necessary

    if (!(registerB & 0x04)) {
        second = (second & 0x0F) + ((second / 16) * 10);
        minute = (minute & 0x0F) + ((minute / 16) * 10);
        hour = ( (hour & 0x0F) + (((hour & 0x70) / 16) * 10) ) | (hour & 0x80);
        day = (day & 0x0F) + ((day / 16) * 10);
        month = (month & 0x0F) + ((month / 16) * 10);
        year = (year & 0x0F) + ((year / 16) * 10);
        if(century_register != 0) {
                century = (century & 0x0F) + ((century / 16) * 10);
        }
    }

    // Convert 12 hour clock to 24 hour clock if necessary

    if (!(registerB & 0x02) && (hour & 0x80)) {
        hour = ((hour & 0x7F) + 12) % 24;
    }

    // Calculate the full (4-digit) year
    if(century_register != 0) {
        year += century * 100;
    } else {
        year += (CURRENT_YEAR / 100) * 100;
        if(year < CURRENT_YEAR) year += 100;
    }

    struct timestamp ts = { year, month, day, hour, minute, second };
    return ts;
}

long long rtc_init(void) {
    struct timestamp ts = read_rtc();
    klog(LOG_INFO, "RTC: %04u/%02u/%02u %02u:%02u:%02u\n",
        ts.year, ts.month, ts.day, ts.hour, ts.minute, ts.second);
    return calculate_unix_time(ts.second, ts.minute, ts.hour, ts.day, ts.month, ts.year);
}
