#include "../include/rtc.h"
#include "../include/pic.h"

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    io_wait();
    return inb(0x71);
}

static bool rtc_wait_ready(void) {
    for (uint32_t i = 0; i < 100000; i++)
        if (!(cmos_read(0x0A) & 0x80)) return true;
    return false;
}

static uint8_t from_bcd(uint8_t value) {
    return (uint8_t)((value & 0x0F) + ((value >> 4) * 10));
}

static uint16_t rtc_expand_year(uint8_t year, uint8_t century) {
    if (century != 0 && century != 0xFF)
        return (uint16_t)((uint16_t)century * 100U + year);

    /*
     * Fallback for machines that do not expose CMOS century register 0x32.
     * Good enough for modern dates and retro OS testing.
     */
    return (uint16_t)((year < 70) ? (2000U + year) : (1900U + year));
}

bool rtc_get_time(rtc_time_t *time) {
    uint8_t second, minute, hour;
    uint8_t second_check, minute_check, hour_check;
    uint8_t status_b;
    bool pm;

    if (!time || !rtc_wait_ready()) return false;

    do {
        second = cmos_read(0x00);
        minute = cmos_read(0x02);
        hour = cmos_read(0x04);
        if (!rtc_wait_ready()) return false;
        second_check = cmos_read(0x00);
        minute_check = cmos_read(0x02);
        hour_check = cmos_read(0x04);
    } while (second != second_check || minute != minute_check ||
             hour != hour_check);

    status_b = cmos_read(0x0B);
    pm = (hour & 0x80) != 0;
    hour &= 0x7F;

    if (!(status_b & 0x04)) {
        second = from_bcd(second);
        minute = from_bcd(minute);
        hour = from_bcd(hour);
    }

    if (!(status_b & 0x02)) {
        if (pm && hour < 12) hour = (uint8_t)(hour + 12);
        if (!pm && hour == 12) hour = 0;
    }

    time->second = second;
    time->minute = minute;
    time->hour = hour;
    return true;
}

bool rtc_get_date(rtc_date_t *date) {
    uint8_t day, month, year, century;
    uint8_t day_check, month_check, year_check, century_check;
    uint8_t status_b;
    uint16_t full_year;

    if (!date || !rtc_wait_ready()) return false;

    do {
        day = cmos_read(0x07);
        month = cmos_read(0x08);
        year = cmos_read(0x09);
        century = cmos_read(0x32);

        if (!rtc_wait_ready()) return false;

        day_check = cmos_read(0x07);
        month_check = cmos_read(0x08);
        year_check = cmos_read(0x09);
        century_check = cmos_read(0x32);
    } while (day != day_check || month != month_check ||
             year != year_check || century != century_check);

    status_b = cmos_read(0x0B);
    if (!(status_b & 0x04)) {
        day = from_bcd(day);
        month = from_bcd(month);
        year = from_bcd(year);
        if (century != 0 && century != 0xFF)
            century = from_bcd(century);
    }

    full_year = rtc_expand_year(year, century);

    if (full_year < 1 || month < 1 || month > 12 || day < 1 || day > 31)
        return false;

    date->year = full_year;
    date->month = month;
    date->day = day;
    return true;
}

bool rtc_get_datetime(rtc_datetime_t *datetime) {
    if (!datetime) return false;
    if (!rtc_get_date(&datetime->date)) return false;
    if (!rtc_get_time(&datetime->time)) return false;
    return true;
}
