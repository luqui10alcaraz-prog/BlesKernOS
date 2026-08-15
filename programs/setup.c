#include "system_tools_common.h"

#define SETUP_MIN_API 20U
#define SETUP_TERMS "/SYSTEM/SETUP/TERMS.TXT"
#define SETUP_CORE "/SYSTEM/SETUP/CORE.BKL"
#define SETUP_PROGRAMS "/SYSTEM/SETUP/PROGRAMS.BKL"
#define SETUP_ASSETS "/SYSTEM/SETUP/ASSETS.BKL"
#define SETUP_WINE "/SYSTEM/SETUP/WINE.BKL"

/* Paleta inspirada en asistentes de instalacion de mediados de los 90. */
#define SETUP_FACE       0x00C0C0C0U
#define SETUP_LIGHT      0x00FFFFFFU
#define SETUP_SHADOW     0x00808080U
#define SETUP_DARK       0x00404040U
#define SETUP_NAVY       0x00000080U
#define SETUP_NAVY_2     0x00001858U
#define SETUP_TEAL       0x00008080U
#define SETUP_WHITE      0x00FFFFFFU
#define SETUP_TEXT       0x00101010U
#define SETUP_MUTED      0x00585858U
#define SETUP_SELECT     0x00000080U
#define SETUP_GREEN      0x00007020U
#define SETUP_RED        0x00900000U
#define SETUP_GOLD       0x00E0B84CU

#define SETUP_LANGUAGE_COUNT 3
#define SETUP_ZONE_COUNT 9

typedef enum {
    SETUP_PAGE_WELCOME = 0,
    SETUP_PAGE_LICENSE,
    SETUP_PAGE_IDENTITY,
    SETUP_PAGE_DATETIME,
    SETUP_PAGE_COMPONENTS,
    SETUP_PAGE_SUMMARY,
    SETUP_PAGE_INSTALLING,
    SETUP_PAGE_DONE,
    SETUP_PAGE_ERROR,
    SETUP_PAGE_COUNT
} setup_page_t;

typedef struct {
    const char *code;
    const char *name;
} setup_language_t;

typedef struct {
    int32_t minutes;
    int config_index;
    const char *label;
    const char *place;
} setup_zone_t;

static const setup_language_t g_languages[SETUP_LANGUAGE_COUNT] = {
    {"ES", "Espanol"}, {"EN", "English"}, {"IT", "Italiano"}
};

static const setup_zone_t g_zones[SETUP_ZONE_COUNT] = {
    {-480, 1, "UTC-08:00", "Pacifico"},
    {-360, 2, "UTC-06:00", "Centroamerica"},
    {-300, 3, "UTC-05:00", "Este de America"},
    {-180, 4, "UTC-03:00", "Argentina / Brasil"},
    {   0, 5, "UTC+00:00", "Greenwich"},
    {  60, 6, "UTC+01:00", "Europa central"},
    { 180, 7, "UTC+03:00", "Europa oriental"},
    { 540, 8, "UTC+09:00", "Japon"},
    { 600, 9, "UTC+10:00", "Australia oriental"}
};

static const char *g_step_names[] = {
    "Bienvenida", "Licencia", "Identidad", "Fecha y hora",
    "Componentes", "Resumen", "Instalando", "Finalizado"
};

typedef struct {
    bk_gui_desktop_t *desktop;
    bk_gui_window_t *window;
    bk_gui_widget_t *back_button;
    bk_gui_widget_t *next_button;
    bk_gui_widget_t *cancel_button;
    bk_gui_widget_t *user_box;
    bk_gui_widget_t *computer_box;
    bk_gui_widget_t *license_box;
    uint32_t back_id;
    uint32_t next_id;
    uint32_t cancel_id;
    setup_page_t page;
    bool accepted_terms;
    bool include_wine;
    bool startup_sound;
    bool clock_24h;
    bool worker_started;
    bool cancel_warning;
    volatile bool worker_done;
    volatile bool worker_failed;
    volatile uint32_t package_step;
    volatile uint32_t package_completed;
    uint32_t package_total;
    volatile uint32_t file_index;
    volatile uint32_t file_total;
    volatile uint32_t file_bytes_done;
    volatile uint32_t file_bytes_total;
    volatile uint32_t progress_phase;
    volatile uint32_t final_step;
    volatile uint32_t final_completed;
    volatile uint32_t final_total;
    volatile bool finalizing;
    uint32_t last_progress_generation;
    uint32_t last_clock_ms;
    uint32_t last_activity_ms;
    int language_index;
    int zone_index;
    char username[64];
    char computer_name[48];
    char license_code[96];
    char current_package[48];
    char current_file[128];
    char final_action[128];
    char status[160];
    char error[192];
    char *terms;
} setup_state_t;

typedef struct {
    bk_gui_rect_t content;
    bk_gui_rect_t header;
    bk_gui_rect_t sidebar;
    bk_gui_rect_t page;
    bk_gui_rect_t footer;
    bk_gui_rect_t terms_box;
    bk_gui_rect_t terms_check;
    bk_gui_rect_t language_list;
    bk_gui_rect_t zone_list;
    bk_gui_rect_t calendar;
    bk_gui_rect_t clock;
    bk_gui_rect_t wine_check;
    bk_gui_rect_t sound_check;
    bk_gui_rect_t format_check;
} setup_layout_t;

static setup_state_t *g_setup;

static bool setup_bad_config_char(char c) {
    return c == '\r' || c == '\n' || c == '=';
}

static bool setup_clean_field(char *text) {
    uint32_t length = st_length(text);
    if (!length) return false;
    for (uint32_t i = 0; i < length; i++)
        if (setup_bad_config_char(text[i])) return false;
    return true;
}

static bool setup_is_leap(uint16_t year) {
    return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
}

static uint8_t setup_days_in_month(uint16_t year, uint8_t month) {
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1U || month > 12U) return 30U;
    if (month == 2U && setup_is_leap(year)) return 29U;
    return days[month - 1U];
}

static uint8_t setup_weekday(uint16_t year, uint8_t month, uint8_t day) {
    static const uint8_t t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    uint32_t y = year;
    if (month < 3U) y--;
    return (uint8_t)((y + y/4U - y/100U + y/400U + t[month-1U] + day) % 7U);
}

static void setup_adjust_datetime(bk_datetime_t *dt, int32_t offset_minutes) {
    int total;
    int day_delta = 0;
    if (!dt) return;
    total = (int)dt->time.hour * 60 + (int)dt->time.minute + offset_minutes;
    while (total < 0) { total += 1440; day_delta--; }
    while (total >= 1440) { total -= 1440; day_delta++; }
    dt->time.hour = (uint8_t)(total / 60);
    dt->time.minute = (uint8_t)(total % 60);
    while (day_delta > 0) {
        uint8_t dim = setup_days_in_month(dt->date.year, dt->date.month);
        if (dt->date.day < dim) dt->date.day++;
        else {
            dt->date.day = 1U;
            if (dt->date.month < 12U) dt->date.month++;
            else { dt->date.month = 1U; dt->date.year++; }
        }
        day_delta--;
    }
    while (day_delta < 0) {
        if (dt->date.day > 1U) dt->date.day--;
        else {
            if (dt->date.month > 1U) dt->date.month--;
            else { dt->date.month = 12U; dt->date.year--; }
            dt->date.day = setup_days_in_month(dt->date.year, dt->date.month);
        }
        day_delta++;
    }
}

static void setup_two_digits(char out[3], uint32_t value) {
    out[0] = (char)('0' + ((value / 10U) % 10U));
    out[1] = (char)('0' + (value % 10U));
    out[2] = '\0';
}

static void setup_signed(char *out, uint32_t capacity, int32_t value) {
    char number[16];
    if (!out || capacity < 2U) return;
    out[0] = '\0';
    if (value < 0) {
        st_append(out, capacity, "-");
        st_u32(number, sizeof(number), (uint32_t)(-value));
    } else {
        st_u32(number, sizeof(number), (uint32_t)value);
    }
    st_append(out, capacity, number);
}

static void setup_set_status(setup_state_t *state, const char *status) {
    st_copy(state->status, sizeof(state->status), status);
    if (state->window) bk_gui_window_invalidate(state->window);
}

static void setup_set_error(setup_state_t *state, const char *error) {
    st_copy(state->error, sizeof(state->error), error);
    state->worker_failed = true;
    state->worker_done = true;
    if (state->window) bk_gui_window_invalidate(state->window);
}

static const char *setup_basename(const char *path) {
    const char *name = path;
    if (!path) return "";
    for (const char *scan = path; *scan; scan++)
        if (*scan == '/') name = scan + 1;
    return name;
}

static const char *setup_progress_phase_text(uint32_t phase) {
    switch ((bkl_setup_phase_t)phase) {
        case BKL_SETUP_PHASE_OPENING: return "Abriendo el paquete BKL3...";
        case BKL_SETUP_PHASE_READING_HEADER: return "Leyendo la lista de archivos...";
        case BKL_SETUP_PHASE_DECODING: return "Descomprimiendo el archivo actual...";
        case BKL_SETUP_PHASE_VERIFYING: return "Verificando CRC32...";
        case BKL_SETUP_PHASE_COMMITTING: return "Escribiendo el archivo en FAT32...";
        case BKL_SETUP_PHASE_FILE_DONE: return "Archivo instalado correctamente.";
        case BKL_SETUP_PHASE_PACKAGE_DONE: return "Paquete completado.";
        case BKL_SETUP_PHASE_ERROR: return "La extraccion encontro un error.";
        default: return "Preparando la instalacion...";
    }
}

static void setup_refresh_bkl_progress(setup_state_t *state) {
    bkl_setup_progress_t progress;
    if (!state || state->finalizing || !bk_setup_get_progress(&progress)) return;
    if (progress.generation == state->last_progress_generation) return;
    state->last_progress_generation = progress.generation;
    state->file_index = progress.file_index;
    state->file_total = progress.file_total;
    state->file_bytes_done = progress.file_bytes_done;
    state->file_bytes_total = progress.file_bytes_total;
    state->progress_phase = progress.phase;
    st_copy(state->current_file, sizeof(state->current_file),
            setup_basename(progress.path));
    if (state->window) bk_gui_window_invalidate(state->window);
}

static void setup_log_final(uint32_t step, uint32_t total,
                            const char *verb, const char *action,
                            uint32_t elapsed_ms, bool result_known,
                            bool ok) {
    char line[256];
    char number[16];
    line[0] = '\0';
    st_append(line, sizeof(line), "[SETUP:APP] final ");
    st_u32(number, sizeof(number), step);
    st_append(line, sizeof(line), number);
    st_append(line, sizeof(line), "/");
    st_u32(number, sizeof(number), total);
    st_append(line, sizeof(line), number);
    st_append(line, sizeof(line), " ");
    st_append(line, sizeof(line), verb);
    st_append(line, sizeof(line), ": ");
    st_append(line, sizeof(line), action ? action : "(sin nombre)");
    if (result_known) {
        st_append(line, sizeof(line), " result=");
        st_append(line, sizeof(line), ok ? "OK" : "FAIL");
        st_append(line, sizeof(line), " elapsed=");
        st_u32(number, sizeof(number), elapsed_ms);
        st_append(line, sizeof(line), number);
        st_append(line, sizeof(line), "ms");
    }
    st_append(line, sizeof(line), "\n");
    bk_sys_log(line);
}

static void setup_final_begin(setup_state_t *state, uint32_t step,
                              uint32_t total, const char *action) {
    state->finalizing = true;
    state->final_step = step;
    state->final_completed = step ? step - 1U : 0U;
    state->final_total = total;
    state->file_index = 0U;
    state->file_total = 0U;
    state->file_bytes_done = 0U;
    state->file_bytes_total = 0U;
    st_copy(state->final_action, sizeof(state->final_action), action);
    setup_set_status(state, action);
    setup_log_final(step, total, "begin", action, 0U, false, false);
}

static bool setup_final_write(setup_state_t *state, uint32_t step,
                              uint32_t total, const char *action,
                              const char *path, const char *data) {
    uint32_t started;
    bool ok;
    setup_final_begin(state, step, total, action);
    started = bk_sys_uptime_ms();
    ok = bk_file_write_all(path, data, st_length(data));
    setup_log_final(step, total, "done", action,
                    bk_sys_uptime_ms() - started, true, ok);
    state->final_completed = step;
    if (state->window) bk_gui_window_invalidate(state->window);
    return ok;
}

static void setup_final_remove(setup_state_t *state, uint32_t step,
                               uint32_t total, const char *action,
                               const char *path) {
    uint32_t started;
    bool ok;
    setup_final_begin(state, step, total, action);
    started = bk_sys_uptime_ms();
    ok = bk_file_remove(path);
    setup_log_final(step, total, "done", action,
                    bk_sys_uptime_ms() - started, true, ok);
    state->final_completed = step;
    if (state->window) bk_gui_window_invalidate(state->window);
}

static void setup_load_terms(setup_state_t *state) {
    void *raw = NULL;
    uint32_t size = 0U;
    if (bk_file_read_all(SETUP_TERMS, &raw, &size) && raw) {
        state->terms = (char *)bk_sys_alloc(size + 1U);
        if (state->terms) {
            for (uint32_t i = 0; i < size; i++) state->terms[i] = ((char *)raw)[i];
            state->terms[size] = '\0';
        }
        bk_sys_free(raw);
    }
    if (!state->terms) {
        const char *fallback =
            "BlesKernOS se distribuye bajo la licencia MIT. Puede usar, "
            "copiar, modificar y distribuir el sistema conservando los avisos "
            "de licencia. El software se entrega tal cual y sin garantia.";
        state->terms = (char *)bk_sys_alloc(st_length(fallback) + 1U);
        if (state->terms) st_copy(state->terms, st_length(fallback) + 1U, fallback);
    }
}

static bool setup_write_configuration(setup_state_t *state,
                                      uint32_t *step, uint32_t total) {
    char profile[256];
    char timezone[192];
    char license[256];
    char features[128];
    char datetime[80];
    char language[40];
    char sound[128];
    char offset[24];
    char zone_index[12];
    const setup_zone_t *zone = &g_zones[state->zone_index];
    const setup_language_t *lang = &g_languages[state->language_index];

    profile[0] = '\0';
    st_append(profile, sizeof(profile), "[USER]\r\nName=");
    st_append(profile, sizeof(profile), state->username);
    st_append(profile, sizeof(profile), "\r\nComputerName=");
    st_append(profile, sizeof(profile), state->computer_name);
    st_append(profile, sizeof(profile), "\r\n");

    setup_signed(offset, sizeof(offset), zone->minutes);
    timezone[0] = '\0';
    st_append(timezone, sizeof(timezone), "[TIMEZONE]\r\nOffsetMinutes=");
    st_append(timezone, sizeof(timezone), offset);
    st_append(timezone, sizeof(timezone), "\r\nDisplay=");
    st_append(timezone, sizeof(timezone), zone->label);
    st_append(timezone, sizeof(timezone), "\r\nRegion=");
    st_append(timezone, sizeof(timezone), zone->place);
    st_append(timezone, sizeof(timezone), "\r\n");

    license[0] = '\0';
    st_append(license, sizeof(license),
        "[LICENSE]\r\nAccepted=1\r\nType=MIT\r\nCode=");
    st_append(license, sizeof(license), state->license_code);
    st_append(license, sizeof(license),
        "\r\nValidation=DevelopmentAcceptAny\r\n");

    features[0] = '\0';
    st_append(features, sizeof(features), "[FEATURES]\r\nWine=");
    st_append(features, sizeof(features), state->include_wine ? "1" : "0");
    st_append(features, sizeof(features), "\r\nStartupSound=");
    st_append(features, sizeof(features), state->startup_sound ? "1" : "0");
    st_append(features, sizeof(features), "\r\n");

    st_u32(zone_index, sizeof(zone_index), (uint32_t)zone->config_index);
    datetime[0] = '\0';
    st_append(datetime, sizeof(datetime), "format=");
    st_append(datetime, sizeof(datetime), state->clock_24h ? "24" : "12");
    st_append(datetime, sizeof(datetime), "\r\ntimezone=");
    st_append(datetime, sizeof(datetime), zone_index);
    st_append(datetime, sizeof(datetime), "\r\n");

    language[0] = '\0';
    st_append(language, sizeof(language), "language=");
    st_append(language, sizeof(language), lang->code);
    st_append(language, sizeof(language), "\r\n");

    st_copy(sound, sizeof(sound), state->startup_sound
        ? "[SOUND]\r\nStartupEnabled=1\r\nStartupSound=/SYSTEM/SOUNDS/START.WAV\r\n"
        : "[SOUND]\r\nStartupEnabled=0\r\nStartupSound=/SYSTEM/SOUNDS/START.WAV\r\n");

    if (!setup_final_write(state, ++(*step), total, "Guardando PROFILE.INI...",
                           "/SYSTEM/USER/PROFILE.INI", profile)) return false;
    if (!setup_final_write(state, ++(*step), total, "Guardando TIMEZONE.INI...",
                           "/SYSTEM/USER/CONFIG/TIMEZONE.INI", timezone)) return false;
    if (!setup_final_write(state, ++(*step), total, "Guardando DATETIME.INI...",
                           "/SYSTEM/USER/CONFIG/DATETIME.INI", datetime)) return false;
    if (!setup_final_write(state, ++(*step), total, "Guardando LANGUAGE.INI...",
                           "/SYSTEM/USER/CONFIG/LANGUAGE.INI", language)) return false;
    if (!setup_final_write(state, ++(*step), total, "Guardando SOUND.INI...",
                           "/SYSTEM/USER/CONFIG/SOUND.INI", sound)) return false;
    if (!setup_final_write(state, ++(*step), total, "Guardando LICENSE.INI...",
                           "/SYSTEM/USER/LICENSE.INI", license)) return false;
    if (!setup_final_write(state, ++(*step), total, "Guardando FEATURES.INI...",
                           "/SYSTEM/USER/CONFIG/FEATURES.INI", features)) return false;
    return true;
}

static bool setup_extract(setup_state_t *state, const char *name,
                          const char *path, uint32_t step) {
    bool ok;
    state->finalizing = false;
    state->package_step = step;
    state->package_completed = step - 1U;
    state->file_index = 0U;
    state->file_total = 0U;
    state->file_bytes_done = 0U;
    state->file_bytes_total = 0U;
    state->progress_phase = BKL_SETUP_PHASE_OPENING;
    state->last_progress_generation = 0U;
    state->current_file[0] = '\0';
    st_copy(state->current_package, sizeof(state->current_package),
            setup_basename(path));
    setup_set_status(state, name);
    ok = bk_setup_extract_package(path);
    if (ok) state->package_completed = step;
    return ok;
}

static void setup_worker(void *argument) {
    setup_state_t *state = (setup_state_t *)argument;
    uint32_t final_step = 0U;
    const uint32_t final_total = 17U;
    const char *normal_start =
        "[START]\r\nBoot=Normal\r\nFirstRunComplete=1\r\n";
    uint32_t started;
    if (!state) return;

    state->package_total = state->include_wine ? 4U : 3U;
    if (!setup_extract(state, "Descomprimiendo el nucleo del sistema...", SETUP_CORE, 1U)) {
        setup_set_error(state, "CORE.BKL no pudo extraerse o fallo su CRC32.");
        return;
    }
    if (!setup_extract(state, "Instalando programas y herramientas...", SETUP_PROGRAMS, 2U)) {
        setup_set_error(state, "PROGRAMS.BKL no pudo extraerse.");
        return;
    }
    if (!setup_extract(state, "Copiando iconos, sonidos y documentos...", SETUP_ASSETS, 3U)) {
        setup_set_error(state, "ASSETS.BKL no pudo extraerse.");
        return;
    }
    if (state->include_wine &&
        !setup_extract(state, "Instalando compatibilidad con Windows 95/98...", SETUP_WINE, 4U)) {
        setup_set_error(state, "WINE.BKL no pudo extraerse.");
        return;
    }

    bk_sys_log("[SETUP:APP] packages complete; starting visible finalization\n");
    state->package_completed = state->package_total;
    state->finalizing = true;
    state->final_total = final_total;
    if (!setup_write_configuration(state, &final_step, final_total)) {
        setup_set_error(state, "No se pudieron guardar las preferencias.");
        return;
    }

    setup_final_begin(state, ++final_step, final_total,
                      "Aplicando el idioma seleccionado...");
    started = bk_sys_uptime_ms();
    (void)bk_lang_set(g_languages[state->language_index].code);
    setup_log_final(final_step, final_total, "done",
                    "Aplicando el idioma seleccionado...",
                    bk_sys_uptime_ms() - started, true, true);
    state->final_completed = final_step;

    setup_final_remove(state, ++final_step, final_total,
                       "Eliminando CORE.BKL...", SETUP_CORE);
    setup_final_remove(state, ++final_step, final_total,
                       "Eliminando PROGRAMS.BKL...", SETUP_PROGRAMS);
    setup_final_remove(state, ++final_step, final_total,
                       "Eliminando ASSETS.BKL...", SETUP_ASSETS);
    setup_final_remove(state, ++final_step, final_total,
                       "Eliminando WINE.BKL...", SETUP_WINE);
    setup_final_remove(state, ++final_step, final_total,
                       "Eliminando PACKAGES.INI...", "/SYSTEM/SETUP/PACKAGES.INI");
    setup_final_remove(state, ++final_step, final_total,
                       "Eliminando MANIFEST.TXT...", "/SYSTEM/SETUP/MANIFEST.TXT");
    setup_final_remove(state, ++final_step, final_total,
                       "Eliminando TERMS.TXT...", "/SYSTEM/SETUP/TERMS.TXT");
    setup_final_remove(state, ++final_step, final_total,
                       "Cerrando la carpeta temporal SETUP...", "/SYSTEM/SETUP");

    /* START.INI se cambia al final: si algo se interrumpe antes, el siguiente
     * arranque vuelve al asistente en vez de entrar a un sistema incompleto. */
    if (!setup_final_write(state, ++final_step, final_total,
                           "Activando el arranque normal...",
                           "/SYSTEM/USER/START.INI", normal_start)) {
        setup_set_error(state, "No se pudo activar el arranque normal.");
        return;
    }

    state->final_step = final_total;
    state->final_completed = final_total;
    state->worker_done = true;
    setup_set_status(state, "Instalacion terminada correctamente.");
    bk_sys_log("[SETUP:APP] installation complete; worker done\n");
}

static int setup_visible_step(const setup_state_t *state) {
    if (state->page <= SETUP_PAGE_SUMMARY) return (int)state->page;
    if (state->page == SETUP_PAGE_INSTALLING || state->page == SETUP_PAGE_ERROR) return 6;
    return 7;
}

static const char *setup_page_title(setup_page_t page) {
    switch (page) {
        case SETUP_PAGE_WELCOME: return "Bienvenido a BlesKernOS";
        case SETUP_PAGE_LICENSE: return "Licencia del sistema";
        case SETUP_PAGE_IDENTITY: return "Identidad del equipo";
        case SETUP_PAGE_DATETIME: return "Fecha, hora y region";
        case SETUP_PAGE_COMPONENTS: return "Componentes opcionales";
        case SETUP_PAGE_SUMMARY: return "Listo para instalar";
        case SETUP_PAGE_INSTALLING: return "Instalando BlesKernOS";
        case SETUP_PAGE_DONE: return "Instalacion completada";
        default: return "La instalacion encontro un problema";
    }
}

static const char *setup_page_subtitle(setup_page_t page) {
    switch (page) {
        case SETUP_PAGE_WELCOME: return "Este asistente preparara su nuevo sistema.";
        case SETUP_PAGE_LICENSE: return "Lea los terminos e introduzca el codigo de licencia.";
        case SETUP_PAGE_IDENTITY: return "Elija como se identificaran usted y esta computadora.";
        case SETUP_PAGE_DATETIME: return "Seleccione la zona y compruebe el reloj local.";
        case SETUP_PAGE_COMPONENTS: return "Personalice las funciones que desea instalar.";
        case SETUP_PAGE_SUMMARY: return "Revise las opciones antes de copiar los paquetes BKL3.";
        case SETUP_PAGE_INSTALLING: return "No apague ni reinicie el equipo durante este proceso.";
        case SETUP_PAGE_DONE: return "El escritorio estara disponible despues del reinicio.";
        default: return "Los paquetes se conservaron para poder reintentar.";
    }
}

static void setup_layout(setup_state_t *state, setup_layout_t *layout) {
    bk_gui_rect_t c = {0,0,0,0};
    (void)bk_gui_window_content_rect(state->window, &c);
    layout->content = c;
    layout->header = (bk_gui_rect_t){c.x, c.y, c.w, 62};
    layout->sidebar = (bk_gui_rect_t){c.x, c.y + 62, 154, c.h - 114};
    layout->page = (bk_gui_rect_t){c.x + 154, c.y + 62, c.w - 154, c.h - 114};
    layout->footer = (bk_gui_rect_t){c.x, c.y + c.h - 52, c.w, 52};
    layout->terms_box = (bk_gui_rect_t){layout->page.x + 22, layout->page.y + 52,
                                        layout->page.w - 44, 220};
    layout->terms_check = (bk_gui_rect_t){layout->page.x + 24, layout->page.y + 284,
                                          18, 18};
    layout->language_list = (bk_gui_rect_t){layout->page.x + 190,
                                            layout->page.y + 198, 230, 84};
    layout->zone_list = (bk_gui_rect_t){layout->page.x + 20,
                                        layout->page.y + 62, 248, 234};
    layout->calendar = (bk_gui_rect_t){layout->page.x + 286,
                                       layout->page.y + 62, 210, 176};
    layout->clock = (bk_gui_rect_t){layout->page.x + 286,
                                    layout->page.y + 248, 210, 74};
    layout->wine_check = (bk_gui_rect_t){layout->page.x + 30,
                                         layout->page.y + 92, 18, 18};
    layout->sound_check = (bk_gui_rect_t){layout->page.x + 30,
                                          layout->page.y + 190, 18, 18};
    layout->format_check = (bk_gui_rect_t){layout->page.x + 286,
                                           layout->page.y + 332, 18, 18};

    /* Los widgets usan coordenadas relativas al cliente de la ventana. */
    bk_gui_widget_set_bounds(state->window, state->back_button,
        (bk_gui_rect_t){c.w - 336, c.h - 39, 96, 27});
    bk_gui_widget_set_bounds(state->window, state->next_button,
        (bk_gui_rect_t){c.w - 232, c.h - 39, 104, 27});
    bk_gui_widget_set_bounds(state->window, state->cancel_button,
        (bk_gui_rect_t){c.w - 120, c.h - 39, 96, 27});
    bk_gui_widget_set_bounds(state->window, state->license_box,
        (bk_gui_rect_t){324, 404, c.w - 354, 24});
    bk_gui_widget_set_bounds(state->window, state->user_box,
        (bk_gui_rect_t){344, 143, c.w - 374, 24});
    bk_gui_widget_set_bounds(state->window, state->computer_box,
        (bk_gui_rect_t){344, 199, c.w - 374, 24});
}

static void setup_widgets(setup_state_t *state) {
    bool license = state->page == SETUP_PAGE_LICENSE;
    bool identity = state->page == SETUP_PAGE_IDENTITY;
    bool navigating = state->page <= SETUP_PAGE_SUMMARY || state->page == SETUP_PAGE_ERROR;
    bool done = state->page == SETUP_PAGE_DONE;

    bk_gui_widget_set_visible(state->window, state->license_box, license);
    bk_gui_widget_set_visible(state->window, state->user_box, identity);
    bk_gui_widget_set_visible(state->window, state->computer_box, identity);
    bk_gui_widget_set_visible(state->window, state->back_button,
                              navigating && state->page != SETUP_PAGE_WELCOME);
    bk_gui_widget_set_visible(state->window, state->next_button,
                              navigating || done);
    bk_gui_widget_set_visible(state->window, state->cancel_button,
                              state->page < SETUP_PAGE_INSTALLING);
    bk_gui_widget_set_enabled(state->next_button,
                              state->page != SETUP_PAGE_LICENSE || state->accepted_terms);
    bk_gui_widget_set_text(state->back_button,
        state->page == SETUP_PAGE_ERROR ? "Volver" : "< Atras");
    bk_gui_widget_set_text(state->next_button,
        done ? "Reiniciar" :
        (state->page == SETUP_PAGE_ERROR ? "Reintentar" :
        (state->page == SETUP_PAGE_SUMMARY ? "Instalar" : "Siguiente >")));
}

static void setup_draw_checkbox(bk_gui_surface_t *surface, bk_gui_rect_t rect,
                                bool checked, const char *label) {
    bk_gui_surface_fill_rect(surface, rect, SETUP_WHITE);
    bk_gui_surface_draw_rect(surface, rect, SETUP_DARK);
    bk_gui_surface_fill_rect(surface,
        (bk_gui_rect_t){rect.x + 1, rect.y + 1, rect.w - 2, 1}, SETUP_SHADOW);
    bk_gui_surface_fill_rect(surface,
        (bk_gui_rect_t){rect.x + 1, rect.y + 1, 1, rect.h - 2}, SETUP_SHADOW);
    if (checked) {
        bk_gui_surface_draw_line(surface, rect.x + 3, rect.y + 9,
                                 rect.x + 7, rect.y + 14, SETUP_NAVY);
        bk_gui_surface_draw_line(surface, rect.x + 7, rect.y + 14,
                                 rect.x + 15, rect.y + 3, SETUP_NAVY);
    }
    bk_gui_surface_draw_text(surface, rect.x + 27, rect.y + 5,
                             label, SETUP_TEXT, 0, false);
}

static void setup_draw_list(bk_gui_surface_t *surface, bk_gui_rect_t rect,
                            int count, int selected, bool zones) {
    st_draw_panel(surface, rect, SETUP_WHITE);
    for (int i = 0; i < count; i++) {
        bk_gui_rect_t row = {rect.x + 3, rect.y + 3 + i * 25, rect.w - 6, 23};
        bool active = i == selected;
        if (active) bk_gui_surface_fill_rect(surface, row, SETUP_SELECT);
        if (zones) {
            bk_gui_surface_draw_text(surface, row.x + 5, row.y + 6,
                g_zones[i].label, active ? SETUP_WHITE : SETUP_TEXT, 0, false);
            bk_gui_surface_draw_text(surface, row.x + 86, row.y + 6,
                g_zones[i].place, active ? SETUP_WHITE : SETUP_MUTED, 0, false);
        } else {
            bk_gui_surface_draw_text(surface, row.x + 8, row.y + 6,
                g_languages[i].name, active ? SETUP_WHITE : SETUP_TEXT, 0, false);
        }
    }
}

static void setup_draw_calendar(bk_gui_surface_t *surface, bk_gui_rect_t rect,
                                const bk_datetime_t *dt) {
    static const char *months[] = {"Enero","Febrero","Marzo","Abril","Mayo","Junio",
        "Julio","Agosto","Septiembre","Octubre","Noviembre","Diciembre"};
    static const char *days[] = {"D","L","M","M","J","V","S"};
    char year[12], number[4];
    uint8_t first, dim;
    st_draw_panel(surface, rect, SETUP_WHITE);
    bk_gui_surface_fill_rect(surface, (bk_gui_rect_t){rect.x+2,rect.y+2,rect.w-4,28}, SETUP_NAVY);
    bk_gui_surface_draw_text(surface, rect.x + 10, rect.y + 10,
        months[dt->date.month - 1U], SETUP_WHITE, 0, false);
    st_u32(year, sizeof(year), dt->date.year);
    bk_gui_surface_draw_text(surface, rect.x + rect.w - 42, rect.y + 10,
        year, SETUP_WHITE, 0, false);
    for (int i=0;i<7;i++)
        bk_gui_surface_draw_text(surface, rect.x+10+i*27, rect.y+38,
                                 days[i], SETUP_NAVY, 0, false);
    first = setup_weekday(dt->date.year, dt->date.month, 1U);
    dim = setup_days_in_month(dt->date.year, dt->date.month);
    for (uint8_t day=1; day<=dim; day++) {
        int cell = (int)first + day - 1;
        int x = rect.x + 8 + (cell % 7) * 27;
        int y = rect.y + 58 + (cell / 7) * 19;
        st_u32(number, sizeof(number), day);
        if (day == dt->date.day) {
            bk_gui_surface_fill_rect(surface, (bk_gui_rect_t){x-2,y-3,23,16}, SETUP_SELECT);
            bk_gui_surface_draw_text(surface, x, y, number, SETUP_WHITE, 0, false);
        } else bk_gui_surface_draw_text(surface, x, y, number, SETUP_TEXT, 0, false);
    }
}

static void setup_draw_clock(bk_gui_surface_t *surface, bk_gui_rect_t rect,
                             const bk_datetime_t *dt, bool format_24h) {
    char hh[3], mm[3], ss[3], text[32];
    uint32_t hour = dt->time.hour;
    const char *suffix = "";
    if (!format_24h) {
        suffix = hour >= 12U ? " PM" : " AM";
        hour %= 12U;
        if (!hour) hour = 12U;
    }
    setup_two_digits(hh, hour);
    setup_two_digits(mm, dt->time.minute);
    setup_two_digits(ss, dt->time.second);
    text[0]='\0'; st_append(text,sizeof(text),hh); st_append(text,sizeof(text),":");
    st_append(text,sizeof(text),mm); st_append(text,sizeof(text),":");
    st_append(text,sizeof(text),ss); st_append(text,sizeof(text),suffix);
    st_draw_panel(surface, rect, 0x00101010U);
    bk_gui_surface_draw_text_px(surface, rect.x + 18, rect.y + 17,
        text, st_length(text), SETUP_GOLD, 24, true, false, true, rect);
}

static void setup_draw_raised_square(bk_gui_surface_t *surface,
                                     bk_gui_rect_t rect, uint32_t face,
                                     bool active, bool completed) {
    /* Two-pixel Win9x-style bevel: white top/left, dark bottom/right. */
    bk_gui_surface_fill_rect(surface, rect, face);
    bk_gui_surface_fill_rect(surface,
        (bk_gui_rect_t){rect.x, rect.y, rect.w, 1}, SETUP_LIGHT);
    bk_gui_surface_fill_rect(surface,
        (bk_gui_rect_t){rect.x, rect.y, 1, rect.h}, SETUP_LIGHT);
    bk_gui_surface_fill_rect(surface,
        (bk_gui_rect_t){rect.x + 1, rect.y + 1, rect.w - 2, 1},
        active ? 0x00FFF0A0U : 0x00E0E0E0U);
    bk_gui_surface_fill_rect(surface,
        (bk_gui_rect_t){rect.x + 1, rect.y + 1, 1, rect.h - 2},
        active ? 0x00FFF0A0U : 0x00E0E0E0U);
    bk_gui_surface_fill_rect(surface,
        (bk_gui_rect_t){rect.x, rect.y + rect.h - 1, rect.w, 1}, SETUP_DARK);
    bk_gui_surface_fill_rect(surface,
        (bk_gui_rect_t){rect.x + rect.w - 1, rect.y, 1, rect.h}, SETUP_DARK);
    bk_gui_surface_fill_rect(surface,
        (bk_gui_rect_t){rect.x + 1, rect.y + rect.h - 2, rect.w - 2, 1},
        SETUP_SHADOW);
    bk_gui_surface_fill_rect(surface,
        (bk_gui_rect_t){rect.x + rect.w - 2, rect.y + 1, 1, rect.h - 2},
        SETUP_SHADOW);

    if (completed) {
        bk_gui_surface_draw_line(surface, rect.x + 4, rect.y + 8,
                                 rect.x + 7, rect.y + 12, SETUP_WHITE);
        bk_gui_surface_draw_line(surface, rect.x + 7, rect.y + 12,
                                 rect.x + 13, rect.y + 4, SETUP_WHITE);
        bk_gui_surface_draw_line(surface, rect.x + 4, rect.y + 9,
                                 rect.x + 7, rect.y + 13, SETUP_DARK);
    } else if (active) {
        bk_gui_surface_fill_rect(surface,
            (bk_gui_rect_t){rect.x + 6, rect.y + 6, 4, 4}, SETUP_NAVY_2);
        bk_gui_surface_fill_rect(surface,
            (bk_gui_rect_t){rect.x + 7, rect.y + 7, 2, 2}, SETUP_LIGHT);
    }
}

static void setup_draw_sidebar(bk_gui_surface_t *surface,
                               const setup_layout_t *layout,
                               const setup_state_t *state) {
    int current = setup_visible_step(state);
    bk_gui_surface_fill_rect(surface, layout->sidebar, SETUP_NAVY_2);
    bk_gui_surface_fill_rect(surface,
        (bk_gui_rect_t){layout->sidebar.x + layout->sidebar.w - 2,
                        layout->sidebar.y, 2, layout->sidebar.h}, SETUP_DARK);
    bk_gui_surface_draw_text(surface, layout->sidebar.x + 15,
        layout->sidebar.y + 18, "PASOS", SETUP_GOLD, 0, false);
    for (int i=0;i<8;i++) {
        bk_gui_rect_t row = {layout->sidebar.x + 8,
                             layout->sidebar.y + 47 + i*37,
                             layout->sidebar.w - 16, 29};
        if (i == current) {
            bk_gui_surface_fill_rect(surface, row, SETUP_TEAL);
            bk_gui_surface_draw_rect(surface, row, SETUP_LIGHT);
        }
        setup_draw_raised_square(surface,
            (bk_gui_rect_t){row.x+5,row.y+6,16,16},
            i < current ? SETUP_GREEN :
            (i == current ? SETUP_GOLD : SETUP_SHADOW),
            i == current, i < current);
        bk_gui_surface_draw_text(surface, row.x + 28, row.y + 9,
            g_step_names[i], i == current ? SETUP_WHITE : 0x00D8D8D8U, 0, false);
    }
}

static void setup_draw_summary_line(bk_gui_surface_t *surface, int x, int y,
                                    const char *label, const char *value) {
    bk_gui_surface_draw_text(surface, x, y, label, SETUP_MUTED, 0, false);
    bk_gui_surface_draw_text(surface, x + 150, y, value, SETUP_TEXT, 0, false);
}

static void setup_draw_segmented_bar(bk_gui_surface_t *surface,
                                     bk_gui_rect_t rect,
                                     uint32_t percent) {
    bk_gui_surface_draw_progress(surface, rect, percent,
                                 BK_GUI_PROGRESS_INSTALLER, 0U);
}

static uint32_t setup_file_percent(const setup_state_t *state) {
    if (!state) return 0U;
    if (state->finalizing) {
        if (!state->final_total) return 0U;
        return (uint32_t)(((uint64_t)state->final_completed * 100U) /
                          state->final_total);
    }
    if (!state->file_bytes_total) {
        return state->progress_phase == BKL_SETUP_PHASE_FILE_DONE ? 100U : 0U;
    }
    return (uint32_t)(((uint64_t)state->file_bytes_done * 100U) /
                      state->file_bytes_total);
}

static uint32_t setup_package_fraction_milli(const setup_state_t *state) {
    uint32_t completed_files;
    uint32_t current_milli = 0U;
    if (!state || !state->file_total) return 0U;
    completed_files = state->file_index ? state->file_index - 1U : 0U;
    if (state->progress_phase == BKL_SETUP_PHASE_FILE_DONE ||
        state->progress_phase == BKL_SETUP_PHASE_PACKAGE_DONE) {
        completed_files = state->file_index;
    } else if (state->file_bytes_total) {
        current_milli = (uint32_t)(((uint64_t)state->file_bytes_done * 1000U) /
                                   state->file_bytes_total);
    }
    if (completed_files > state->file_total) completed_files = state->file_total;
    return (uint32_t)((((uint64_t)completed_files * 1000U) + current_milli) /
                      state->file_total);
}

static uint32_t setup_total_percent(const setup_state_t *state) {
    uint32_t package_milli;
    uint64_t total_milli;
    if (!state || !state->package_total) return 0U;
    if (state->finalizing) return 100U;
    package_milli = setup_package_fraction_milli(state);
    total_milli = (uint64_t)state->package_completed * 1000U + package_milli;
    return (uint32_t)((total_milli * 100U) /
                      ((uint64_t)state->package_total * 1000U));
}

static void setup_draw_activity(bk_gui_surface_t *surface, int x, int y,
                                uint32_t frame) {
    for (uint32_t i = 0U; i < 8U; i++) {
        uint32_t color = i == (frame % 8U) ? SETUP_GOLD : SETUP_SHADOW;
        bk_gui_rect_t block = {x + (int)i * 13, y, 9, 7};
        bk_gui_surface_fill_rect(surface, block, color);
        bk_gui_surface_fill_rect(surface,
            (bk_gui_rect_t){block.x, block.y, block.w, 1}, SETUP_LIGHT);
        bk_gui_surface_fill_rect(surface,
            (bk_gui_rect_t){block.x, block.y, 1, block.h}, SETUP_LIGHT);
        bk_gui_surface_fill_rect(surface,
            (bk_gui_rect_t){block.x, block.y + block.h - 1, block.w, 1}, SETUP_DARK);
        bk_gui_surface_fill_rect(surface,
            (bk_gui_rect_t){block.x + block.w - 1, block.y, 1, block.h}, SETUP_DARK);
    }
}

static void setup_paint(bk_gui_window_t *window UNUSED,
                        bk_gui_surface_t *surface, void *context) {
    setup_state_t *state = (setup_state_t *)context;
    setup_layout_t l;
    bk_datetime_t dt;
    if (!state || !surface) return;
    setup_layout(state, &l);
    setup_widgets(state);

    bk_gui_surface_fill_rect(surface, l.content, SETUP_FACE);
    bk_gui_surface_fill_rect(surface, l.header, SETUP_NAVY);
    bk_gui_surface_fill_rect(surface,
        (bk_gui_rect_t){l.header.x,l.header.y+l.header.h-2,l.header.w,2}, SETUP_GOLD);
    bk_gui_surface_draw_text_px(surface, l.header.x+20, l.header.y+11,
        "BlesKernOS 0.8 Setup", 21, SETUP_WHITE, 20, true, false, false, l.header);
    bk_gui_surface_draw_text(surface, l.header.x+22, l.header.y+39,
        "Asistente de instalacion y primer inicio", 0x00D8D8E8U, 0, false);
    setup_draw_sidebar(surface, &l, state);
    bk_gui_surface_fill_rect(surface, l.page, SETUP_FACE);
    bk_gui_surface_fill_rect(surface, l.footer, SETUP_FACE);
    bk_gui_surface_fill_rect(surface,
        (bk_gui_rect_t){l.footer.x,l.footer.y,l.footer.w,1}, SETUP_SHADOW);
    bk_gui_surface_fill_rect(surface,
        (bk_gui_rect_t){l.footer.x,l.footer.y+1,l.footer.w,1}, SETUP_LIGHT);

    bk_gui_surface_draw_text_px(surface, l.page.x+20, l.page.y+15,
        setup_page_title(state->page), st_length(setup_page_title(state->page)),
        SETUP_NAVY, 17, true, false, false, l.page);
    bk_gui_surface_draw_text(surface, l.page.x+21, l.page.y+38,
        setup_page_subtitle(state->page), SETUP_MUTED, 0, false);

    if (state->page == SETUP_PAGE_WELCOME) {
        st_draw_panel(surface, (bk_gui_rect_t){l.page.x+24,l.page.y+76,l.page.w-48,238}, SETUP_WHITE);
        bk_gui_surface_fill_rect(surface,
            (bk_gui_rect_t){l.page.x+44,l.page.y+98,86,86}, SETUP_NAVY);
        bk_gui_surface_draw_text_px(surface, l.page.x+58,l.page.y+121,
            "BK",2,SETUP_GOLD,32,true,false,false,
            (bk_gui_rect_t){l.page.x+44,l.page.y+98,86,86});
        bk_gui_surface_draw_text_px(surface, l.page.x+150,l.page.y+105,
            "Bienvenido",10,SETUP_NAVY,26,true,false,false,l.page);
        (void)st_draw_wrapped(surface,
            (bk_gui_rect_t){l.page.x+150,l.page.y+148,l.page.w-205,110},
            l.page.x+150,l.page.y+150,
            "El sistema base ya fue copiado al disco. En los proximos pasos "
            "elegira su identidad, region y componentes antes de que BKL3 "
            "descomprima el resto de BlesKernOS.", SETUP_TEXT, 17);
        bk_gui_surface_draw_text(surface, l.page.x+44,l.page.y+284,
            "Presione Siguiente para continuar.", SETUP_NAVY, 0, false);
    } else if (state->page == SETUP_PAGE_LICENSE) {
        st_draw_panel(surface, l.terms_box, SETUP_WHITE);
        if (state->terms)
            (void)st_draw_wrapped(surface,
                (bk_gui_rect_t){l.terms_box.x+8,l.terms_box.y+7,
                                l.terms_box.w-16,l.terms_box.h-14},
                l.terms_box.x+10,l.terms_box.y+10,state->terms,SETUP_TEXT,14);
        setup_draw_checkbox(surface, l.terms_check, state->accepted_terms,
                            "Acepto los terminos y la licencia MIT");
        bk_gui_surface_draw_text(surface, l.page.x+24,l.page.y+349,
                                 "Codigo de licencia:", SETUP_TEXT, 0, false);
        bk_gui_surface_draw_text(surface, l.page.x+24,l.page.y+386,
            "En esta version de desarrollo se acepta cualquier codigo no vacio.",
            SETUP_MUTED,0,false);
        if (state->error[0])
            bk_gui_surface_draw_text(surface,l.page.x+24,l.page.y+408,
                                     state->error,SETUP_RED,0,false);
    } else if (state->page == SETUP_PAGE_IDENTITY) {
        st_draw_panel(surface,(bk_gui_rect_t){l.page.x+24,l.page.y+70,l.page.w-48,244},SETUP_FACE);
        bk_gui_surface_draw_text(surface,l.page.x+44,l.page.y+88,
                                 "Nombre de usuario:",SETUP_TEXT,0,false);
        bk_gui_surface_draw_text(surface,l.page.x+44,l.page.y+144,
                                 "Nombre del equipo:",SETUP_TEXT,0,false);
        bk_gui_surface_draw_text(surface,l.page.x+44,l.page.y+216,
                                 "Idioma principal:",SETUP_TEXT,0,false);
        setup_draw_list(surface,l.language_list,SETUP_LANGUAGE_COUNT,
                        state->language_index,false);
        bk_gui_surface_draw_text(surface,l.page.x+44,l.page.y+278,
            "El nombre del equipo se utilizara para identificarlo en la red.",
            SETUP_MUTED,0,false);
        if (state->error[0]) bk_gui_surface_draw_text(surface,l.page.x+44,
            l.page.y+334,state->error,SETUP_RED,0,false);
    } else if (state->page == SETUP_PAGE_DATETIME) {
        setup_draw_list(surface,l.zone_list,SETUP_ZONE_COUNT,state->zone_index,true);
        if (!bk_time_datetime(&dt)) {
            dt.date.year=2026; dt.date.month=1; dt.date.day=1;
            dt.time.hour=0; dt.time.minute=0; dt.time.second=0;
        }
        setup_adjust_datetime(&dt,g_zones[state->zone_index].minutes);
        setup_draw_calendar(surface,l.calendar,&dt);
        setup_draw_clock(surface,l.clock,&dt,state->clock_24h);
        setup_draw_checkbox(surface,l.format_check,state->clock_24h,
                            "Usar reloj de 24 horas");
        bk_gui_surface_draw_text(surface,l.page.x+20,l.page.y+310,
            "La vista se actualiza usando el reloj del equipo y la zona elegida.",
            SETUP_MUTED,0,false);
    } else if (state->page == SETUP_PAGE_COMPONENTS) {
        st_draw_panel(surface,(bk_gui_rect_t){l.page.x+22,l.page.y+70,l.page.w-44,262},SETUP_WHITE);
        setup_draw_checkbox(surface,l.wine_check,state->include_wine,
                            "Compatibilidad con aplicaciones Windows 95/98");
        (void)st_draw_wrapped(surface,
            (bk_gui_rect_t){l.page.x+60,l.page.y+119,l.page.w-105,55},
            l.page.x+61,l.page.y+120,
            "Descomprime WINE.BKL y agrega las DLL y el lanzador PE. "
            "Desmarquelo para obtener una instalacion mas pequena.",SETUP_MUTED,15);
        setup_draw_checkbox(surface,l.sound_check,state->startup_sound,
                            "Reproducir sonido al iniciar BlesKernOS");
        (void)st_draw_wrapped(surface,
            (bk_gui_rect_t){l.page.x+60,l.page.y+217,l.page.w-105,50},
            l.page.x+61,l.page.y+218,
            "Puede cambiar esta opcion mas adelante desde el Panel de control.",
            SETUP_MUTED,15);
        bk_gui_surface_fill_rect(surface,(bk_gui_rect_t){l.page.x+35,l.page.y+288,
            l.page.w-70,1},SETUP_SHADOW);
        bk_gui_surface_draw_text(surface,l.page.x+40,l.page.y+305,
            state->include_wine ? "Se instalaran 4 paquetes BKL3." :
                                  "Se instalaran 3 paquetes BKL3; WINE.BKL sera omitido.",
            SETUP_NAVY,0,false);
    } else if (state->page == SETUP_PAGE_SUMMARY) {
        st_draw_panel(surface,(bk_gui_rect_t){l.page.x+24,l.page.y+72,l.page.w-48,250},SETUP_WHITE);
        setup_draw_summary_line(surface,l.page.x+46,l.page.y+98,"Usuario:",state->username);
        setup_draw_summary_line(surface,l.page.x+46,l.page.y+126,"Equipo:",state->computer_name);
        setup_draw_summary_line(surface,l.page.x+46,l.page.y+154,"Idioma:",g_languages[state->language_index].name);
        setup_draw_summary_line(surface,l.page.x+46,l.page.y+182,"Zona horaria:",g_zones[state->zone_index].label);
        setup_draw_summary_line(surface,l.page.x+46,l.page.y+210,"Formato de hora:",state->clock_24h?"24 horas":"12 horas");
        setup_draw_summary_line(surface,l.page.x+46,l.page.y+238,"Compatibilidad Wine:",state->include_wine?"Instalar":"Omitir");
        setup_draw_summary_line(surface,l.page.x+46,l.page.y+266,"Sonido de inicio:",state->startup_sound?"Activado":"Desactivado");
        bk_gui_surface_draw_text(surface,l.page.x+46,l.page.y+302,
            "Haga clic en Instalar para aplicar estas opciones.",SETUP_NAVY,0,false);
    } else if (state->page == SETUP_PAGE_INSTALLING) {
        uint32_t total_percent = setup_total_percent(state);
        uint32_t item_percent = setup_file_percent(state);
        uint32_t activity = bk_sys_uptime_ms() / 250U;
        char detail[192];
        char number[16];
        char percent_text[24];

        st_draw_panel(surface,
            (bk_gui_rect_t){l.page.x+24,l.page.y+68,l.page.w-48,310},
            SETUP_WHITE);
        bk_gui_surface_draw_text(surface,l.page.x+45,l.page.y+88,
            "Finalizando la instalacion. No apague el equipo.",SETUP_TEXT,0,false);

        bk_gui_surface_draw_text(surface,l.page.x+45,l.page.y+122,
            "Progreso total",SETUP_NAVY,0,false);
        setup_draw_segmented_bar(surface,
            (bk_gui_rect_t){l.page.x+45,l.page.y+142,l.page.w-90,28},
            total_percent);
        percent_text[0]='\0';
        st_u32(number,sizeof(number),total_percent);
        st_append(percent_text,sizeof(percent_text),number);
        st_append(percent_text,sizeof(percent_text),"%");
        bk_gui_surface_draw_text(surface,l.page.x+l.page.w-82,l.page.y+123,
            percent_text,SETUP_MUTED,0,false);

        detail[0]='\0';
        if (state->finalizing) {
            st_append(detail,sizeof(detail),"Paquetes BKL3 completados. Finalizando el sistema.");
        } else {
            st_append(detail,sizeof(detail),"Paquete ");
            st_u32(number,sizeof(number),state->package_step);
            st_append(detail,sizeof(detail),number);
            st_append(detail,sizeof(detail)," de ");
            st_u32(number,sizeof(number),state->package_total);
            st_append(detail,sizeof(detail),number);
            if (state->current_package[0]) {
                st_append(detail,sizeof(detail),": ");
                st_append(detail,sizeof(detail),state->current_package);
            }
        }
        bk_gui_surface_draw_text(surface,l.page.x+45,l.page.y+178,
            detail,SETUP_MUTED,0,false);

        bk_gui_surface_draw_text(surface,l.page.x+45,l.page.y+210,
            state->finalizing ? "Tareas de finalizacion" : "Archivo actual",
            SETUP_NAVY,0,false);
        setup_draw_segmented_bar(surface,
            (bk_gui_rect_t){l.page.x+45,l.page.y+230,l.page.w-90,28},
            item_percent);

        detail[0]='\0';
        if (state->finalizing) {
            st_append(detail,sizeof(detail),"Tarea ");
            st_u32(number,sizeof(number),state->final_step);
            st_append(detail,sizeof(detail),number);
            st_append(detail,sizeof(detail)," de ");
            st_u32(number,sizeof(number),state->final_total);
            st_append(detail,sizeof(detail),number);
            st_append(detail,sizeof(detail),": ");
            st_append(detail,sizeof(detail),state->final_action);
        } else if (state->file_total) {
            st_append(detail,sizeof(detail),"Archivo ");
            st_u32(number,sizeof(number),state->file_index);
            st_append(detail,sizeof(detail),number);
            st_append(detail,sizeof(detail)," de ");
            st_u32(number,sizeof(number),state->file_total);
            st_append(detail,sizeof(detail),number);
            st_append(detail,sizeof(detail),": ");
            st_append(detail,sizeof(detail),state->current_file[0] ?
                      state->current_file : "leyendo nombre...");
        } else {
            st_append(detail,sizeof(detail),"Leyendo el contenido del paquete...");
        }
        bk_gui_surface_draw_text(surface,l.page.x+45,l.page.y+267,
            detail,SETUP_TEXT,0,false);

        bk_gui_surface_draw_text(surface,l.page.x+45,l.page.y+297,
            state->finalizing ? state->status :
            setup_progress_phase_text(state->progress_phase),
            SETUP_MUTED,0,false);
        setup_draw_activity(surface,l.page.x+45,l.page.y+326,activity);
        bk_gui_surface_draw_text(surface,l.page.x+162,l.page.y+323,
            "Actividad del instalador",SETUP_MUTED,0,false);
        bk_gui_surface_fill_rect(surface,
            (bk_gui_rect_t){l.page.x+45,l.page.y+350,l.page.w-90,1},SETUP_SHADOW);
        bk_gui_surface_draw_text(surface,l.page.x+45,l.page.y+359,
            "Cada archivo se descomprime, verifica y escribe antes de continuar.",
            SETUP_MUTED,0,false);
    } else if (state->page == SETUP_PAGE_DONE) {
        st_draw_panel(surface,(bk_gui_rect_t){l.page.x+34,l.page.y+92,l.page.w-68,210},SETUP_WHITE);
        bk_gui_surface_fill_rect(surface,(bk_gui_rect_t){l.page.x+56,l.page.y+118,54,54},SETUP_GREEN);
        bk_gui_surface_draw_line(surface,l.page.x+68,l.page.y+145,l.page.x+79,l.page.y+157,SETUP_WHITE);
        bk_gui_surface_draw_line(surface,l.page.x+79,l.page.y+157,l.page.x+99,l.page.y+128,SETUP_WHITE);
        bk_gui_surface_draw_text_px(surface,l.page.x+130,l.page.y+121,
            "BlesKernOS esta listo",22,SETUP_NAVY,22,true,false,false,l.page);
        bk_gui_surface_draw_text(surface,l.page.x+132,l.page.y+165,
            "La configuracion y los componentes fueron instalados.",SETUP_TEXT,0,false);
        bk_gui_surface_draw_text(surface,l.page.x+132,l.page.y+194,
            "Retire el medio de instalacion y reinicie el equipo.",SETUP_TEXT,0,false);
    } else {
        st_draw_panel(surface,(bk_gui_rect_t){l.page.x+34,l.page.y+92,l.page.w-68,220},SETUP_WHITE);
        bk_gui_surface_draw_text_px(surface,l.page.x+55,l.page.y+116,
            "No se pudo completar",20,SETUP_RED,20,true,false,false,l.page);
        (void)st_draw_wrapped(surface,(bk_gui_rect_t){l.page.x+55,l.page.y+160,
            l.page.w-110,95},l.page.x+55,l.page.y+160,state->error,SETUP_TEXT,16);
        bk_gui_surface_draw_text(surface,l.page.x+55,l.page.y+273,
            "Los archivos BKL3 no fueron eliminados y se puede reintentar.",
            SETUP_MUTED,0,false);
    }

    if (state->cancel_warning)
        bk_gui_surface_draw_text(surface,l.footer.x+18,l.footer.y+20,
            "Complete el asistente o apague el equipo para salir.",SETUP_RED,0,false);
}

static bool setup_capture_identity(setup_state_t *state) {
    if (!bk_gui_widget_get_text(state->user_box,state->username,sizeof(state->username)) ||
        !setup_clean_field(state->username)) {
        st_copy(state->error,sizeof(state->error),"Escriba un nombre de usuario valido.");
        return false;
    }
    if (!bk_gui_widget_get_text(state->computer_box,state->computer_name,
                                sizeof(state->computer_name)) ||
        !setup_clean_field(state->computer_name)) {
        st_copy(state->error,sizeof(state->error),"Escriba un nombre valido para el equipo.");
        return false;
    }
    state->error[0]='\0';
    return true;
}

static bool setup_capture_license(setup_state_t *state) {
    if (!state->accepted_terms ||
        !bk_gui_widget_get_text(state->license_box,state->license_code,
                                sizeof(state->license_code)) ||
        !setup_clean_field(state->license_code)) {
        st_copy(state->error,sizeof(state->error),
                "Debe aceptar los terminos e ingresar un codigo no vacio.");
        return false;
    }
    state->error[0]='\0';
    return true;
}

static void setup_begin_install(setup_state_t *state) {
    state->page=SETUP_PAGE_INSTALLING;
    state->package_step=0U;
    state->package_completed=0U;
    state->package_total=state->include_wine?4U:3U;
    state->file_index=0U;
    state->file_total=0U;
    state->file_bytes_done=0U;
    state->file_bytes_total=0U;
    state->progress_phase=BKL_SETUP_PHASE_IDLE;
    state->final_step=0U;
    state->final_completed=0U;
    state->final_total=0U;
    state->finalizing=false;
    state->last_progress_generation=0U;
    state->last_activity_ms=bk_sys_uptime_ms();
    state->current_package[0]='\0';
    state->current_file[0]='\0';
    state->final_action[0]='\0';
    state->worker_done=false;
    state->worker_failed=false;
    setup_set_status(state,"Preparando los paquetes BKL3...");
    setup_widgets(state);
    if (bk_proc_spawn_thread("setup-bkl3",setup_worker,state)<0) {
        state->page=SETUP_PAGE_ERROR;
        st_copy(state->error,sizeof(state->error),"No se pudo crear el hilo de instalacion.");
    } else state->worker_started=true;
    bk_gui_window_invalidate(state->window);
}

static void setup_button(bk_gui_window_t *window UNUSED,uint32_t id) {
    setup_state_t *state=g_setup;
    if (!state) return;
    state->cancel_warning=false;
    if (id==state->cancel_id) {
        state->cancel_warning=true;
        bk_gui_window_invalidate(state->window);
        return;
    }
    if (id==state->back_id) {
        if (state->page==SETUP_PAGE_ERROR) state->page=SETUP_PAGE_SUMMARY;
        else if (state->page>SETUP_PAGE_WELCOME && state->page<=SETUP_PAGE_SUMMARY)
            state->page=(setup_page_t)(state->page-1);
        setup_widgets(state); bk_gui_window_invalidate(state->window); return;
    }
    if (id!=state->next_id) return;
    if (state->page==SETUP_PAGE_LICENSE && !setup_capture_license(state)) {
        bk_gui_window_invalidate(state->window); return;
    }
    if (state->page==SETUP_PAGE_IDENTITY && !setup_capture_identity(state)) {
        bk_gui_window_invalidate(state->window); return;
    }
    if (state->page<SETUP_PAGE_SUMMARY) state->page=(setup_page_t)(state->page+1);
    else if (state->page==SETUP_PAGE_SUMMARY) setup_begin_install(state);
    else if (state->page==SETUP_PAGE_DONE) bk_sys_reboot();
    else if (state->page==SETUP_PAGE_ERROR) {
        state->worker_started=false; state->worker_done=false; state->worker_failed=false;
        setup_begin_install(state);
    }
    setup_widgets(state); bk_gui_window_invalidate(state->window);
}

static int setup_list_hit(bk_gui_rect_t rect,int count,int x,int y) {
    if (!st_rect_contains(rect,x,y)) return -1;
    int row=(y-rect.y-3)/25;
    return row>=0 && row<count?row:-1;
}

static bool setup_event(bk_gui_window_t *window UNUSED,
                        const bk_gui_event_t *event,void *context) {
    setup_state_t *state=(setup_state_t *)context;
    setup_layout_t l;
    int hit;
    if (!state||!event) return false;
    setup_layout(state,&l);
    if (event->type==BK_GUI_EVENT_MOUSE_UP) {
        if (state->page==SETUP_PAGE_LICENSE &&
            st_rect_contains((bk_gui_rect_t){l.terms_check.x,l.terms_check.y,
                                             l.page.w-55,22},event->x,event->y)) {
            state->accepted_terms=!state->accepted_terms; setup_widgets(state);
            bk_gui_window_invalidate(state->window); return true;
        }
        if (state->page==SETUP_PAGE_IDENTITY &&
            (hit=setup_list_hit(l.language_list,SETUP_LANGUAGE_COUNT,event->x,event->y))>=0) {
            state->language_index=hit; bk_gui_window_invalidate(state->window); return true;
        }
        if (state->page==SETUP_PAGE_DATETIME) {
            hit=setup_list_hit(l.zone_list,SETUP_ZONE_COUNT,event->x,event->y);
            if (hit>=0) {state->zone_index=hit;bk_gui_window_invalidate(state->window);return true;}
            if (st_rect_contains((bk_gui_rect_t){l.format_check.x,l.format_check.y,210,22},
                                 event->x,event->y)) {
                state->clock_24h=!state->clock_24h;bk_gui_window_invalidate(state->window);return true;
            }
        }
        if (state->page==SETUP_PAGE_COMPONENTS) {
            if (st_rect_contains((bk_gui_rect_t){l.wine_check.x,l.wine_check.y,l.page.w-70,24},
                                 event->x,event->y)) {
                state->include_wine=!state->include_wine;bk_gui_window_invalidate(state->window);return true;
            }
            if (st_rect_contains((bk_gui_rect_t){l.sound_check.x,l.sound_check.y,l.page.w-70,24},
                                 event->x,event->y)) {
                state->startup_sound=!state->startup_sound;bk_gui_window_invalidate(state->window);return true;
            }
        }
    }
    return false;
}

void bleskernos_program_main(bk_gui_desktop_t *desktop) {
    setup_state_t *state;
    if (bk_sys_api_version()<SETUP_MIN_API) return;
    if (!desktop) desktop=bk_gui_desktop();
    if (!desktop) return;
    state=(setup_state_t *)bk_sys_alloc(sizeof(*state));
    if (!state) return;
    st_zero(state,sizeof(*state));
    state->desktop=desktop;
    state->include_wine=true;
    state->startup_sound=true;
    state->clock_24h=true;
    state->language_index=0;
    state->zone_index=3;
    state->page=SETUP_PAGE_WELCOME;
    state->last_clock_ms=bk_sys_uptime_ms();
    st_copy(state->status,sizeof(state->status),"Esperando opciones...");
    setup_load_terms(state);

    state->window=bk_gui_create_window(desktop,60,28,680,544,"BlesKernOS 0.8 Setup");
    if (!state->window) {
        if (state->terms) bk_sys_free(state->terms);
        bk_sys_free(state); return;
    }
    g_setup=state;
    state->back_button=bk_gui_create_button(desktop,state->window,
        (bk_gui_rect_t){0,0,96,27},"< Atras",setup_button);
    state->next_button=bk_gui_create_button(desktop,state->window,
        (bk_gui_rect_t){0,0,104,27},"Siguiente >",setup_button);
    state->cancel_button=bk_gui_create_button(desktop,state->window,
        (bk_gui_rect_t){0,0,96,27},"Cancelar",setup_button);
    (void)bk_gui_widget_set_icon(state->back_button,"ArrowLeft");
    (void)bk_gui_widget_set_icon(state->next_button,"ArrowRight");
    (void)bk_gui_widget_set_icon(state->cancel_button,"Close");
    state->license_box=bk_gui_create_textbox(desktop,state->window,
        (bk_gui_rect_t){0,0,280,24},"BLES-0.8-USER",95,setup_button);
    state->user_box=bk_gui_create_textbox(desktop,state->window,
        (bk_gui_rect_t){0,0,260,24},"Usuario",63,setup_button);
    state->computer_box=bk_gui_create_textbox(desktop,state->window,
        (bk_gui_rect_t){0,0,240,24},"BLES-PC",47,setup_button);
    state->back_id=bk_gui_widget_id(state->back_button);
    state->next_id=bk_gui_widget_id(state->next_button);
    state->cancel_id=bk_gui_widget_id(state->cancel_button);
    bk_gui_set_window_content(state->window,setup_paint,state);
    bk_gui_set_window_event_handler(state->window,setup_event,state);
    bk_gui_set_window_min_size(state->window,680,544);
    bk_gui_window_set_owner(state->window,bk_sys_getpid());
    bk_proc_bind_window(state->window);
    setup_widgets(state);

    while (bk_gui_window_is_open(state->window)) {
        uint32_t now=bk_sys_uptime_ms();
        if (state->page==SETUP_PAGE_INSTALLING) {
            setup_refresh_bkl_progress(state);
            if (now-state->last_activity_ms>=250U) {
                state->last_activity_ms=now;
                bk_gui_window_invalidate(state->window);
            }
        }
        if (state->page==SETUP_PAGE_INSTALLING && state->worker_done) {
            state->page=state->worker_failed?SETUP_PAGE_ERROR:SETUP_PAGE_DONE;
            setup_widgets(state); bk_gui_window_invalidate(state->window);
        }
        if (state->page==SETUP_PAGE_DATETIME && now-state->last_clock_ms>=1000U) {
            state->last_clock_ms=now; bk_gui_window_invalidate(state->window);
        }
        bk_sys_sleep_ms(20);
    }

    while (state->worker_started&&!state->worker_done) bk_sys_sleep_ms(20);
    if (g_setup==state) g_setup=NULL;
    bk_gui_destroy_window(desktop,state->window);
    if (state->terms) bk_sys_free(state->terms);
    bk_sys_free(state);
}
