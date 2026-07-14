#include "../include/rtc.h"

static const rtc_driver_ops_t *g_rtc_driver;

bool rtc_register_driver(const rtc_driver_ops_t *ops) {
    if (!ops || !ops->get_time || !ops->get_date || !ops->get_datetime)
        return false;
    g_rtc_driver = ops;
    return true;
}

bool rtc_get_time(rtc_time_t *time) {
    return g_rtc_driver && g_rtc_driver->get_time(time);
}
bool rtc_get_date(rtc_date_t *date) {
    return g_rtc_driver && g_rtc_driver->get_date(date);
}
bool rtc_get_datetime(rtc_datetime_t *datetime) {
    return g_rtc_driver && g_rtc_driver->get_datetime(datetime);
}
