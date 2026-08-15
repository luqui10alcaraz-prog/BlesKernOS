#include "../include/types.h"

/*
 * Compatibility bridge for trees whose historical stage-6 export table is
 * absent. Returning zero delegates lookup to the stage-5, stage-9 and native
 * DLL resolvers already chained by win32.c.
 */
uint32_t win32_wine_stage6_resolve(const char *dll UNUSED,
                                   const char *name UNUSED)
{
    return 0U;
}

uint32_t win32_wine_stage6_resolve_ordinal(const char *dll UNUSED,
                                           uint16_t ordinal UNUSED)
{
    return 0U;
}

bool win32_wine_stage6_is_data_export(const char *dll UNUSED,
                                      const char *name UNUSED)
{
    return false;
}
