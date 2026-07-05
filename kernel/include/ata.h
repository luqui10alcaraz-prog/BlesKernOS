#ifndef ATA_H
#define ATA_H

#include "types.h"

void ata_init(void);
bool ata_refresh_media(const char *name);

#endif
