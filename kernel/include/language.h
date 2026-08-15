#ifndef BK_LANGUAGE_H
#define BK_LANGUAGE_H

#include "types.h"

#define BK_LANGUAGE_CODE_MAX 8U
#define BK_LANGUAGE_NAME_MAX 32U

#ifndef BLESKERNOS_APPLICATION_API_H
typedef struct {
    char code[BK_LANGUAGE_CODE_MAX];
    char name[BK_LANGUAGE_NAME_MAX];
} bk_language_info_t;
#endif

/* Inicializa el catalogo despues de montar el volumen del sistema. */
void language_init(void);

/* Claves explicitas, por ejemplo COMMON.OK. Devuelve key si falta. */
const char *language_get(const char *key);

/* Compatibilidad: traduce una cadena fuente por su hash, o una clave @KEY. */
const char *language_translate(const char *source);

/* Traduce también claves @H12345678 incrustadas en texto ya formateado. */
const char *language_expand(const char *source, char *output,
                            uint32_t output_size);

const char *language_current(void);
uint32_t language_generation(void);
uint32_t language_count(void);
bool language_info(uint32_t index, bk_language_info_t *info);
bool language_set(const char *code);

#endif
