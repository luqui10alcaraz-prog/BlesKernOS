#ifndef PE_LOADER_H
#define PE_LOADER_H

#include "types.h"

/*
 * Primer cargador PE32 de BlesKernOS.
 *
 * Alcance inicial:
 *   - ejecutables PE32/i386
 *   - secciones cargadas en una imagen contigua del heap
 *   - relocalaciones IMAGE_REL_BASED_HIGHLOW
 *   - imports por nombre desde KERNEL32.DLL/KERNELBASE.DLL
 *
 * Incluye DLLs PE, TLS inicial y un entorno PEB/TEB x86 por proceso.
 * Incluye SEH x86 inicial; todavia no implementa unwind completo ni aislamiento por paginacion.
 */
bool pe_execute_program(const char *path);
bool pe_execute_program_command_line(const char *path,
                                     const char *command_line);
bool pe_dump_info(const char *path);
const char *pe_last_error(void);

/* Puente interno usado por los modulos Win32 incorporados. */
uint32_t pe_win32_resolve_export(const char *dll, const char *name);
uint32_t pe_win32_current_image_base(void);
/* Consulta la imagen PE (EXE o DLL) que contiene una direccion cargada. */
bool pe_win32_query_image_region(const void *address, const uint8_t **base_out,
                                 uint32_t *size_out);
uint32_t pe_win32_get_last_error(void);
void pe_win32_set_last_error(uint32_t error);
void *pe_win32_load_library(const char *name);
void *pe_win32_get_module_handle(const char *name);
void *pe_win32_get_proc_address(void *module, const char *name);
void *pe_win32_get_proc_ordinal(void *module, uint16_t ordinal);
bool pe_win32_free_library(void *module);
void pe_win32_terminate_current_process(void);

/* Vista segura de la imagen y de su directorio de recursos PE. */
bool pe_win32_get_image_resource(void *module, const uint8_t **image_out,
                                 uint32_t *image_size_out,
                                 uint32_t *resource_rva_out,
                                 uint32_t *resource_size_out);

/* Notificaciones usadas por CreateThread para TLS callbacks y DllMain. */
void pe_win32_thread_attach(void);
void pe_win32_thread_detach(void);

#endif
