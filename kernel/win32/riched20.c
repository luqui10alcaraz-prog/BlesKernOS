#include "win32.h"
#include "../include/types.h"

/*
 * RICHED20 is represented as a built-in compatibility module.  Rich edit
 * controls themselves are created by USER32 using the RichEdit20A/W class
 * aliases and the Stage 11B text engine.  Metapad does not statically import
 * functions from the DLL, but it requires LoadLibrary("RICHED20.DLL") to
 * succeed before creating its client control.
 */
uint32_t win32_riched20_resolve(const char *name UNUSED) {
    return 0U;
}
