#include "common.h"

static int run(int argc UNUSED, char **argv UNUSED) { rtc_datetime_t dt; if(!bk_time_datetime(&dt)) return command_error("date","reloj no disponible"); kprintf("%u-%u-%u\n",dt.date.year,dt.date.month,dt.date.day); return 0; }

BK_COMMAND_MAIN(run)
