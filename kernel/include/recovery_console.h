#ifndef BLESKERNOS_RECOVERY_CONSOLE_H
#define BLESKERNOS_RECOVERY_CONSOLE_H

/*
 * Abandona el modo grafico y restaura VGA texto 80x25.
 * Esta ruta no usa BIOS y puede llamarse desde modo protegido.
 */
void recovery_console_enter(void);

#endif
