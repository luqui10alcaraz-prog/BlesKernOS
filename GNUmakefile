# ATIR128 enhanced integration for BlesKernOS 0.8.
#
# Keep the main Makefile untouched: several driver patches extend DRIVER_OBJS,
# so editing that long line is fragile. GNU make loads GNUmakefile first; this
# wrapper includes the normal build and then adds ATIR128.DVR as an additional
# prerequisite of the ATA image. The FAT32 packer automatically copies every
# built *.DVR from build/system/drivers into /SYSTEM/DRIVERS.

include Makefile

ATIR128_DVR ?= build/system/drivers/ATIR128.DVR
ATIR1283D_DVR ?= build/system/drivers/ATIR1283D.DVR

$(ATA_IMG): $(ATIR128_DVR) $(ATIR1283D_DVR)
