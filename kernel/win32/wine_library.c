/* Unified BlesKernOS Win32/Wine system library initializer. */
#include "win32.h"

bool win32_wine_stage7_init(void);
bool win32_wine_stage8_init(void);
bool win32_wine_stage9_init(void);

static bool wine_library_initialized;
static bool wine_library_initializing;

bool win32_wine_library_init(void) {
    bool ok7, ok8, ok9;
    if (wine_library_initialized) return true;
    if (wine_library_initializing) return true;
    wine_library_initializing = true;
    ok7 = win32_wine_stage7_init();
    ok8 = win32_wine_stage8_init();
    ok9 = win32_wine_stage9_init();
    wine_library_initialized = ok7 && ok8 && ok9;
    wine_library_initializing = false;
    return wine_library_initialized;
}
