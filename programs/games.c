#include "../kernel/include/api.h"

#define C4_COLS 7
#define C4_ROWS 6
typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    uint8_t board[C4_ROWS][C4_COLS];
    uint8_t turn, winner;
    uint32_t reset_id;
} games_state_t;
static games_state_t *g_games;

static void c4_reset(games_state_t *st) {
    bk_runtime_memset(st->board, 0, sizeof(st->board));
    st->turn = 1;
    st->winner = 0;
    if (st->window) st->window->dirty = true;
}
static bool c4_win(games_state_t *st, int r, int c) {
    const int dr[4] = {0, 1, 1, 1}, dc[4] = {1, 0, 1, -1};
    uint8_t p = st->board[r][c];
    for (int d = 0; d < 4; d++) {
        int count = 1;
        for (int sign = -1; sign <= 1; sign += 2)
            for (int n = 1; n < 4; n++) {
                int rr = r + dr[d] * n * sign, cc = c + dc[d] * n * sign;
                if (rr < 0 || rr >= C4_ROWS || cc < 0 || cc >= C4_COLS ||
                    st->board[rr][cc] != p) break;
                count++;
            }
        if (count >= 4) return true;
    }
    return false;
}
static void c4_drop(games_state_t *st, int col) {
    if (st->winner || col < 0 || col >= C4_COLS) return;
    for (int r = C4_ROWS - 1; r >= 0; r--) if (!st->board[r][col]) {
        st->board[r][col] = st->turn;
        if (c4_win(st, r, col)) st->winner = st->turn;
        else st->turn = st->turn == 1 ? 2 : 1;
        st->window->dirty = true;
        return;
    }
}
static void games_reset(gui_window_t *window, uint32_t id UNUSED) {
    games_state_t *st = window ? (games_state_t *)window->content_context : NULL;
    if (st) c4_reset(st);
}
static void games_draw(gui_window_t *window UNUSED, gui_surface_t *s, void *ctx) {
    games_state_t *st = (games_state_t *)ctx;
    int x = st->window->bounds.x + 25, y = st->window->bounds.y + 58;
    const char *msg = st->winner == 1 ? "Gana rojo!" :
                      st->winner == 2 ? "Gana amarillo!" :
                      st->turn == 1 ? "Turno: rojo" : "Turno: amarillo";
    bk_gui_font_draw_string(s, x, st->window->bounds.y + 44, msg,
                         0x00101010, 0, false);
    bk_gui_gfx_fill_rect(s, (gui_rect_t){x, y, 7 * 34, 6 * 30}, 0x001850B0);
    for (int r = 0; r < C4_ROWS; r++) for (int c = 0; c < C4_COLS; c++) {
        uint32_t color = st->board[r][c] == 1 ? 0x00E03030 :
                         st->board[r][c] == 2 ? 0x00F0D030 : 0x00E8E8E0;
        bk_gui_gfx_fill_rounded_rect(s,
            (gui_rect_t){x + c * 34 + 5, y + r * 30 + 3, 24, 24}, 11, color);
    }
}
static bool games_event(gui_window_t *window UNUSED, const gui_event_t *e,
                        void *ctx) {
    games_state_t *st = (games_state_t *)ctx;
    int x = st->window->bounds.x + 25, y = st->window->bounds.y + 58;
    if (e->type == GUI_EVENT_MOUSE_DOWN && e->x >= x && e->x < x + 238 &&
        e->y >= y && e->y < y + 180) {
        c4_drop(st, (e->x - x) / 34);
        return true;
    }
    if (e->type == GUI_EVENT_KEY && e->key >= '1' && e->key <= '7') {
        c4_drop(st, e->key - '1');
        return true;
    }
    return false;
}
static void games_cleanup(games_state_t *st) {
    if (!st) return;
    if (st->window) {
        bk_gui_desktop_remove_window(st->desktop, st->window);
        bk_gui_window_destroy_raw(st->window);
        bk_proc_bind_window(NULL);
    }
    if (g_games == st) g_games = NULL;
    bk_sys_free(st);
}
static void games_main(void *arg) {
    games_state_t *st = (games_state_t *)arg;
    c4_reset(st);
    st->window = bk_gui_create_window(st->desktop, 120, 40, 290, 280,
                                           "Juegos - Conecta 4");
    if (st->window) {
        (void)bk_about_attach(st->window, st->desktop, &(bk_about_info_t){
            "Juegos", "Version 1.0", "Centro de juegos de BlesKernOS.",
            "Bles.INC (C) 2026", "/ICONS/OBJECT.BMP"});
        bk_gui_set_window_content(st->window, games_draw, st);
        bk_gui_set_window_event_handler(st->window, games_event, st);
        st->window->owner_pid = bk_sys_getpid();
        bk_proc_bind_window(st->window);
        gui_widget_t *b = bk_gui_widget_create(st->desktop, st->window,
            GUI_WIDGET_BUTTON, (gui_rect_t){185, 4, 82, 22}, "Reiniciar",
            games_reset);
        if (b) st->reset_id = b->id;
    }
    while (!bk_proc_exit_requested() && st->window && st->window->listed) bk_sys_sleep_ticks(4);
    games_cleanup(st);
    bk_proc_exit();
}
void games_open_from_desktop(gui_desktop_t *desktop) {
    games_state_t *st;
    if (!desktop) return;
    st = (games_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;
    st->desktop = desktop; g_games = st;
    if (bk_proc_spawn_thread("games", games_main, st) < 0) games_cleanup(st);
}
void games_install(gui_desktop_t *desktop UNUSED) {}

void bleskernos_program_main(gui_desktop_t *desktop) {
    games_open_from_desktop(desktop);
}
