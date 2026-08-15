#include "include/file_dialog.h"
#include "include/api.h"
#include "include/block.h"
#include "include/graphics_resources.h"
#include "include/iso9660.h"
#include "include/keyboard.h"
#include "include/memory.h"
#include "include/mouse.h"
#include "include/pit.h"
#include "include/sound.h"
#include "include/task.h"
#include "include/usercopy.h"

#define BKFD_WINDOW_W 560
#define BKFD_WINDOW_H 330
#define BKFD_ROW_H     20
#define BKFD_SIDEBAR_W 104
#define BKFD_MAX_VOLUMES 12U

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    gui_event_queue_t events;
    gui_scrollbar_drag_t scrollbar_drag;
    vfs_dir_entry_t entries[VFS_MAX_DIR_ENTRIES];
    uint32_t entry_count;
    int selected;
    int scroll;
    int last_click;
    uint32_t last_click_tick;
    char cwd[VFS_MAX_PATH];
    char extension[16];
    char title[48];
    char status[80];
    char volumes[BKFD_MAX_VOLUMES][16];
    uint8_t volume_types[BKFD_MAX_VOLUMES];
    uint32_t volume_count;
    gui_image_t icon_computer;
    gui_image_t icon_folder;
    gui_image_t icon_file;
    gui_image_t icon_disk;
    gui_image_t icon_cdrom;
    uint32_t flags;
    bk_file_dialog_callback_t callback;
    void *callback_context;
    uint32_t callback_pid;
    bool callback_user;
    bool notified;
} bk_file_dialog_state_t;

static void bkfd_copy(char *dst, size_t size, const char *src) {
    if (!dst || !size) return;
    if (!src) src = "";
    kstrncpy(dst, src, size - 1U);
    dst[size - 1U] = '\0';
}

static char bkfd_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool bkfd_extension_matches(const char *name, const char *extension) {
    const char *dot = NULL;
    if (!name || !extension || !extension[0]) return true;
    for (const char *p = name; *p; p++) if (*p == '.') dot = p;
    if (!dot) return false;
    while (*dot && *extension) {
        if (bkfd_upper(*dot++) != bkfd_upper(*extension++)) return false;
    }
    return !*dot && !*extension;
}

static void bkfd_join(char *out, const char *directory, const char *name) {
    size_t length;
    bkfd_copy(out, VFS_MAX_PATH, directory);
    length = kstrlen(out);
    if (length > 1U && out[length - 1U] != '/' && length + 1U < VFS_MAX_PATH) {
        out[length++] = '/';
        out[length] = '\0';
    }
    if (length == 1U && out[0] == '/') length = 1U;
    kstrncpy(out + length, name, VFS_MAX_PATH - length - 1U);
    out[VFS_MAX_PATH - 1U] = '\0';
}

static void bkfd_parent(char *path) {
    size_t length;
    if (!path || path[0] != '/') return;
    length = kstrlen(path);
    while (length > 1U && path[length - 1U] == '/') path[--length] = '\0';
    while (length > 1U && path[length - 1U] != '/') length--;
    if (length <= 1U) {
        path[0] = '/';
        path[1] = '\0';
    } else {
        path[length - 1U] = '\0';
    }
}

static bool bkfd_volume_name(const char *name) {
    if (!name || !name[0]) return false;
    if (kstrcmp(name, "CDROM") == 0) return true;
    return (name[0] == 'a' && name[1] == 't' && name[2] == 'a') ||
           (name[0] == 'u' && name[1] == 's' && name[2] == 'b') ||
           (name[0] == 'f' && name[1] == 'd') ||
           (name[0] == 'h' && name[1] == 'd');
}

static void bkfd_refresh_volumes(bk_file_dialog_state_t *state) {
    vfs_dir_entry_t probe[1];
    uint32_t count;
    state->volume_count = 0U;
    /* Los volumenes FAT son rutas virtuales (/ata0p1, /usb0, etc.) y no
       aparecen necesariamente al listar '/'. Enumerarlos desde block evita
       que el selector quede encerrado en el volumen de arranque. */
    for (uint32_t i=0U;i<block_count()&&state->volume_count<BKFD_MAX_VOLUMES;i++) {
        block_device_t *device=block_at(i);
        char path[20];
        bool duplicate=false;
        if(!device||device->type==BLOCK_DEVICE_ATAPI)continue;
        path[0]='/';bkfd_copy(path+1,sizeof(path)-1U,device->name);
        count=0U;
        if(!vfs_listdir(path,probe,1U,&count))continue;
        for(uint32_t j=0U;j<state->volume_count;j++)
            if(kstrcmp(state->volumes[j],device->name)==0)duplicate=true;
        if(duplicate)continue;
        bkfd_copy(state->volumes[state->volume_count],sizeof(state->volumes[0]),device->name);
        state->volume_types[state->volume_count]=(uint8_t)device->type;
        state->volume_count++;
    }
    if(iso9660_is_mounted()&&state->volume_count<BKFD_MAX_VOLUMES){
        bkfd_copy(state->volumes[state->volume_count],sizeof(state->volumes[0]),"CDROM");
        state->volume_types[state->volume_count]=(uint8_t)BLOCK_DEVICE_ATAPI;
        state->volume_count++;
    }
}

static gui_rect_t bkfd_sidebar_rect(const bk_file_dialog_state_t *state) {
    gui_rect_t content = gui_window_content_rect(state->window);
    return (gui_rect_t){content.x + 8, content.y + 29,
                        BKFD_SIDEBAR_W, content.h - 70};
}

static gui_rect_t bkfd_list_rect(const bk_file_dialog_state_t *state) {
    gui_rect_t content = gui_window_content_rect(state->window);
    return (gui_rect_t){content.x + 8 + BKFD_SIDEBAR_W + 6,
                        content.y + 29,
                        content.w - 22 - BKFD_SIDEBAR_W,
                        content.h - 70};
}

static gui_rect_t bkfd_button_rect(const bk_file_dialog_state_t *state,
                                   int button) {
    gui_rect_t content = gui_window_content_rect(state->window);
    int y = content.y + content.h - 33;
    if (button == 0) return (gui_rect_t){content.x + 8, y, 54, 23};
    if (button == 1) return (gui_rect_t){content.x + content.w - 238, y, 88, 23};
    if (button == 2) return (gui_rect_t){content.x + content.w - 144, y, 66, 23};
    return (gui_rect_t){content.x + content.w - 72, y, 64, 23};
}

static int bkfd_visible_rows(const bk_file_dialog_state_t *state) {
    int rows = bkfd_list_rect(state).h / BKFD_ROW_H;
    return rows > 0 ? rows : 1;
}

static void bkfd_clamp_scroll(bk_file_dialog_state_t *state) {
    int maximum = (int)state->entry_count - bkfd_visible_rows(state);
    if (maximum < 0) maximum = 0;
    if (state->scroll < 0) state->scroll = 0;
    if (state->scroll > maximum) state->scroll = maximum;
}

static bool bkfd_load(bk_file_dialog_state_t *state) {
    vfs_dir_entry_t raw[VFS_MAX_DIR_ENTRIES];
    uint32_t count = 0;
    state->entry_count = 0;
    state->selected = -1;
    state->scroll = 0;
    if (!vfs_listdir(state->cwd, raw, VFS_MAX_DIR_ENTRIES, &count)) {
        bkfd_copy(state->status, sizeof(state->status), "@H13B7BCAD");
        return false;
    }
    /* Mini Files en modo lista: directorios, archivos compatibles y por
       ultimo el resto. La extension ordena, pero nunca oculta contenido. */
    for (int pass = 0; pass < 3; pass++) {
        for (uint32_t i = 0; i < count; i++) {
            bool directory = raw[i].type == VFS_NODE_DIR;
            bool compatible = !directory &&
                bkfd_extension_matches(raw[i].name, state->extension);
            if ((pass == 0 && !directory) ||
                (pass == 1 && !compatible) ||
                (pass == 2 && (directory || compatible))) continue;
            if (state->entry_count < VFS_MAX_DIR_ENTRIES)
                state->entries[state->entry_count++] = raw[i];
        }
    }
    bkfd_copy(state->status, sizeof(state->status),
              state->entry_count ? "@H94644398" : "@HC1BC9FAD");
    if (state->window) state->window->dirty = true;
    return true;
}

static void bkfd_draw_icon(gui_surface_t *surface, int x, int y,
                           bool directory, bool volume, uint32_t color,
                           const gui_image_t *resource) {
    if(resource&&resource->pixels){
        bk_gui_surface_draw_image(surface,(gui_rect_t){x,y,16,16},
                                  (gui_rect_t){x,y,16,16},resource);
        return;
    }
    if (volume) {
        gui_gfx_fill_rect(surface, (gui_rect_t){x, y + 2, 14, 10},
                          0x00909098);
        gui_gfx_draw_rect(surface, (gui_rect_t){x, y + 2, 14, 10}, color);
        gui_gfx_fill_rect(surface, (gui_rect_t){x + 2, y + 4, 10, 2},
                          0x00D8D8E0);
        gui_gfx_fill_rect(surface, (gui_rect_t){x + 10, y + 9, 2, 2},
                          0x0020C040);
    } else if (directory) {
        gui_gfx_fill_rect(surface, (gui_rect_t){x + 1, y + 2, 7, 3},
                          0x00E8B820);
        gui_gfx_fill_rect(surface, (gui_rect_t){x, y + 5, 15, 9},
                          0x00FFD448);
        gui_gfx_draw_rect(surface, (gui_rect_t){x, y + 5, 15, 9}, color);
    } else {
        gui_gfx_fill_rect(surface, (gui_rect_t){x + 2, y + 1, 11, 14},
                          0x00FFFFFF);
        gui_gfx_draw_rect(surface, (gui_rect_t){x + 2, y + 1, 11, 14}, color);
        gui_gfx_fill_rect(surface, (gui_rect_t){x + 5, y + 5, 6, 1},
                          0x00808080);
        gui_gfx_fill_rect(surface, (gui_rect_t){x + 5, y + 8, 6, 1},
                          0x00808080);
    }
}

static gui_rect_t bkfd_sidebar_item(const bk_file_dialog_state_t *state,
                                    uint32_t index) {
    gui_rect_t side = bkfd_sidebar_rect(state);
    return (gui_rect_t){side.x + 3, side.y + 5 + (int)index * 27,
                        side.w - 6, 24};
}

static void bkfd_draw_button(gui_surface_t *surface, gui_rect_t rect,
                             const char *text) {
    gui_gfx_fill_rect(surface, rect, 0x00D4D0C8);
    gui_gfx_draw_rect(surface, rect, 0x00404040);
    gui_gfx_fill_rect(surface, (gui_rect_t){rect.x + 1, rect.y + 1,
                                            rect.w - 2, 1}, 0x00FFFFFF);
    gui_font_draw_string_clipped(surface, rect.x + 8, rect.y + 8,
                                 text, 0x00101010, rect);
}

static void bkfd_paint(gui_window_t *window, gui_surface_t *surface,
                       void *context) {
    bk_file_dialog_state_t *state = (bk_file_dialog_state_t *)context;
    gui_rect_t content = gui_window_content_rect(window);
    gui_rect_t list = bkfd_list_rect(state);
    gui_rect_t sidebar = bkfd_sidebar_rect(state);
    int rows = bkfd_visible_rows(state);
    gui_scrollbar_t scrollbar;
    gui_gfx_fill_rect(surface, content, 0x00D4D0C8);
    gui_gfx_fill_rect(surface, (gui_rect_t){content.x + 8, content.y + 5,
                                            content.w - 16, 19}, 0x00FFFFFF);
    gui_gfx_draw_rect(surface, (gui_rect_t){content.x + 8, content.y + 5,
                                            content.w - 16, 19}, 0x00606060);
    gui_font_draw_string_clipped(surface, content.x + 13, content.y + 11,
                                 state->cwd, 0x00101010,
                                 (gui_rect_t){content.x + 12, content.y + 6,
                                              content.w - 24, 17});
    gui_gfx_fill_rect(surface, list, 0x00FFFFFF);
    gui_gfx_draw_rect(surface, list, 0x00505050);
    gui_gfx_fill_rect(surface, sidebar, 0x00ECE9E2);
    gui_gfx_draw_rect(surface, sidebar, 0x00707070);
    for (uint32_t i = 0U; i <= state->volume_count; i++) {
        gui_rect_t item = bkfd_sidebar_item(state, i);
        char path[20];
        bool active;
        if (i == 0U) bkfd_copy(path, sizeof(path), "/");
        else {
            path[0] = '/';
            bkfd_copy(path + 1, sizeof(path) - 1U, state->volumes[i - 1U]);
        }
        active = i == 0U ? kstrcmp(state->cwd, "/") == 0
                         : kstrncmp(state->cwd, path, kstrlen(path)) == 0;
        if (active) gui_gfx_fill_rect(surface, item, 0x00C8DCF0);
        const gui_image_t *volume_icon=i==0U?&state->icon_computer:
            (state->volume_types[i-1U]==BLOCK_DEVICE_ATAPI?&state->icon_cdrom:&state->icon_disk);
        bkfd_draw_icon(surface, item.x + 5, item.y + 4, false, true,
                       0x00404040,volume_icon);
        gui_font_draw_string_clipped(surface, item.x + 24, item.y + 8,
            i == 0U ? "Equipo" : state->volumes[i - 1U], 0x00101010,
            (gui_rect_t){item.x + 23, item.y, item.w - 24, item.h});
    }
    for (int row = 0; row < rows; row++) {
        int index = state->scroll + row;
        gui_rect_t row_rect = {list.x + 1, list.y + 1 + row * BKFD_ROW_H,
                               list.w - GUI_SCROLLBAR_SIZE - 2, BKFD_ROW_H};
        char label[VFS_MAX_NAME + 4];
        bool volume;
        if (index >= (int)state->entry_count) break;
        if (index == state->selected)
            gui_gfx_fill_rect(surface, row_rect, 0x000080C0);
        volume = state->entries[index].type == VFS_NODE_DIR &&
                 bkfd_volume_name(state->entries[index].name);
        bkfd_copy(label, sizeof(label), state->entries[index].name);
        bkfd_draw_icon(surface, row_rect.x + 4, row_rect.y + 2,
                       state->entries[index].type == VFS_NODE_DIR, volume,
                       index == state->selected ? 0x00FFFFFF : 0x00404040,
                       state->entries[index].type==VFS_NODE_DIR?
                       &state->icon_folder:&state->icon_file);
        gui_font_draw_string_clipped(surface, row_rect.x + 24, row_rect.y + 6,
                                     label,
                                     index == state->selected ? 0x00FFFFFF
                                                              : 0x00101010,
                                     row_rect);
    }
    gui_scrollbar_init_vertical(&scrollbar,
        (gui_rect_t){list.x + list.w - GUI_SCROLLBAR_SIZE, list.y,
                     GUI_SCROLLBAR_SIZE, list.h},
        (uint32_t)state->scroll, (uint32_t)rows, state->entry_count);
    gui_scrollbar_paint_vertical(surface, &scrollbar);
    gui_font_draw_string_clipped(surface, content.x + 72,
                                 content.y + content.h - 25,
                                 state->status, 0x00404040,
                                 (gui_rect_t){content.x + 70,
                                              content.y + content.h - 31,
                                              content.w - 315, 23});
    bkfd_draw_button(surface, bkfd_button_rect(state, 0), "@H362E39D2");
    if (state->flags & BK_FILE_DIALOG_PREVIEW_AUDIO)
        bkfd_draw_button(surface, bkfd_button_rect(state, 1), "Reproducir");
    bkfd_draw_button(surface, bkfd_button_rect(state, 2), "@H5A216C13");
    bkfd_draw_button(surface, bkfd_button_rect(state, 3), "@HEA3D2B54");
}

static int bkfd_hit_row(bk_file_dialog_state_t *state, int x, int y) {
    gui_rect_t list = bkfd_list_rect(state);
    int row;
    if (!gui_rect_contains(list, x, y) ||
        x >= list.x + list.w - GUI_SCROLLBAR_SIZE) return -1;
    row = (y - list.y - 1) / BKFD_ROW_H;
    if (row < 0 || row >= bkfd_visible_rows(state)) return -1;
    row += state->scroll;
    return row < (int)state->entry_count ? row : -1;
}

static void bkfd_selected_path(bk_file_dialog_state_t *state, char *path) {
    if (!state || state->selected < 0 ||
        state->selected >= (int)state->entry_count) {
        if (path) path[0] = '\0';
        return;
    }
    bkfd_join(path, state->cwd, state->entries[state->selected].name);
}

static bool bkfd_notify(bk_file_dialog_state_t *state, const char *path) {
    uint32_t arguments[2];
    char *user_path = NULL;
    uint32_t length = 0U;

    if (!state || !state->callback || state->notified) return false;
    if (!state->callback_user) {
        state->callback(path, state->callback_context);
        state->notified = true;
        return true;
    }

    arguments[0] = 0U;
    arguments[1] = (uint32_t)(uintptr_t)state->callback_context;
    if (path) {
        length = (uint32_t)kstrlen(path) + 1U;
        if (!length || length > VFS_MAX_PATH) return false;
        user_path = (char *)kmalloc(length);
        if (!user_path) return false;
        /* The dialog worker is a short-lived kernel process. Keep the path
         * outside its automatic process cleanup; the upcall owns and frees it
         * after the application callback returns. */
        (void)mm_set_allocation_owner(user_path, 0U);
        kmemcpy(user_path, path, length);
        arguments[0] = (uint32_t)(uintptr_t)user_path;
        if (!task_queue_user_upcall_owned(
                state->callback_pid,
                (uint32_t)(uintptr_t)state->callback,
                arguments, 2U, user_path)) {
            kfree(user_path);
            return false;
        }
    } else if (!task_queue_user_upcall(
                   state->callback_pid,
                   (uint32_t)(uintptr_t)state->callback,
                   arguments, 2U, NULL, 0U, -6)) {
        return false;
    }
    state->notified = true;
    return true;
}

static void bkfd_activate(bk_file_dialog_state_t *state, bool choose) {
    char path[VFS_MAX_PATH];
    vfs_dir_entry_t *entry;
    if (!state || state->selected < 0 ||
        state->selected >= (int)state->entry_count) return;
    entry = &state->entries[state->selected];
    bkfd_selected_path(state, path);
    if (entry->type == VFS_NODE_DIR) {
        bkfd_copy(state->cwd, sizeof(state->cwd), path);
        (void)bkfd_load(state);
        return;
    }
    if (!choose) {
        if ((state->flags & BK_FILE_DIALOG_PREVIEW_AUDIO) && sound_play_file(path))
            bkfd_copy(state->status, sizeof(state->status), "@HCF1AFD02");
        else
            bkfd_copy(state->status, sizeof(state->status), "@H4EFE6C5F");
        state->window->dirty = true;
        return;
    }
    if (!bkfd_notify(state, path)) {
        bkfd_copy(state->status, sizeof(state->status),
                  "No se pudo entregar el archivo seleccionado");
        state->window->dirty = true;
        return;
    }
    gui_window_close(state->window);
}

static bool bkfd_event(gui_window_t *window UNUSED,
                       const gui_event_t *event, void *context) {
    bk_file_dialog_state_t *state = (bk_file_dialog_state_t *)context;
    if (!state || !event) return false;
    return gui_event_queue_push(&state->events, event);
}

static void bkfd_process_event(bk_file_dialog_state_t *state,
                               const gui_event_t *event) {
    int hit;
    gui_rect_t list = bkfd_list_rect(state);
    gui_scrollbar_t scrollbar;
    uint32_t new_scroll;
    gui_scrollbar_init_vertical(&scrollbar,
        (gui_rect_t){list.x + list.w - GUI_SCROLLBAR_SIZE, list.y,
                     GUI_SCROLLBAR_SIZE, list.h},
        (uint32_t)state->scroll,
        (uint32_t)bkfd_visible_rows(state), state->entry_count);
    if ((state->scrollbar_drag.active ||
         gui_rect_contains(list, event->x, event->y)) &&
        gui_scrollbar_handle_event_vertical(&scrollbar,
            &state->scrollbar_drag, event, 1U, &new_scroll)) {
        state->scroll = (int)new_scroll;
        bkfd_clamp_scroll(state);
        state->window->dirty = true;
        return;
    }
    if (event->type == GUI_EVENT_KEY) {
        if (event->key == 27) gui_window_close(state->window);
        else if (event->key == '\b') {
            bkfd_parent(state->cwd);
            (void)bkfd_load(state);
        } else if (event->key == '\n') bkfd_activate(state, true);
        else if ((uint8_t)event->key == KEY_UP && state->selected > 0)
            state->selected--;
        else if ((uint8_t)event->key == KEY_DOWN &&
                 state->selected + 1 < (int)state->entry_count)
            state->selected++;
        if (state->selected >= 0) {
            int visible = bkfd_visible_rows(state);
            if (state->selected < state->scroll)
                state->scroll = state->selected;
            if (state->selected >= state->scroll + visible)
                state->scroll = state->selected - visible + 1;
            bkfd_clamp_scroll(state);
        }
        state->window->dirty = true;
        return;
    }
    if (event->type != GUI_EVENT_MOUSE_UP ||
        event->button != MOUSE_LEFT_BUTTON) return;
    for (uint32_t i = 0U; i <= state->volume_count; i++) {
        if (!gui_rect_contains(bkfd_sidebar_item(state, i),
                               event->x, event->y)) continue;
        if (i == 0U) bkfd_copy(state->cwd, sizeof(state->cwd), "/");
        else {
            state->cwd[0] = '/';
            bkfd_copy(state->cwd + 1, sizeof(state->cwd) - 1U,
                      state->volumes[i - 1U]);
        }
        (void)bkfd_load(state);
        return;
    }
    if (gui_rect_contains(bkfd_button_rect(state, 0), event->x, event->y)) {
        bkfd_parent(state->cwd);
        (void)bkfd_load(state);
        return;
    }
    if ((state->flags & BK_FILE_DIALOG_PREVIEW_AUDIO) &&
        gui_rect_contains(bkfd_button_rect(state, 1), event->x, event->y)) {
        bkfd_activate(state, false);
        return;
    }
    if (gui_rect_contains(bkfd_button_rect(state, 2), event->x, event->y)) {
        bkfd_activate(state, true);
        return;
    }
    if (gui_rect_contains(bkfd_button_rect(state, 3), event->x, event->y)) {
        gui_window_close(state->window);
        return;
    }
    hit = bkfd_hit_row(state, event->x, event->y);
    if (hit < 0) return;
    state->selected = hit;
    if (state->last_click == hit &&
        pit_get_ticks() - state->last_click_tick < pit_get_frequency_hz() / 2U) {
        state->last_click = -1;
        bkfd_activate(state, true);
    } else {
        state->last_click = hit;
        state->last_click_tick = pit_get_ticks();
        state->window->dirty = true;
    }
}

static void bkfd_task(void *argument) {
    bk_file_dialog_state_t *state = (bk_file_dialog_state_t *)argument;
    gui_event_t event;
    if (!state || !state->desktop) goto done;
    gui_event_queue_reset(&state->events);
    state->window = gui_desktop_create_window(state->desktop, 105, 62,
                                               BKFD_WINDOW_W, BKFD_WINDOW_H,
                                               state->title);
    if (!state->window) goto done;
    gui_window_set_content(state->window, bkfd_paint, state);
    gui_window_set_event_handler(state->window, bkfd_event, state);
    gui_window_set_min_size(state->window, 330, 220);
    state->window->owner_pid = task_current_pid();
    task_bind_window(state->window);
    (void)bk_graphics_icon_load("Computer",&state->icon_computer);
    (void)bk_graphics_icon_load("Folder",&state->icon_folder);
    (void)bk_graphics_icon_load("FileText",&state->icon_file);
    (void)bk_graphics_icon_load("ReaderDisket",&state->icon_disk);
    (void)bk_graphics_icon_load("CdExe",&state->icon_cdrom);
    bkfd_refresh_volumes(state);
    (void)bkfd_load(state);
    while (!task_exit_requested() && state->window->listed) {
        while (gui_event_queue_pop(&state->events, &event))
            bkfd_process_event(state, &event);
        task_sleep(1);
    }
    gui_desktop_remove_window(state->desktop, state->window);
    gui_window_destroy(state->window);
    task_bind_window(NULL);
done:
    if (state && !state->notified && state->callback &&
        !bkfd_notify(state, NULL))
        kprintf("[FILEDIALOG] no se pudo entregar cancelacion pid=%u\n",
                state->callback_pid);
    if (state) {
        gui_image_free(&state->icon_computer);gui_image_free(&state->icon_folder);
        gui_image_free(&state->icon_file);gui_image_free(&state->icon_disk);
        gui_image_free(&state->icon_cdrom);kfree(state);
    }
    task_exit();
}

static bool bk_file_dialog_open_internal(
                         gui_desktop_t *desktop, const char *title,
                         const char *initial_path, const char *extension,
                         uint32_t flags, bk_file_dialog_callback_t callback,
                         void *context, bool callback_user,
                         uint32_t callback_pid) {
    bk_file_dialog_state_t *state;
    gui_desktop_t *real_desktop = gui_get_desktop();
    (void)desktop;
    if (!real_desktop || !callback ||
        (callback_user && !callback_pid)) return false;
    state = (bk_file_dialog_state_t *)kzalloc(sizeof(*state));
    if (!state) return false;
    /* Native applications may hold an opaque user-side desktop proxy. The
     * worker is Ring 0 and must always use the real compositor desktop. */
    state->desktop = real_desktop;
    state->selected = -1;
    state->last_click = -1;
    state->flags = flags;
    state->callback = callback;
    state->callback_context = context;
    state->callback_user = callback_user;
    state->callback_pid = callback_pid;
    bkfd_copy(state->title, sizeof(state->title),
              title && title[0] ? title : "@H270B4CA3");
    bkfd_copy(state->cwd, sizeof(state->cwd),
              initial_path && initial_path[0] ? initial_path : "/");
    if (state->cwd[0] != '/') bkfd_copy(state->cwd, sizeof(state->cwd), "/");
    bkfd_copy(state->extension, sizeof(state->extension), extension);
    /* This worker executes kernel GUI/VFS code. task_create() inherits the
     * caller's privilege and therefore tried to launch bkfd_task as Ring 3
     * when invoked through the public API. Always create a kernel worker and
     * return native callbacks through the normal Ring-3 upcall bridge. */
    if (task_create_kernel("file-dialog", bkfd_task, state) < 0) {
        kfree(state);
        return false;
    }
    return true;
}

bool bk_file_dialog_open(gui_desktop_t *desktop, const char *title,
                         const char *initial_path, const char *extension,
                         uint32_t flags, bk_file_dialog_callback_t callback,
                         void *context) {
    bool user = task_current_is_user();
    if (user && !user_access_ok((const void *)(uintptr_t)callback,
                                1U, false)) {
        kprintf("[FILEDIALOG] callback Ring 3 no accesible: %x pid=%u\n",
                (uint32_t)(uintptr_t)callback, task_current_pid());
        return false;
    }
    return bk_file_dialog_open_internal(desktop, title, initial_path,
                                        extension, flags, callback, context,
                                        user, user ? task_current_pid() : 0U);
}

bool bk_file_dialog_open_kernel(gui_desktop_t *desktop, const char *title,
                                const char *initial_path,
                                const char *extension, uint32_t flags,
                                bk_file_dialog_callback_t callback,
                                void *context) {
    return bk_file_dialog_open_internal(desktop, title, initial_path,
                                        extension, flags, callback, context,
                                        false, 0U);
}
