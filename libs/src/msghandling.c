/*
 * TinyGL message handling - BlesKernOS port
 *
 * The first port keeps TinyGL logging silent to avoid pulling extra stdio
 * formatting code into the renderer. Turn these into kprintf/vsnprintf later
 * if you want verbose TinyGL diagnostics.
 */

#include "msghandling.h"

void tgl_warning(const char *text, ...)
{
    (void)text;
}

void tgl_trace(const char *text, ...)
{
    (void)text;
}

void tgl_fixme(const char *text, ...)
{
    (void)text;
}
