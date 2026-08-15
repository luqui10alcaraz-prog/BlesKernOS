#ifndef BK_DATETIME_PREFS_H
#define BK_DATETIME_PREFS_H

#include "rtc.h"
#include "user_config.h"

typedef struct {
    const char *label;
    int16_t offset_minutes;
} bk_timezone_option_t;

typedef struct {
    bool format_24h;
    int timezone_index;
} bk_datetime_preferences_t;

bool bk_datetime_runtime_preferences_get(bk_datetime_preferences_t *prefs);
void bk_datetime_runtime_preferences_set(
    const bk_datetime_preferences_t *prefs);

static const bk_timezone_option_t g_bk_timezone_options[] = {
    {"Hora del RTC", 0},
    {"UTC-08 Pacifico", -480},
    {"UTC-06 Ciudad de Mexico", -360},
    {"UTC-05 Nueva York", -300},
    {"UTC-03 Buenos Aires", -180},
    {"UTC+00 Londres", 0},
    {"UTC+01 Madrid", 60},
    {"UTC+03 Moscu", 180},
    {"UTC+09 Tokio", 540},
    {"UTC+10 Sidney", 600},
};

static inline int bk_timezone_count(void) {
    return (int)(sizeof(g_bk_timezone_options) /
                 sizeof(g_bk_timezone_options[0]));
}

static inline const bk_timezone_option_t *bk_timezone_option(int index) {
    if (index < 0 || index >= bk_timezone_count()) index = 0;
    return &g_bk_timezone_options[index];
}

static inline int bk_datetime_parse_int(const char *text, int fallback) {
    int value = 0;
    bool seen_digit = false;
    bool negative = false;

    if (!text) return fallback;
    if (*text == '+') text++;
    else if (*text == '-') {
        negative = true;
        text++;
    }

    while (*text >= '0' && *text <= '9') {
        seen_digit = true;
        value = value * 10 + (*text - '0');
        text++;
    }
    if (!seen_digit) return fallback;
    return negative ? -value : value;
}

static inline void bk_datetime_preferences_defaults(
    bk_datetime_preferences_t *prefs) {
    if (!prefs) return;
    prefs->format_24h = true;
    prefs->timezone_index = 0;
}

static inline bool bk_datetime_is_leap_year(int year) {
    if (year <= 0) return false;
    if ((year % 400) == 0) return true;
    if ((year % 100) == 0) return false;
    return (year % 4) == 0;
}

static inline int bk_datetime_days_in_month(int year, int month) {
    static const int days[12] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    if (month < 1 || month > 12) return 30;
    if (month == 2 && bk_datetime_is_leap_year(year)) return 29;
    return days[month - 1];
}

static inline int bk_datetime_weekday(int year, int month, int day) {
    static const int table[12] = {
        0, 3, 2, 5, 0, 3,
        5, 1, 4, 6, 2, 4
    };

    if (year < 1) year = 1;
    if (month < 1) month = 1;
    if (month > 12) month = 12;
    if (day < 1) day = 1;

    if (month < 3) year--;
    return (year + year / 4 - year / 100 + year / 400 +
            table[month - 1] + day) % 7;
}

static inline void bk_datetime_shift_date(rtc_date_t *date, int delta_days) {
    if (!date) return;

    while (delta_days > 0) {
        int days = bk_datetime_days_in_month(date->year, date->month);
        if (date->day < days) {
            date->day++;
        } else {
            date->day = 1;
            if (date->month < 12) date->month++;
            else {
                date->month = 1;
                date->year++;
            }
        }
        delta_days--;
    }

    while (delta_days < 0) {
        if (date->day > 1) {
            date->day--;
        } else {
            if (date->month > 1) date->month--;
            else {
                date->month = 12;
                if (date->year > 1) date->year--;
            }
            date->day = (uint8_t)bk_datetime_days_in_month(date->year,
                                                           date->month);
        }
        delta_days++;
    }
}

static inline void bk_datetime_apply_offset(const rtc_datetime_t *src,
                                            rtc_datetime_t *dst,
                                            int offset_minutes) {
    int total_minutes;
    int day_delta = 0;

    if (!src || !dst) return;
    *dst = *src;

    total_minutes = (int)src->time.hour * 60 +
                    (int)src->time.minute + offset_minutes;
    while (total_minutes < 0) {
        total_minutes += 24 * 60;
        day_delta--;
    }
    while (total_minutes >= 24 * 60) {
        total_minutes -= 24 * 60;
        day_delta++;
    }

    dst->time.hour = (uint8_t)(total_minutes / 60);
    dst->time.minute = (uint8_t)(total_minutes % 60);
    bk_datetime_shift_date(&dst->date, day_delta);
}

static inline void bk_datetime_apply_preferences(
    const bk_datetime_preferences_t *prefs,
    const rtc_datetime_t *src,
    rtc_datetime_t *dst) {
    const bk_timezone_option_t *zone;

    if (!src || !dst) return;
    zone = bk_timezone_option(prefs ? prefs->timezone_index : 0);
    bk_datetime_apply_offset(src, dst, zone->offset_minutes);
}

static inline void bk_datetime_preferences_load(
    bk_datetime_preferences_t *prefs) {
    void *config = NULL;
    uint32_t size = 0;

    if (!prefs) return;
    bk_datetime_preferences_defaults(prefs);

    if (!bk_user_config_read_all(BK_DATETIME_CONFIG_PATH,
                                 BK_DATETIME_CONFIG_LEGACY_PATH,
                                 &config, &size) || !config) {
        (void)bk_user_config_write_text(BK_DATETIME_CONFIG_PATH,
                                        "format=24\r\ntimezone=0\r\n");
        return;
    }
    (void)size;

    {
        char *line = (char *)config;

        while (*line) {
            char *end = line;
            char *eq = NULL;
            char saved;

            while (*end && *end != '\r' && *end != '\n') {
                if (*end == '=' && !eq) eq = end;
                end++;
            }
            saved = *end;
            *end = '\0';

            if (eq) {
                int value;

                *eq++ = '\0';
                value = bk_datetime_parse_int(eq, 0);
                if (kstrcmp(line, "format") == 0) {
                    prefs->format_24h = value != 12;
                } else if (kstrcmp(line, "timezone") == 0) {
                    if (value >= 0 && value < bk_timezone_count())
                        prefs->timezone_index = value;
                }
            }

            *end = saved;
            line = end;
            while (*line == '\r' || *line == '\n') line++;
        }
    }

    kfree(config);
}

#endif
