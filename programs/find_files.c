#include "system_tools_common.h"

#define FIND_MIN_API 23U
#define FIND_MAX_RESULTS 96U
#define FIND_MAX_DIRS 128U
#define FIND_VISIBLE_ROWS 14U

typedef struct {
    char path[BK_PATH_MAX];
    uint32_t size;
    bool directory;
} find_result_t;

typedef struct {
    bk_gui_desktop_t *desktop;
    bk_gui_window_t *window;
    bk_gui_widget_t *query_box;
    bk_gui_widget_t *path_box;
    bk_gui_widget_t *search_button;
    bk_gui_widget_t *open_button;
    bk_gui_widget_t *folder_button;
    bk_gui_widget_t *clear_button;
    uint32_t search_id;
    uint32_t open_id;
    uint32_t folder_id;
    uint32_t clear_id;
    find_result_t results[FIND_MAX_RESULTS];
    uint32_t result_count;
    uint32_t visited_dirs;
    uint32_t scroll;
    int32_t selected;
    bool pending_search;
    bool searching;
    char status[128];
} find_state_t;

static find_state_t *g_find_state;

static const char *find_extension(const char *path) {
    const char *extension = path;
    if (!path) return "";
    while (*path) {
        if (*path == '.') extension = path;
        if (*path == '/') extension = path + 1;
        path++;
    }
    return extension;
}

static void find_layout(find_state_t *state, bk_gui_rect_t *list,
                        bk_gui_rect_t *status) {
    bk_gui_rect_t content = {0, 0, 0, 0};
    (void)bk_gui_window_content_rect(state->window, &content);
    /* Widget bounds are client-relative; painted rectangles are screen-relative. */
    bk_gui_widget_set_bounds(state->window, state->query_box,
        (bk_gui_rect_t){76, 10, content.w - 170, 22});
    bk_gui_widget_set_bounds(state->window, state->path_box,
        (bk_gui_rect_t){76, 38, content.w - 170, 22});
    bk_gui_widget_set_bounds(state->window, state->search_button,
        (bk_gui_rect_t){content.w - 84, 10, 74, 50});
    *list = (bk_gui_rect_t){content.x + 10, content.y + 72,
                           content.w - 20, content.h - 126};
    *status = (bk_gui_rect_t){content.x + 10, content.y + content.h - 46,
                             content.w - 20, 18};
    bk_gui_widget_set_bounds(state->window, state->open_button,
        (bk_gui_rect_t){10, content.h - 26, 76, 22});
    bk_gui_widget_set_bounds(state->window, state->folder_button,
        (bk_gui_rect_t){92, content.h - 26, 112, 22});
    bk_gui_widget_set_bounds(state->window, state->clear_button,
        (bk_gui_rect_t){content.w - 86, content.h - 26, 76, 22});
}

static void find_set_status(find_state_t *state, const char *text) {
    st_copy(state->status, sizeof(state->status), text);
    if (state->window) bk_gui_window_invalidate(state->window);
}

static bool find_name_matches(const char *name, const char *query) {
    if (!query || !*query || st_equal_ci(query, "*")) return true;
    return st_contains_ci(name, query);
}

static void find_add_result(find_state_t *state, const char *path,
                            const bk_file_entry_t *entry) {
    find_result_t *result;
    if (state->result_count >= FIND_MAX_RESULTS) return;
    result = &state->results[state->result_count++];
    st_copy(result->path, sizeof(result->path), path);
    result->size = entry->size;
    result->directory = entry->type == BK_FILE_NODE_DIRECTORY;
}

static void find_run_search(find_state_t *state) {
    char query[64];
    char root[BK_PATH_MAX];
    char (*directories)[BK_PATH_MAX];
    uint32_t queue_head = 0;
    uint32_t queue_tail = 0;

    state->pending_search = false;
    state->searching = true;
    state->result_count = 0;
    state->visited_dirs = 0;
    state->scroll = 0;
    state->selected = -1;
    (void)bk_gui_widget_get_text(state->query_box, query, sizeof(query));
    (void)bk_gui_widget_get_text(state->path_box, root, sizeof(root));
    if (!root[0]) st_copy(root, sizeof(root), "/");
    if (!query[0]) {
        find_set_status(state, "Escriba un nombre o * para listar todo.");
        state->searching = false;
        return;
    }

    directories = (char (*)[BK_PATH_MAX])bk_sys_alloc(
        FIND_MAX_DIRS * BK_PATH_MAX);
    if (!directories) {
        find_set_status(state, "No hay memoria para iniciar la busqueda.");
        state->searching = false;
        return;
    }
    st_copy(directories[queue_tail++], BK_PATH_MAX, root);
    find_set_status(state, "Buscando...");

    while (queue_head < queue_tail && state->result_count < FIND_MAX_RESULTS) {
        bk_file_entry_t entries[BK_DIRECTORY_MAX];
        uint32_t count = 0;
        char current[BK_PATH_MAX];
        st_copy(current, sizeof(current), directories[queue_head++]);
        state->visited_dirs++;
        if (!bk_file_list_dir(current, entries, BK_DIRECTORY_MAX, &count))
            continue;
        for (uint32_t i = 0; i < count; i++) {
            char full[BK_PATH_MAX];
            if (!entries[i].name[0] || st_equal_ci(entries[i].name, ".") ||
                st_equal_ci(entries[i].name, "..")) continue;
            st_join(full, sizeof(full), current, entries[i].name);
            if (find_name_matches(entries[i].name, query))
                find_add_result(state, full, &entries[i]);
            if (entries[i].type == BK_FILE_NODE_DIRECTORY &&
                queue_tail < FIND_MAX_DIRS)
                st_copy(directories[queue_tail++], BK_PATH_MAX, full);
            if (state->result_count >= FIND_MAX_RESULTS) break;
        }
        if ((state->visited_dirs & 7U) == 0U) {
            bk_gui_window_invalidate(state->window);
            bk_sys_yield();
        }
    }

    bk_sys_free(directories);
    {
        char number[16];
        state->status[0] = '\0';
        st_u32(number, sizeof(number), state->result_count);
        st_append(state->status, sizeof(state->status), number);
        st_append(state->status, sizeof(state->status), " resultado(s), ");
        st_u32(number, sizeof(number), state->visited_dirs);
        st_append(state->status, sizeof(state->status), number);
        st_append(state->status, sizeof(state->status), " carpeta(s) revisadas");
        if (state->result_count >= FIND_MAX_RESULTS)
            st_append(state->status, sizeof(state->status), " (limite alcanzado)");
    }
    state->searching = false;
    bk_gui_window_invalidate(state->window);
}

static void find_open_selected(find_state_t *state, bool folder_only) {
    const find_result_t *result;
    char parent[BK_PATH_MAX];
    const char *extension;
    if (!state || state->selected < 0 ||
        (uint32_t)state->selected >= state->result_count) {
        find_set_status(state, "Seleccione un resultado.");
        return;
    }
    result = &state->results[state->selected];
    if (folder_only) {
        if (result->directory) st_copy(parent, sizeof(parent), result->path);
        else st_parent(parent, sizeof(parent), result->path);
        (void)bk_app_launch("/SYSTEM/PROGRAMS/FILE.BEX", parent);
        return;
    }
    if (result->directory) {
        (void)bk_app_launch("/SYSTEM/PROGRAMS/FILE.BEX", result->path);
        return;
    }
    extension = find_extension(result->path);
    if (st_equal_ci(extension, ".BEX") || st_equal_ci(extension, ".CPL") ||
        st_equal_ci(extension, ".EXE")) {
        (void)bk_app_launch(result->path, NULL);
    } else if (st_equal_ci(extension, ".BMP") ||
               st_equal_ci(extension, ".PNG") ||
               st_equal_ci(extension, ".GIF") ||
               st_equal_ci(extension, ".JPG") ||
               st_equal_ci(extension, ".JPEG")) {
        (void)bk_app_launch("/SYSTEM/PROGRAMS/IMAGEVIEWER.BEX", result->path);
    } else if (st_equal_ci(extension, ".MID") ||
               st_equal_ci(extension, ".MIDI")) {
        (void)bk_app_launch("/SYSTEM/PROGRAMS/MIDAMP.BEX", result->path);
    } else {
        (void)bk_app_launch("/SYSTEM/PROGRAMS/TEXTEDITOR.BEX", result->path);
    }
}

static void find_widget_callback(bk_gui_window_t *window UNUSED,
                                 uint32_t widget_id) {
    find_state_t *state = g_find_state;
    if (!state) return;
    if (widget_id == state->search_id) {
        state->pending_search = true;
    } else if (widget_id == state->open_id) {
        find_open_selected(state, false);
    } else if (widget_id == state->folder_id) {
        find_open_selected(state, true);
    } else if (widget_id == state->clear_id) {
        state->result_count = 0;
        state->selected = -1;
        state->scroll = 0;
        find_set_status(state, "Resultados limpiados.");
    }
}

static void find_paint(bk_gui_window_t *window UNUSED,
                       bk_gui_surface_t *surface, void *context) {
    find_state_t *state = (find_state_t *)context;
    bk_gui_rect_t content;
    bk_gui_rect_t list;
    bk_gui_rect_t status;
    uint32_t visible;
    if (!state || !surface ||
        !bk_gui_window_content_rect(state->window, &content)) return;
    find_layout(state, &list, &status);
    bk_gui_surface_fill_rect(surface, content, ST_FACE);
    bk_gui_surface_draw_text(surface, content.x + 10, content.y + 17,
                             "Nombre:", ST_TEXT, 0, false);
    bk_gui_surface_draw_text(surface, content.x + 10, content.y + 45,
                             "Buscar en:", ST_TEXT, 0, false);
    st_draw_panel(surface, list, ST_PANEL);

    visible = (uint32_t)((list.h - 8) / 20);
    if (!visible) visible = 1;
    for (uint32_t row = 0; row < visible; row++) {
        uint32_t index = state->scroll + row;
        bk_gui_rect_t item = {list.x + 4, list.y + 4 + (int)row * 20,
                              list.w - 8, 19};
        char line[320];
        char size[20];
        if (index >= state->result_count) break;
        if ((int32_t)index == state->selected)
            bk_gui_surface_fill_rect(surface, item, ST_SELECT);
        line[0] = '\0';
        st_append(line, sizeof(line), state->results[index].directory ?
                  "[DIR] " : "      ");
        st_append(line, sizeof(line), state->results[index].path);
        if (!state->results[index].directory) {
            st_append(line, sizeof(line), "  (");
            st_u32(size, sizeof(size), state->results[index].size);
            st_append(line, sizeof(line), size);
            st_append(line, sizeof(line), " bytes)");
        }
        bk_gui_surface_draw_text(surface, item.x + 4, item.y + 6, line,
            (int32_t)index == state->selected ? ST_SELECT_TXT : ST_TEXT,
            0, false);
    }
    bk_gui_surface_draw_text(surface, status.x, status.y + 4,
                             state->status, state->searching ? ST_BLUE : ST_MUTED,
                             0, false);
}

static bool find_event(bk_gui_window_t *window UNUSED,
                       const bk_gui_event_t *event, void *context) {
    find_state_t *state = (find_state_t *)context;
    bk_gui_rect_t list;
    bk_gui_rect_t status;
    uint32_t visible;
    if (!state || !event) return false;
    find_layout(state, &list, &status);
    visible = (uint32_t)((list.h - 8) / 20);
    if (!visible) visible = 1;
    if (event->type == BK_GUI_EVENT_MOUSE_UP &&
        st_rect_contains(list, event->x, event->y)) {
        int row = (event->y - (list.y + 4)) / 20;
        uint32_t index = state->scroll + (uint32_t)(row < 0 ? 0 : row);
        if (index < state->result_count) {
            state->selected = (int32_t)index;
            bk_gui_window_invalidate(state->window);
            return true;
        }
    } else if (event->type == BK_GUI_EVENT_MOUSE_WHEEL &&
               st_rect_contains(list, event->x, event->y)) {
        if (event->dy < 0 && state->scroll + visible < state->result_count)
            state->scroll++;
        else if (event->dy > 0 && state->scroll)
            state->scroll--;
        bk_gui_window_invalidate(state->window);
        return true;
    } else if (event->type == BK_GUI_EVENT_KEY &&
               (uint8_t)event->key == BK_KEY_ENTER) {
        if (bk_gui_widget_is_focused(state->window, state->query_box) ||
            bk_gui_widget_is_focused(state->window, state->path_box))
            state->pending_search = true;
        else
            find_open_selected(state, false);
        return true;
    }
    (void)status;
    return false;
}

void bleskernos_program_main(bk_gui_desktop_t *desktop) {
    find_state_t *state;
    if (bk_sys_api_version() < FIND_MIN_API) return;
    if (!desktop) desktop = bk_gui_desktop();
    if (!desktop) return;
    state = (find_state_t *)bk_sys_alloc(sizeof(*state));
    if (!state) return;
    st_zero(state, sizeof(*state));
    state->desktop = desktop;
    state->selected = -1;
    st_copy(state->status, sizeof(state->status),
            "Escriba un nombre parcial o * para todos.");
    state->window = bk_gui_create_window(desktop, 75, 55, 650, 450,
                                         "Find Files");
    if (!state->window) {
        bk_sys_free(state);
        return;
    }
    g_find_state = state;
    state->query_box = bk_gui_create_textbox(desktop, state->window,
        (bk_gui_rect_t){0, 0, 100, 22}, "", 63, find_widget_callback);
    state->path_box = bk_gui_create_textbox(desktop, state->window,
        (bk_gui_rect_t){0, 0, 100, 22}, "/", BK_PATH_MAX - 1,
        find_widget_callback);
    state->search_button = bk_gui_create_button(desktop, state->window,
        (bk_gui_rect_t){0, 0, 70, 22}, "Buscar", find_widget_callback);
    state->open_button = bk_gui_create_button(desktop, state->window,
        (bk_gui_rect_t){0, 0, 70, 22}, "Abrir", find_widget_callback);
    state->folder_button = bk_gui_create_button(desktop, state->window,
        (bk_gui_rect_t){0, 0, 100, 22}, "Abrir carpeta", find_widget_callback);
    state->clear_button = bk_gui_create_button(desktop, state->window,
        (bk_gui_rect_t){0, 0, 70, 22}, "Limpiar", find_widget_callback);
    (void)bk_gui_widget_set_icon(state->search_button, "FileFind");
    (void)bk_gui_widget_set_icon(state->open_button, "Open");
    (void)bk_gui_widget_set_icon(state->folder_button, "FolderOpen");
    (void)bk_gui_widget_set_icon(state->clear_button, "Delete");
    state->search_id = bk_gui_widget_id(state->search_button);
    state->open_id = bk_gui_widget_id(state->open_button);
    state->folder_id = bk_gui_widget_id(state->folder_button);
    state->clear_id = bk_gui_widget_id(state->clear_button);
    bk_gui_set_window_content(state->window, find_paint, state);
    bk_gui_set_window_event_handler(state->window, find_event, state);
    bk_gui_set_window_min_size(state->window, 540, 360);
    bk_gui_window_set_owner(state->window, bk_sys_getpid());

    while (bk_gui_window_is_open(state->window)) {
        if (state->pending_search && !state->searching)
            find_run_search(state);
        bk_sys_sleep_ms(10);
    }
    if (g_find_state == state) g_find_state = NULL;
    bk_gui_destroy_window(desktop, state->window);
    bk_sys_free(state);
}
