#include "system_tools_common.h"

#define HELP_MIN_API 22U
#define HELP_TOPIC_MAX 7U

typedef struct {
    const char *title;
    const char *body;
} help_topic_t;

typedef struct {
    bk_gui_desktop_t *desktop;
    bk_gui_window_t *window;
    uint32_t selected;
    uint32_t scroll_lines;
    bool developer;
    bk_gui_image_t icon;
    bk_gui_image_t open_icon;
    bool have_icon;
    bool have_open_icon;
} help_state_t;

static const help_topic_t g_help_topics[HELP_TOPIC_MAX] = {
    {
        "Bienvenido",
        "BlesKernOS es un sistema operativo de escritorio inspirado en la era "
        "1993-1995. Desde el boton Bles puede abrir Programas, Documentos y "
        "utilidades del sistema. Las ventanas se mueven desde la barra de titulo, "
        "pueden minimizarse y aparecen en la barra inferior.\n\n"
        "Esta ayuda describe las tareas mas comunes y las herramientas incluidas "
        "en la edicion instalada."
    },
    {
        "Escritorio",
        "Use el menu Bles para iniciar programas. El Explorador de archivos abre "
        "unidades, carpetas y documentos. El Panel de control cambia pantalla, "
        "sonido, mouse, teclado, idioma, fecha y red.\n\n"
        "El comando Run permite iniciar una aplicacion por nombre. Ejemplos: "
        "calculator, help_center, find_files y network_status."
    },
    {
        "Archivos",
        "Find Files busca nombres de archivos y carpetas de forma recursiva. "
        "Archive Manager crea archivos BKZ de un solo archivo y puede inspeccionar "
        "o extraer su contenido.\n\n"
        "Antes de modificar un disquete antiguo conviene crear una copia de "
        "seguridad. Nunca formatee una unidad sin verificar dos veces el nombre "
        "del dispositivo."
    },
    {
        "Discos",
        "Disk Tools muestra dispositivos de bloque, particiones MBR y el volumen "
        "FAT activo. Puede ejecutar una comprobacion, reparar errores detectados, "
        "montar una unidad o formatear un dispositivo no activo.\n\n"
        "Las operaciones destructivas requieren una segunda confirmacion. La "
        "reparacion no sustituye una copia de seguridad."
    },
    {
        "Red",
        "Network Status muestra el adaptador, enlace, direccion IPv4, mascara, "
        "gateway, DNS y contadores de paquetes. Renovar DHCP solicita una nueva "
        "configuracion. Ping acepta una direccion IPv4 o un nombre DNS.\n\n"
        "Para Internet deben estar cargados el driver Ethernet, NETSTACK.DVR y, "
        "para HTTPS, TLS.DVR."
    },
    {
        "Portapapeles",
        "Clipboard Viewer muestra el texto copiado por aplicaciones nativas. "
        "Puede reemplazarlo, actualizar la vista o vaciarlo. El Editor de texto "
        "comparte este portapapeles mediante Ctrl+C, Ctrl+X y Ctrl+V.\n\n"
        "Las operaciones de copiar archivos del Explorador usan un portapapeles "
        "de archivos separado para evitar mezclar rutas con texto."
    },
    {
        "Desarrollo",
        "La edicion Developer agrega pruebas Ring 3, ejecutables Win32 de prueba, "
        "headers del SDK y libblesk.a en /SYSTEM/SDK. La edicion User omite esos "
        "archivos para ser mas limpia y pequena.\n\n"
        "Compile aplicaciones ET_REL contra sdk/include/bleskernos_api.h y use "
        "solamente simbolos bk_*. Los programas terminados van en "
        "/SYSTEM/PROGRAMS."
    }
};

static void help_layout(help_state_t *state, bk_gui_rect_t *topics,
                        bk_gui_rect_t *article, bk_gui_rect_t *readme) {
    bk_gui_rect_t content = {0, 0, 0, 0};
    (void)bk_gui_window_content_rect(state->window, &content);
    *topics = (bk_gui_rect_t){content.x + 10, content.y + 10,
                             145, content.h - 54};
    *article = (bk_gui_rect_t){topics->x + topics->w + 10, content.y + 10,
                              content.w - topics->w - 30, content.h - 20};
    *readme = (bk_gui_rect_t){topics->x + 10, content.y + content.h - 34,
                             topics->w - 20, 24};
}

static uint32_t help_topic_count(const help_state_t *state) {
    return state->developer ? HELP_TOPIC_MAX : HELP_TOPIC_MAX - 1U;
}

static void help_detect_edition(help_state_t *state) {
    void *data = NULL;
    uint32_t size = 0;
    state->developer = false;
    if (bk_file_read_all("/SYSTEM/EDITION.TXT", &data, &size) && data) {
        char edition[64];
        uint32_t copied = size < sizeof(edition) - 1U
                        ? size : sizeof(edition) - 1U;
        for (uint32_t i = 0; i < copied; i++)
            edition[i] = ((const char *)data)[i];
        edition[copied] = '\0';
        state->developer = st_contains_ci(edition, "developer");
        bk_sys_free(data);
    }
}

static void help_paint(bk_gui_window_t *window UNUSED,
                       bk_gui_surface_t *surface, void *context) {
    help_state_t *state = (help_state_t *)context;
    bk_gui_rect_t content;
    bk_gui_rect_t topics;
    bk_gui_rect_t article;
    bk_gui_rect_t readme;
    uint32_t topic_count;
    int y;

    if (!state || !surface ||
        !bk_gui_window_content_rect(state->window, &content)) return;
    help_layout(state, &topics, &article, &readme);
    topic_count = help_topic_count(state);
    bk_gui_surface_fill_rect(surface, content, ST_FACE);
    st_draw_panel(surface, topics, ST_PANEL);
    st_draw_panel(surface, article, ST_PANEL);

    y = topics.y + 8;
    for (uint32_t i = 0; i < topic_count; i++) {
        bk_gui_rect_t row = {topics.x + 5, y, topics.w - 10, 24};
        if (i == state->selected) {
            bk_gui_surface_fill_rect(surface, row, ST_SELECT);
            bk_gui_surface_draw_text(surface, row.x + 6, row.y + 8,
                                     g_help_topics[i].title,
                                     ST_SELECT_TXT, 0, false);
        } else {
            bk_gui_surface_draw_text(surface, row.x + 6, row.y + 8,
                                     g_help_topics[i].title,
                                     ST_TEXT, 0, false);
        }
        y += 26;
    }

    if (state->have_icon)
        bk_gui_surface_draw_image(
            surface, (bk_gui_rect_t){article.x + 10, article.y + 5, 24, 24},
            article, &state->icon);
    bk_gui_surface_draw_text(surface, article.x + 42, article.y + 10,
                             g_help_topics[state->selected].title,
                             ST_BLUE, 0, false);
    bk_gui_surface_fill_rect(surface,
        (bk_gui_rect_t){article.x + 10, article.y + 28, article.w - 20, 1},
        ST_SHADOW);

    {
        const char *body = g_help_topics[state->selected].body;
        uint32_t skip = state->scroll_lines;
        while (*body && skip) {
            if (*body == '\n') skip--;
            body++;
        }
        (void)st_draw_wrapped(surface,
            (bk_gui_rect_t){article.x + 10, article.y + 36,
                            article.w - 20, article.h - 46},
            article.x + 12, article.y + 40, body, ST_TEXT, 15);
    }

    st_draw_button(surface, readme, "Abrir README", true, false,
                   state->have_open_icon ? &state->open_icon : NULL);
    bk_gui_surface_draw_text(surface, content.x + 170,
                             content.y + content.h - 16,
                             "Rueda del mouse: desplazar articulo",
                             ST_MUTED, 0, false);
}

static bool help_event(bk_gui_window_t *window UNUSED,
                       const bk_gui_event_t *event, void *context) {
    help_state_t *state = (help_state_t *)context;
    bk_gui_rect_t topics;
    bk_gui_rect_t article;
    bk_gui_rect_t readme;
    uint32_t topic_count;
    if (!state || !event) return false;
    help_layout(state, &topics, &article, &readme);
    topic_count = help_topic_count(state);

    if (event->type == BK_GUI_EVENT_MOUSE_UP) {
        if (st_rect_contains(readme, event->x, event->y)) {
            (void)bk_app_launch("/SYSTEM/PROGRAMS/TEXTEDITOR.BEX", "/README.TXT");
            return true;
        }
        if (st_rect_contains(topics, event->x, event->y)) {
            int row = (event->y - (topics.y + 8)) / 26;
            if (row >= 0 && (uint32_t)row < topic_count) {
                state->selected = (uint32_t)row;
                state->scroll_lines = 0;
                bk_gui_window_invalidate(state->window);
                return true;
            }
        }
    } else if (event->type == BK_GUI_EVENT_MOUSE_WHEEL &&
               st_rect_contains(article, event->x, event->y)) {
        if (event->dy < 0) state->scroll_lines++;
        else if (state->scroll_lines) state->scroll_lines--;
        bk_gui_window_invalidate(state->window);
        return true;
    } else if (event->type == BK_GUI_EVENT_KEY) {
        if ((uint8_t)event->key == BK_KEY_UP && state->selected) {
            state->selected--;
            state->scroll_lines = 0;
            bk_gui_window_invalidate(state->window);
            return true;
        }
        if ((uint8_t)event->key == BK_KEY_DOWN &&
            state->selected + 1U < topic_count) {
            state->selected++;
            state->scroll_lines = 0;
            bk_gui_window_invalidate(state->window);
            return true;
        }
    }
    return false;
}

void bleskernos_program_main(bk_gui_desktop_t *desktop) {
    help_state_t *state;
    if (bk_sys_api_version() < HELP_MIN_API) return;
    if (!desktop) desktop = bk_gui_desktop();
    if (!desktop) return;
    state = (help_state_t *)bk_sys_alloc(sizeof(*state));
    if (!state) return;
    st_zero(state, sizeof(*state));
    state->desktop = desktop;
    state->have_icon = bk_graphics_icon_load("HelpBook", &state->icon);
    state->have_open_icon =
        bk_graphics_icon_load("Open", &state->open_icon);
    help_detect_edition(state);
    state->window = bk_gui_create_window(desktop, 90, 55, 620, 430,
                                         "Bles Help Center");
    if (!state->window) {
        if (state->have_icon) bk_gui_image_free(&state->icon);
        if (state->have_open_icon) bk_gui_image_free(&state->open_icon);
        bk_sys_free(state);
        return;
    }
    bk_gui_set_window_content(state->window, help_paint, state);
    bk_gui_set_window_event_handler(state->window, help_event, state);
    bk_gui_set_window_min_size(state->window, 520, 350);
    bk_gui_window_set_owner(state->window, bk_sys_getpid());

    while (bk_gui_window_is_open(state->window)) bk_sys_sleep_ms(15);
    bk_gui_destroy_window(desktop, state->window);
    if (state->have_icon) bk_gui_image_free(&state->icon);
    if (state->have_open_icon) bk_gui_image_free(&state->open_icon);
    bk_sys_free(state);
}
