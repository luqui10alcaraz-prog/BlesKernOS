#ifndef RTC_H
#define RTC_H

#include "types.h"

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} rtc_time_t;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
} rtc_date_t;

typedef struct {
    rtc_date_t date;
    rtc_time_t time;
} rtc_datetime_t;

typedef struct {
    bool (*get_time)(rtc_time_t *time);
    bool (*get_date)(rtc_date_t *date);
    bool (*get_datetime)(rtc_datetime_t *datetime);
} rtc_driver_ops_t;

bool rtc_register_driver(const rtc_driver_ops_t *ops);

bool rtc_get_time(rtc_time_t *time);
bool rtc_get_date(rtc_date_t *date);
bool rtc_get_datetime(rtc_datetime_t *datetime);

#endif
