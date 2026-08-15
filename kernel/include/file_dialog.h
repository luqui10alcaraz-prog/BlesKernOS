#ifndef BK_FILE_DIALOG_H
#define BK_FILE_DIALOG_H

#include "types.h"
#include "vfs.h"
#include "../../gui/gui.h"

#define BK_FILE_DIALOG_PREVIEW_AUDIO 0x00000001U

/* path es NULL cuando el usuario cancela o cierra el selector. */
typedef void (*bk_file_dialog_callback_t)(const char *path, void *context);

/*
 * Abre un selector asincrono. extension puede ser ".WAV" o NULL para
 * mostrar todos los archivos. La ruta entregada al callback es absoluta.
 */
bool bk_file_dialog_open(gui_desktop_t *desktop, const char *title,
                         const char *initial_path, const char *extension,
                         uint32_t flags, bk_file_dialog_callback_t callback,
                         void *context);

/* Internal variant for trusted kernel subsystems such as COMDLG32.  Unlike
 * the public API, its callback runs in Ring 0 and may reference a suspended
 * kernel frame. It is intentionally not exported through the SDK. */
bool bk_file_dialog_open_kernel(gui_desktop_t *desktop, const char *title,
                                const char *initial_path,
                                const char *extension, uint32_t flags,
                                bk_file_dialog_callback_t callback,
                                void *context);

#endif
