#ifndef MESA35_BLESKERNOS_CONF_H
#define MESA35_BLESKERNOS_CONF_H

/* Freestanding Mesa 3.5 configuration for BlesKernOS.
 * Keep all host window-system, threading and assembly backends disabled.
 * The port uses the portable software rasterizer through OSMesa. */
#define PACKAGE "Mesa"
#define VERSION "3.5-bleskernos"
#define STDC_HEADERS 1
#define HAVE_MEMCPY 1
#define HAVE_MEMMOVE 1
#define HAVE_STRCHR 1
#define HAVE_MATH_H 1
#define X_DISPLAY_MISSING 1

#endif
