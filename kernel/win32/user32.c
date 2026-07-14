#include "win32.h"
#include "resources.h"
#include "../include/pe_loader.h"
#include "../../gui/gui.h"
#include "../include/task.h"
#include "../include/memory.h"
#include "../include/pit.h"
#include "../stdio.h"
#include "../stdarg.h"

bool bk_sound_tone(uint32_t frequency_hz, uint32_t duration_ms);

#define WIN32_IDOK 1
#define WIN32_IDCANCEL 2
#define WM_INITDIALOG 0x0110U
#define BM_GETCHECK 0x00F0U
#define BM_SETCHECK 0x00F1U
#define EM_GETSEL 0x00B0U
#define EM_SETSEL 0x00B1U
#define EM_GETRECT 0x00B2U
#define EM_SETRECT 0x00B3U
#define EM_SETRECTNP 0x00B4U
#define EM_SCROLL 0x00B5U
#define EM_LINESCROLL 0x00B6U
#define EM_SCROLLCARET 0x00B7U
#define EM_GETMODIFY 0x00B8U
#define EM_SETMODIFY 0x00B9U
#define EM_GETLINECOUNT 0x00BAU
#define EM_LINEINDEX 0x00BBU
#define EM_SETHANDLE 0x00BCU
#define EM_GETHANDLE 0x00BDU
#define EM_GETTHUMB 0x00BEU
#define EM_LINELENGTH 0x00C1U
#define EM_REPLACESEL 0x00C2U
#define EM_GETLINE 0x00C4U
#define EM_SETLIMITTEXT 0x00C5U
#define EM_CANUNDO 0x00C6U
#define EM_UNDO 0x00C7U
#define EM_FMTLINES 0x00C8U
#define EM_LINEFROMCHAR 0x00C9U
#define EM_EMPTYUNDOBUFFER 0x00CDU
#define EM_GETFIRSTVISIBLELINE 0x00CEU
#define EM_SETREADONLY 0x00CFU
#define EM_SETMARGINS 0x00D3U
#define EM_GETMARGINS 0x00D4U
#define EM_GETLIMITTEXT 0x00D5U
#define EM_POSFROMCHAR 0x00D6U
#define EM_CHARFROMPOS 0x00D7U
/* RichEdit 2.x messages used by Metapad. */
#define EM_EXGETSEL 0x0434U
#define EM_EXLIMITTEXT 0x0435U
#define EM_EXLINEFROMCHAR 0x0436U
#define EM_EXSETSEL 0x0437U
#define EM_FINDTEXT 0x0438U
#define EM_FORMATRANGE 0x0439U
#define EM_GETCHARFORMAT 0x043AU
#define EM_GETEVENTMASK 0x043BU
#define EM_GETOLEINTERFACE 0x043CU
#define EM_GETPARAFORMAT 0x043DU
#define EM_GETSELTEXT 0x043EU
#define EM_HIDESELECTION 0x043FU
#define EM_PASTESPECIAL 0x0440U
#define EM_REQUESTRESIZE 0x0441U
#define EM_SELECTIONTYPE 0x0442U
#define EM_SETBKGNDCOLOR 0x0443U
#define EM_SETCHARFORMAT 0x0444U
#define EM_SETEVENTMASK 0x0445U
#define EM_SETOLECALLBACK 0x0446U
#define EM_SETPARAFORMAT 0x0447U
#define EM_SETTARGETDEVICE 0x0448U
#define EM_STREAMIN 0x0449U
#define EM_STREAMOUT 0x044AU
#define EM_GETTEXTRANGE 0x044BU
#define EM_FINDWORDBREAK 0x044CU
#define EM_SETOPTIONS 0x044DU
#define EM_GETOPTIONS 0x044EU
#define EM_FINDTEXTEX 0x044FU
#define EM_SETUNDOLIMIT 0x0452U
#define EM_REDO 0x0454U
#define EM_CANREDO 0x0455U
#define EM_STOPGROUPTYPING 0x0458U
#define EM_SETTEXTMODE 0x0459U
#define EM_GETTEXTMODE 0x045AU
#define EM_AUTOURLDETECT 0x045BU
#define EM_GETAUTOURLDETECT 0x045CU
#define EM_GETTEXTEX 0x045EU
#define EM_GETTEXTLENGTHEX 0x045FU
#define EM_SHOWSCROLLBAR 0x0460U
#define EM_SETTEXTEX 0x0461U
#define FR_DOWN 0x00000001U
#define FR_WHOLEWORD 0x00000002U
#define FR_MATCHCASE 0x00000004U
#define ECOOP_SET 0x0001U
#define ECOOP_OR 0x0002U
#define ECOOP_AND 0x0003U
#define ECOOP_XOR 0x0004U
#define SEL_EMPTY 0x0000U
#define SEL_TEXT 0x0001U
#define SF_TEXT 0x0001U
#define WM_CUT 0x0300U
#define WM_COPY 0x0301U
#define WM_PASTE 0x0302U
#define WM_CLEAR 0x0303U
#define WM_UNDO 0x0304U
#define WM_SETFONT 0x0030U
#define WM_GETFONT 0x0031U
#define WM_SETFOCUS 0x0007U
#define WM_KILLFOCUS 0x0008U
#define EN_UPDATE 0x0400U
#define EN_CHANGE 0x0300U
#define EN_MAXTEXT 0x0501U
#define ES_MULTILINE 0x00000004U
#define ES_AUTOHSCROLL 0x00000080U
#define ES_AUTOVSCROLL 0x00000040U
#define ES_NOHIDESEL 0x00000100U
#define ES_READONLY 0x00000800U
#define ES_WANTRETURN 0x00001000U
#define ES_NUMBER 0x00002000U
#define WS_HSCROLL 0x00100000U
#define WS_VSCROLL 0x00200000U
#define WIN32_EDIT_DEFAULT_LIMIT 1048576U
#define WIN32_EDIT_LINE_HEIGHT 14
#define BST_UNCHECKED 0U
#define BST_CHECKED 1U
#define BS_TYPEMASK 0x0000000FU
#define BS_CHECKBOX 0x00000002U
#define BS_AUTOCHECKBOX 0x00000003U
#define BS_RADIOBUTTON 0x00000004U
#define BS_AUTORADIOBUTTON 0x00000009U
#define WM_INITMENU 0x0116U
#define WM_INITMENUPOPUP 0x0117U
#define DS_SETFONT 0x00000040U
#define MF_POPUP 0x0010U
#define MF_STRING 0x0000U
#define MF_BYCOMMAND 0x0000U
#define MF_ENABLED 0x0000U
#define MF_REMOVE 0x1000U
#define MF_DELETE 0x0200U
#define MF_END 0x0080U
#define MF_SEPARATOR 0x0800U
#define MF_BYPOSITION 0x0400U
#define MF_CHECKED 0x0008U
#define MF_UNCHECKED 0x0000U
#define MF_DISABLED 0x0002U
#define MF_GRAYED 0x0001U
#define MIIM_STATE 0x00000001U
#define MIIM_ID 0x00000002U
#define MIIM_SUBMENU 0x00000004U
#define MIIM_TYPE 0x00000010U
#define MIIM_STRING 0x00000040U
#define MIIM_FTYPE 0x00000100U
#define MFT_SEPARATOR 0x00000800U
#define MFS_CHECKED 0x00000008U
#define MFS_DISABLED 0x00000003U
#define VK_SHIFT 0x10U
#define VK_CONTROL 0x11U
#define VK_MENU 0x12U
#define VK_CAPITAL 0x14U
#define CF_TEXT 1U
#define CF_UNICODETEXT 13U
#define WIN32_CLIPBOARD_SLOTS 8U
#define FVIRTKEY 0x01U
#define FNOINVERT 0x02U
#define FSHIFT 0x04U
#define FCONTROL 0x08U
#define FALT 0x10U
#define ACCEL_END 0x80U
#define VK_TAB 0x09U
#define VK_RETURN 0x0DU
#define VK_ESCAPE 0x1BU
#define VK_HOME 0x24U
#define VK_LEFT 0x25U
#define VK_UP 0x26U
#define VK_RIGHT 0x27U
#define VK_DOWN 0x28U
#define VK_END 0x23U
#define VK_DELETE 0x2EU
#define VK_BACK 0x08U
#define WIN32_MAX_WINDOWS 128U
#define WIN32_MAX_MENUS 32U
#define WIN32_MAX_MENU_ITEMS 64U
#define WIN32_MESSAGE_QUEUE 128U
#define WM_CREATE 0x0001U
#define WM_DESTROY 0x0002U
#define WM_MOVE 0x0003U
#define WM_SIZE 0x0005U
#define WM_NCCREATE 0x0081U
#define WM_GETDLGCODE 0x0087U
#define DLGC_WANTARROWS 0x0001U
#define DLGC_WANTTAB 0x0002U
#define DLGC_WANTALLKEYS 0x0004U
#define DLGC_WANTCHARS 0x0080U
#define WM_CLOSE 0x0010U
#define WM_QUIT 0x0012U
#define WM_KEYDOWN 0x0100U
#define WM_CHAR 0x0102U
#define WM_COMMAND 0x0111U
#define WM_PAINT 0x000FU
#define WM_TIMER 0x0113U
#define WM_SETTEXT 0x000CU
#define WM_GETTEXT 0x000DU
#define WM_GETTEXTLENGTH 0x000EU
#define SB_SETTEXTA 0x0401U
#define SB_GETTEXTA 0x0402U
#define SB_GETTEXTLENGTHA 0x0403U
#define SB_SETPARTS 0x0404U
#define SB_GETPARTS 0x0406U
#define SB_SETMINHEIGHT 0x0408U
#define TB_ENABLEBUTTON 0x0401U
#define TB_CHECKBUTTON 0x0402U
#define TB_HIDEBUTTON 0x0404U
#define TB_BUTTONCOUNT 0x0418U
#define TB_GETITEMRECT 0x041DU
#define TB_SETBUTTONSIZE 0x041FU
#define TB_SETBITMAPSIZE 0x0420U
#define TB_AUTOSIZE 0x0421U
#define SW_HIDE 0
#define WS_CHILD 0x40000000U
#define WS_DISABLED 0x08000000U
#define WS_BORDER 0x00800000U
#define WS_VISIBLE 0x10000000U
#define SWP_NOSIZE 0x0001U
#define SWP_NOMOVE 0x0002U
#define SWP_NOZORDER 0x0004U
#define SWP_NOACTIVATE 0x0010U
#define SWP_FRAMECHANGED 0x0020U
#define SWP_SHOWWINDOW 0x0040U
#define SWP_HIDEWINDOW 0x0080U
#define RDW_INVALIDATE 0x0001U
#define RDW_UPDATENOW 0x0100U
#define TPM_RETURNCMD 0x0100U
#define GCL_MENUNAME (-8)
#define GCL_HBRBACKGROUND (-10)
#define GCL_HCURSOR (-12)
#define GCL_HICON (-14)
#define GCL_HMODULE (-16)
#define GCL_CBWNDEXTRA (-18)
#define GCL_CBCLSEXTRA (-20)
#define GCL_WNDPROC (-24)
#define GCL_STYLE (-26)
#define SPI_GETWORKAREA 48U
#define SPI_GETNONCLIENTMETRICS 41U
#define SPI_GETWHEELSCROLLLINES 104U
#define GWL_WNDPROC (-4)
#define GWL_HINSTANCE (-6)
#define GWL_HWNDPARENT (-8)
#define GWL_ID (-12)
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define GWL_USERDATA (-21)
#define HWND_BASE 0x72000000U
typedef int32_t (WIN32_API *wndproc_t)(void *,uint32_t,uint32_t,int32_t);
typedef struct { uint32_t style; wndproc_t proc; int cls_extra,win_extra; void *instance,*icon,*cursor,*background; const char *menu,*name; } wndclass_a_t;
typedef struct { uint32_t style; wndproc_t proc; int cls_extra,win_extra; void *instance,*icon,*cursor,*background; const uint16_t *menu,*name; } wndclass_w_t;
typedef struct { void *hwnd; uint32_t message,wparam,lparam,time; int x,y; } winmsg_t;
typedef struct {
    void *create_params;
    void *instance;
    void *menu;
    void *parent;
    int cy, cx, y, x;
    int32_t style;
    const char *name;
    const char *class_name;
    uint32_t exstyle;
} create_struct_a_t;
typedef struct {
    bool used, destroy_sent, control, focused, pressed, enabled, visible;
    bool dialog, dialog_done;
    uint8_t kind;
    gui_window_t *native;
    wndproc_t proc;
    wndproc_t default_proc;
    void *parent;
    void *menu_handle;
    gui_rect_t bounds;
    uint32_t id, style, exstyle, check_state;
    uint32_t selection_start, selection_end;
    uint32_t edit_caret, edit_anchor, edit_length, edit_capacity, edit_limit;
    uint32_t undo_length, undo_caret, undo_anchor;
    int32_t edit_first_line, edit_hscroll;
    bool edit_readonly, edit_modified, edit_selecting, edit_format_lines;
    bool rich_edit, rich_hide_selection, rich_auto_url;
    char *edit_buffer, *undo_buffer;
    uint32_t rich_event_mask, rich_options, rich_background;
    uint32_t rich_text_mode, rich_undo_limit;
    void *font;
    int32_t scroll_min, scroll_max, scroll_page, scroll_pos;
    uint8_t status_part_count, toolbar_count;
    int16_t toolbar_button_width, toolbar_button_height;
    int32_t status_parts[8];
    char status_text[8][64];
    uint16_t toolbar_commands[32];
    uint8_t toolbar_states[32], toolbar_styles[32];
    void *instance;
    int32_t user_data, dialog_result;
    char class_name[32];
    char text[1024];
} win_window_t;
static wndclass_a_t registered_class; static char registered_name[48]; static win_window_t win_windows[WIN32_MAX_WINDOWS];
static winmsg_t message_queue[WIN32_MESSAGE_QUEUE]; static uint8_t message_head,message_tail;
typedef struct{bool used;uint8_t kind;void*hwnd;int x1,y1,x2,y2;uint32_t color;char text[96];uint32_t*pixels;int pitch,src_x,src_y;}gdi_command_t;
#define WIN32_MAX_GDI_COMMANDS 512U
static gdi_command_t gdi_commands[WIN32_MAX_GDI_COMMANDS];
#define MENU_BASE 0x76000000U
typedef struct { uint32_t id, flags; void *submenu; char text[48]; } win_menu_item_t;
typedef struct { bool used; uint8_t count; win_menu_item_t items[WIN32_MAX_MENU_ITEMS]; } win_menu_t;
static win_menu_t win_menus[WIN32_MAX_MENUS];

typedef struct { uint32_t format; void *handle; } win_clipboard_entry_t;
static win_clipboard_entry_t win_clipboard[WIN32_CLIPBOARD_SLOTS];
static uint32_t win_clipboard_open_pid;
static void *win_clipboard_open_window;
static bool win_key_shift, win_key_ctrl, win_key_alt;
static uint8_t win_mouse_buttons;
static int win_cursor_x, win_cursor_y;
static void *win_current_cursor;
static void *win_syscolor_brushes[32];

typedef struct {
    uint32_t cbSize;
    uint32_t fMask;
    uint32_t fType;
    uint32_t fState;
    uint32_t wID;
    void *hSubMenu;
    void *hbmpChecked;
    void *hbmpUnchecked;
    uint32_t dwItemData;
    char *dwTypeData;
    uint32_t cch;
    void *hbmpItem;
} win_menu_item_info_a_t;
#define ACCEL_BASE 0x77000000U
#define WIN32_MAX_ACCELS 16U
#define WIN32_MAX_ACCEL_ITEMS 32U
typedef struct { uint8_t flags; uint16_t key, command; } win_accel_item_t;
typedef struct { bool used; uint8_t count; win_accel_item_t items[WIN32_MAX_ACCEL_ITEMS]; } win_accel_t;
static win_accel_t win_accels[WIN32_MAX_ACCELS];
static win_accel_t *accel_from_handle(void *handle){uint32_t v=(uint32_t)(uintptr_t)handle;if(v<ACCEL_BASE||v>=ACCEL_BASE+WIN32_MAX_ACCELS)return NULL;v-=ACCEL_BASE;return win_accels[v].used?&win_accels[v]:NULL;}

typedef struct{bool used;void*hwnd;uint32_t id,interval,next;void*callback;}win_timer_t;static win_timer_t win_timers[8];
typedef struct { gui_window_t *window; const char *text; volatile int result; } win32_message_box_t;
static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}
static uint8_t upper_ascii(uint8_t c) {
    return c >= 'a' && c <= 'z' ? (uint8_t)(c - ('a' - 'A')) : c;
}
static bool equal_ci(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (upper_ascii((uint8_t)*a) != upper_ascii((uint8_t)*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

static char *edit_text(win_window_t *w);

typedef struct { int32_t cpMin, cpMax; } win_char_range_t;
typedef struct { win_char_range_t chrg; char *lpstrText; } win_text_range_a_t;
typedef struct {
    win_char_range_t chrg;
    const char *lpstrText;
    win_char_range_t chrgText;
} win_find_text_ex_a_t;
typedef struct {
    uint32_t cb;
    uint32_t flags;
    uint32_t codepage;
    const char *default_char;
    int *used_default_char;
} win_get_text_ex_t;
typedef struct { uint32_t flags, codepage; } win_get_text_length_ex_t;
typedef struct { uint32_t flags, codepage; } win_set_text_ex_t;
typedef uint32_t (WIN32_API *win_edit_stream_callback_t)(uint32_t cookie,
    uint8_t *buffer, int32_t bytes, int32_t *processed);
typedef struct {
    uint32_t cookie;
    uint32_t error;
    win_edit_stream_callback_t callback;
} win_edit_stream_t;
typedef struct {
    int32_t iBitmap;
    int32_t idCommand;
    uint8_t fsState;
    uint8_t fsStyle;
    uint8_t reserved[2];
    uint32_t dwData;
    int32_t iString;
} win_toolbar_button_t;

static bool is_rich_edit_class(const char *name) {
    return equal_ci(name, "RichEdit20A") || equal_ci(name, "RichEdit20W") ||
           equal_ci(name, "RICHEDIT20A") || equal_ci(name, "RICHEDIT20W") ||
           equal_ci(name, "RICHEDIT") || equal_ci(name, "RichEdit50W");
}
static bool is_edit_class(const char *name) {
    return equal_ci(name, "EDIT") || is_rich_edit_class(name);
}
static bool is_status_class(const char *name) {
    return equal_ci(name, "msctls_statusbar32") ||
           equal_ci(name, "STATUSCLASSNAME");
}
static bool ascii_word(uint8_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_';
}
static bool text_matches_at(const char *haystack, uint32_t hay_length,
                            uint32_t position, const char *needle,
                            uint32_t needle_length, uint32_t flags) {
    if (!haystack || !needle || position + needle_length > hay_length) return false;
    for (uint32_t i = 0; i < needle_length; i++) {
        uint8_t a = (uint8_t)haystack[position + i];
        uint8_t b = (uint8_t)needle[i];
        if (!(flags & FR_MATCHCASE)) { a = upper_ascii(a); b = upper_ascii(b); }
        if (a != b) return false;
    }
    if (flags & FR_WHOLEWORD) {
        if (position && ascii_word((uint8_t)haystack[position - 1U])) return false;
        if (position + needle_length < hay_length &&
            ascii_word((uint8_t)haystack[position + needle_length])) return false;
    }
    return true;
}
static int32_t edit_find_text_range(win_window_t *w, int32_t from, int32_t to,
                                    const char *needle, uint32_t flags,
                                    win_char_range_t *found) {
    uint32_t needle_length;
    if (!w || !needle || !*needle) return -1;
    needle_length = (uint32_t)kstrlen(needle);
    if (flags & FR_DOWN) {
        uint32_t start = from < 0 ? 0U : (uint32_t)from;
        uint32_t end = to < 0 || (uint32_t)to > w->edit_length
            ? w->edit_length : (uint32_t)to;
        if (start > end) { uint32_t tmp = start; start = end; end = tmp; }
        for (uint32_t p = start; p + needle_length <= end; p++)
            if (text_matches_at(edit_text(w), w->edit_length, p, needle,
                                needle_length, flags)) {
                if (found) { found->cpMin = (int32_t)p; found->cpMax = (int32_t)(p + needle_length); }
                return (int32_t)p;
            }
    } else {
        uint32_t high = from < 0 || (uint32_t)from > w->edit_length
            ? w->edit_length : (uint32_t)from;
        uint32_t low = to < 0 ? 0U : (uint32_t)to;
        if (low > high) { uint32_t tmp = low; low = high; high = tmp; }
        if (high >= needle_length) {
            uint32_t p = high - needle_length;
            for (;;) {
                if (p >= low && text_matches_at(edit_text(w), w->edit_length,
                    p, needle, needle_length, flags)) {
                    if (found) { found->cpMin = (int32_t)p; found->cpMax = (int32_t)(p + needle_length); }
                    return (int32_t)p;
                }
                if (p == 0U || p <= low) break;
                p--;
            }
        }
    }
    if (found) found->cpMin = found->cpMax = -1;
    return -1;
}

static void move_bytes(void *dst, const void *src, uint32_t count) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (!count || d == s) return;
    if (d < s) for (uint32_t i = 0; i < count; i++) d[i] = s[i];
    else for (uint32_t i = count; i > 0U; i--) d[i - 1U] = s[i - 1U];
}

static win_window_t *window_from_handle(void *hwnd);
static void queue_message(void *hwnd, uint32_t msg, uint32_t wp, int32_t lp);

static int WIN32_API win32_OpenClipboard(void *window);
static int WIN32_API win32_CloseClipboard(void);
static int WIN32_API win32_EmptyClipboard(void);
static void *WIN32_API win32_SetClipboardData(uint32_t format, void *handle);
static void *WIN32_API win32_GetClipboardData(uint32_t format);
static int WIN32_API win32_IsClipboardFormatAvailable(uint32_t format);
static int32_t WIN32_API win32_EditWndProc(void *hwnd, uint32_t msg,
                                           uint32_t wp, int32_t lp);

static char *edit_text(win_window_t *w) {
    return w && w->edit_buffer ? w->edit_buffer : (char *)"";
}

static bool edit_ensure_capacity(win_window_t *w, uint32_t needed) {
    uint32_t capacity;
    char *buffer;
    if (!w || needed == 0U) return false;
    if (needed > w->edit_limit + 1U) return false;
    if (w->edit_buffer && needed <= w->edit_capacity) return true;
    capacity = w->edit_capacity ? w->edit_capacity : 256U;
    while (capacity < needed) {
        uint32_t next = capacity < 65536U ? capacity * 2U : capacity + 65536U;
        if (next <= capacity || next > w->edit_limit + 1U)
            next = w->edit_limit + 1U;
        capacity = next;
        if (capacity < needed && capacity == w->edit_limit + 1U) return false;
    }
    buffer = (char *)kmalloc(capacity);
    if (!buffer) return false;
    if (w->edit_buffer) {
        kmemcpy(buffer, w->edit_buffer, w->edit_length + 1U);
        kfree(w->edit_buffer);
    } else buffer[0] = '\0';
    w->edit_buffer = buffer;
    w->edit_capacity = capacity;
    return true;
}

static void edit_clear_undo(win_window_t *w) {
    if (!w) return;
    if (w->undo_buffer) kfree(w->undo_buffer);
    w->undo_buffer = NULL;
    w->undo_length = 0U;
    w->undo_caret = w->undo_anchor = 0U;
}

static bool edit_save_undo(win_window_t *w) {
    char *snapshot;
    if (!w) return false;
    snapshot = (char *)kmalloc(w->edit_length + 1U);
    if (!snapshot) return false;
    kmemcpy(snapshot, edit_text(w), w->edit_length + 1U);
    edit_clear_undo(w);
    w->undo_buffer = snapshot;
    w->undo_length = w->edit_length;
    w->undo_caret = w->edit_caret;
    w->undo_anchor = w->edit_anchor;
    return true;
}

static void edit_clamp_selection(win_window_t *w) {
    if (!w) return;
    if (w->edit_caret > w->edit_length) w->edit_caret = w->edit_length;
    if (w->edit_anchor > w->edit_length) w->edit_anchor = w->edit_length;
    w->selection_start = w->edit_anchor < w->edit_caret ?
                         w->edit_anchor : w->edit_caret;
    w->selection_end = w->edit_anchor < w->edit_caret ?
                       w->edit_caret : w->edit_anchor;
}

static uint32_t edit_next_line_break(const char *text, uint32_t length,
                                     uint32_t position) {
    while (position < length && text[position] != '\r' && text[position] != '\n')
        position++;
    return position;
}

static uint32_t edit_skip_line_break(const char *text, uint32_t length,
                                     uint32_t position) {
    if (position < length && text[position] == '\r') {
        position++;
        if (position < length && text[position] == '\n') position++;
    } else if (position < length && text[position] == '\n') position++;
    return position;
}

static uint32_t edit_line_count(const win_window_t *w) {
    const char *text = w && w->edit_buffer ? w->edit_buffer : "";
    uint32_t count = 1U, position = 0U;
    while (w && position < w->edit_length) {
        position = edit_next_line_break(text, w->edit_length, position);
        if (position >= w->edit_length) break;
        count++;
        position = edit_skip_line_break(text, w->edit_length, position);
    }
    return count;
}

static uint32_t edit_line_start(const win_window_t *w, uint32_t line) {
    const char *text = edit_text((win_window_t *)w);
    uint32_t current = 0U, position = 0U;
    if (!w) return 0U;
    while (current < line && position < w->edit_length) {
        position = edit_next_line_break(text, w->edit_length, position);
        if (position >= w->edit_length) return w->edit_length;
        position = edit_skip_line_break(text, w->edit_length, position);
        current++;
    }
    return position;
}

static uint32_t edit_line_from_char(const win_window_t *w, uint32_t character) {
    const char *text = edit_text((win_window_t *)w);
    uint32_t line = 0U, position = 0U;
    if (!w) return 0U;
    if (character > w->edit_length) character = w->edit_length;
    while (position < character) {
        uint32_t end = edit_next_line_break(text, w->edit_length, position);
        if (end >= character || end >= w->edit_length) break;
        position = edit_skip_line_break(text, w->edit_length, end);
        line++;
    }
    return line;
}

static uint32_t edit_line_end(const win_window_t *w, uint32_t line) {
    uint32_t start = edit_line_start(w, line);
    return edit_next_line_break(edit_text((win_window_t *)w), w->edit_length, start);
}

static uint32_t edit_column_from_char(const win_window_t *w, uint32_t character) {
    uint32_t line = edit_line_from_char(w, character);
    uint32_t start = edit_line_start(w, line);
    return character > start ? character - start : 0U;
}

static uint32_t edit_char_from_line_column(const win_window_t *w,
                                           uint32_t line, uint32_t column) {
    uint32_t start = edit_line_start(w, line);
    uint32_t end = edit_line_end(w, line);
    uint32_t result = start + column;
    return result > end ? end : result;
}

static int edit_char_width(char c) {
    char text[2] = {c, '\0'};
    if (c == '\t') return (int)gui_font_text_width("    ");
    return (int)gui_font_text_width(text);
}

static void edit_update_scroll_info(win_window_t *w) {
    uint32_t lines;
    int visible;
    if (!w) return;
    lines = edit_line_count(w);
    visible = (w->bounds.h - 8) / WIN32_EDIT_LINE_HEIGHT;
    if (visible < 1) visible = 1;
    w->scroll_min = 0;
    w->scroll_max = lines ? (int32_t)lines - 1 : 0;
    w->scroll_page = visible;
    w->scroll_pos = w->edit_first_line;
}

static void edit_scroll_caret(win_window_t *w) {
    uint32_t line, column;
    int visible_lines, visible_columns;
    if (!w) return;
    line = edit_line_from_char(w, w->edit_caret);
    column = edit_column_from_char(w, w->edit_caret);
    visible_lines = (w->bounds.h - 8) / WIN32_EDIT_LINE_HEIGHT;
    if (w->style & WS_HSCROLL) visible_lines--;
    if (visible_lines < 1) visible_lines = 1;
    visible_columns = (w->bounds.w - 10) / 8;
    if (w->style & WS_VSCROLL) visible_columns--;
    if (visible_columns < 1) visible_columns = 1;
    if ((int32_t)line < w->edit_first_line) w->edit_first_line = (int32_t)line;
    else if ((int32_t)line >= w->edit_first_line + visible_lines)
        w->edit_first_line = (int32_t)line - visible_lines + 1;
    if ((int32_t)column < w->edit_hscroll) w->edit_hscroll = (int32_t)column;
    else if ((int32_t)column >= w->edit_hscroll + visible_columns)
        w->edit_hscroll = (int32_t)column - visible_columns + 1;
    if (w->edit_first_line < 0) w->edit_first_line = 0;
    if (w->edit_hscroll < 0) w->edit_hscroll = 0;
    edit_update_scroll_info(w);
}

static void edit_notify(win_window_t *w, uint32_t code) {
    uint32_t index;
    if (!w || !w->parent) return;
    index = (uint32_t)(w - win_windows);
    queue_message(w->parent, WM_COMMAND, (w->id & 0xFFFFU) | (code << 16),
                  (int32_t)(HWND_BASE + index));
}

static bool edit_set_text_internal(win_window_t *w, const char *text,
                                   bool mark_modified) {
    uint32_t length;
    if (!w) return false;
    if (!text) text = "";
    length = (uint32_t)kstrlen(text);
    if (length > w->edit_limit) length = w->edit_limit;
    if (!edit_ensure_capacity(w, length + 1U)) return false;
    kmemcpy(w->edit_buffer, text, length);
    w->edit_buffer[length] = '\0';
    w->edit_length = length;
    w->edit_caret = w->edit_anchor = 0U;
    edit_clamp_selection(w);
    w->edit_modified = mark_modified;
    w->edit_first_line = w->edit_hscroll = 0;
    edit_clear_undo(w);
    edit_update_scroll_info(w);
    if (w->native) { w->native->dirty = true; gui_request_paint(); }
    return true;
}

static bool edit_replace(win_window_t *w, uint32_t start, uint32_t end,
                         const char *replacement, uint32_t replacement_length,
                         bool allow_undo, bool notify) {
    uint32_t tail, available, new_length;
    if (!w || w->edit_readonly || (w->style & ES_READONLY)) return false;
    if (!replacement) replacement_length = 0U;
    if (start > w->edit_length) start = w->edit_length;
    if (end > w->edit_length) end = w->edit_length;
    if (end < start) { uint32_t swap = start; start = end; end = swap; }
    available = w->edit_limit - (w->edit_length - (end - start));
    if (replacement_length > available) {
        replacement_length = available;
        edit_notify(w, EN_MAXTEXT);
    }
    new_length = w->edit_length - (end - start) + replacement_length;
    if (!edit_ensure_capacity(w, new_length + 1U)) return false;
    if (allow_undo && !edit_save_undo(w)) return false;
    if (notify) edit_notify(w, EN_UPDATE);
    tail = w->edit_length - end;
    move_bytes(w->edit_buffer + start + replacement_length,
               w->edit_buffer + end, tail + 1U);
    if (replacement_length)
        kmemcpy(w->edit_buffer + start, replacement, replacement_length);
    w->edit_length = new_length;
    w->edit_buffer[new_length] = '\0';
    w->edit_caret = w->edit_anchor = start + replacement_length;
    edit_clamp_selection(w);
    w->edit_modified = true;
    edit_scroll_caret(w);
    if (w->native) { w->native->dirty = true; gui_request_paint(); }
    if (notify) edit_notify(w, EN_CHANGE);
    return true;
}

static void edit_set_selection(win_window_t *w, int32_t start, int32_t end) {
    if (!w) return;
    if (start == -1) {
        w->edit_anchor = w->edit_caret;
    } else {
        if (start < 0) start = 0;
        if (end == -1) end = (int32_t)w->edit_length;
        if (end < 0) end = 0;
        w->edit_anchor = (uint32_t)start;
        w->edit_caret = (uint32_t)end;
    }
    edit_clamp_selection(w);
    edit_scroll_caret(w);
    if (w->native) { w->native->dirty = true; gui_request_paint(); }
}

static void edit_move_caret(win_window_t *w, uint32_t position, bool extend) {
    if (!w) return;
    if (position > w->edit_length) position = w->edit_length;
    if (!extend) w->edit_anchor = position;
    w->edit_caret = position;
    edit_clamp_selection(w);
    edit_scroll_caret(w);
    if (w->native) { w->native->dirty = true; gui_request_paint(); }
}

static uint32_t edit_position_from_point(win_window_t *w, int x, int y) {
    uint32_t line, start, end, position;
    int current_x = 4;
    const char *text;
    if (!w) return 0U;
    line = (uint32_t)(w->edit_first_line + ((y - 4) < 0 ? 0 : (y - 4) / WIN32_EDIT_LINE_HEIGHT));
    if (line >= edit_line_count(w)) line = edit_line_count(w) - 1U;
    start = edit_line_start(w, line);
    end = edit_line_end(w, line);
    position = start;
    text = edit_text(w);
    for (uint32_t skip = 0; skip < (uint32_t)w->edit_hscroll && position < end; skip++)
        position++;
    while (position < end) {
        int width = edit_char_width(text[position]);
        if (x < current_x + width / 2) break;
        current_x += width;
        position++;
    }
    return position;
}

static bool edit_copy_selection(win_window_t *w, bool cut) {
    uint32_t start, end, length;
    void *handle;
    char *buffer;
    if (!w) return false;
    edit_clamp_selection(w);
    start = w->selection_start; end = w->selection_end;
    if (end <= start) return false;
    length = end - start;
    handle = win32_global_alloc_block(0x0042U, length + 1U); /* GHND */
    if (!handle) return false;
    buffer = (char *)win32_global_lock_block(handle);
    if (!buffer) { win32_global_release_handle(handle); return false; }
    kmemcpy(buffer, edit_text(w) + start, length);
    buffer[length] = '\0';
    (void)win32_global_unlock_block(handle);
    if (!win32_OpenClipboard(w->parent)) {
        win32_global_release_handle(handle);
        return false;
    }
    if (!win32_EmptyClipboard() || !win32_SetClipboardData(CF_TEXT, handle)) {
        win32_CloseClipboard();
        win32_global_release_handle(handle);
        return false;
    }
    win32_CloseClipboard();
    if (cut && !w->edit_readonly)
        return edit_replace(w, start, end, "", 0U, true, true);
    return true;
}

static bool edit_paste(win_window_t *w) {
    void *handle;
    char *buffer;
    uint32_t length;
    bool result;
    if (!w || w->edit_readonly) return false;
    if (!win32_IsClipboardFormatAvailable(CF_TEXT) ||
        !win32_OpenClipboard(w->parent)) return false;
    handle = win32_GetClipboardData(CF_TEXT);
    if (!handle) { win32_CloseClipboard(); return false; }
    buffer = (char *)win32_global_lock_block(handle);
    if (!buffer) { win32_CloseClipboard(); return false; }
    length = win32_global_size_block(handle);
    if (length) length = (uint32_t)kstrlen(buffer);
    result = edit_replace(w, w->selection_start, w->selection_end,
                          buffer, length, true, true);
    (void)win32_global_unlock_block(handle);
    win32_CloseClipboard();
    return result;
}

static bool edit_undo(win_window_t *w) {
    char *current;
    uint32_t current_length, current_caret, current_anchor;
    if (!w || !w->undo_buffer || w->edit_readonly) return false;
    current = (char *)kmalloc(w->edit_length + 1U);
    if (!current) return false;
    kmemcpy(current, edit_text(w), w->edit_length + 1U);
    current_length = w->edit_length;
    current_caret = w->edit_caret;
    current_anchor = w->edit_anchor;
    if (!edit_ensure_capacity(w, w->undo_length + 1U)) { kfree(current); return false; }
    edit_notify(w, EN_UPDATE);
    kmemcpy(w->edit_buffer, w->undo_buffer, w->undo_length + 1U);
    w->edit_length = w->undo_length;
    w->edit_caret = w->undo_caret;
    w->edit_anchor = w->undo_anchor;
    kfree(w->undo_buffer);
    w->undo_buffer = current;
    w->undo_length = current_length;
    w->undo_caret = current_caret;
    w->undo_anchor = current_anchor;
    w->edit_modified = true;
    edit_clamp_selection(w);
    edit_scroll_caret(w);
    if (w->native) { w->native->dirty = true; gui_request_paint(); }
    edit_notify(w, EN_CHANGE);
    return true;
}

static void edit_draw(win_window_t *w, gui_surface_t *surface, gui_rect_t rect) {
    const char *text;
    uint32_t lines, first, last, sel_start, sel_end, caret_line;
    int visible_lines, content_right;
    bool show_selection;
    if (!w || !surface) return;
    gui_gfx_fill_rect(surface, rect, w->enabled ? (w->rich_background ? w->rich_background : 0x00FFFFFFU) : 0x00E8E8E8U);
    gui_gfx_draw_rect(surface, rect, w->focused ? 0x000060C0U : 0x00606060U);
    text = edit_text(w);
    lines = edit_line_count(w);
    visible_lines = (rect.h - 8) / WIN32_EDIT_LINE_HEIGHT;
    if (w->style & WS_HSCROLL) visible_lines--;
    if (visible_lines < 1) visible_lines = 1;
    content_right = rect.x + rect.w - 4 - ((w->style & WS_VSCROLL) ? 12 : 0);
    first = w->edit_first_line < 0 ? 0U : (uint32_t)w->edit_first_line;
    last = first + (uint32_t)visible_lines;
    if (last > lines) last = lines;
    edit_clamp_selection(w);
    sel_start = w->selection_start; sel_end = w->selection_end;
    show_selection = !w->rich_hide_selection && (w->focused || (w->style & ES_NOHIDESEL));
    for (uint32_t line = first; line < last; line++) {
        uint32_t start = edit_line_start(w, line);
        uint32_t end = edit_line_end(w, line);
        uint32_t position = start;
        int x = rect.x + 4;
        int y = rect.y + 4 + (int)(line - first) * WIN32_EDIT_LINE_HEIGHT;
        for (int skip = 0; skip < w->edit_hscroll && position < end; skip++) position++;
        while (position < end && x < content_right) {
            char c = text[position];
            int width = edit_char_width(c);
            bool selected = show_selection && position >= sel_start && position < sel_end;
            if (selected)
                gui_gfx_fill_rect(surface, (gui_rect_t){x, y, width, WIN32_EDIT_LINE_HEIGHT}, 0x000060C0U);
            if (c != '\t') {
                char glyph[2] = {c, '\0'};
                gui_font_draw_string_clipped(surface, x, y, glyph,
                    selected ? 0x00FFFFFFU : (w->enabled ? 0x00101010U : 0x00808080U),
                    (gui_rect_t){rect.x + 3, rect.y + 2, content_right - rect.x - 3, rect.h - 4});
            }
            x += width;
            position++;
        }
    }
    caret_line = edit_line_from_char(w, w->edit_caret);
    if (w->focused && caret_line >= first && caret_line < last) {
        uint32_t start = edit_line_start(w, caret_line);
        uint32_t position = start;
        int x = rect.x + 4;
        int y = rect.y + 4 + (int)(caret_line - first) * WIN32_EDIT_LINE_HEIGHT;
        for (int skip = 0; skip < w->edit_hscroll && position < w->edit_caret; skip++) position++;
        while (position < w->edit_caret && position < w->edit_length) {
            x += edit_char_width(text[position]); position++;
        }
        if (x < content_right)
            gui_gfx_draw_line(surface, x, y, x, y + WIN32_EDIT_LINE_HEIGHT - 2, 0x00000000U);
    }
    if (w->style & WS_VSCROLL) {
        gui_rect_t track = {rect.x + rect.w - 12, rect.y + 1, 11, rect.h - 2};
        uint32_t max_first = lines > (uint32_t)visible_lines ? lines - (uint32_t)visible_lines : 0U;
        int thumb_h = max_first ? (track.h * visible_lines) / (int)lines : track.h;
        int thumb_y = max_first ? (track.h - thumb_h) * w->edit_first_line / (int)max_first : 0;
        if (thumb_h < 10) thumb_h = 10;
        gui_gfx_fill_rect(surface, track, 0x00D8D8D8U);
        gui_gfx_fill_rect(surface, (gui_rect_t){track.x + 1, track.y + thumb_y, track.w - 2, thumb_h}, 0x00A8A8A8U);
    }
}
static win_window_t *window_from_handle(void *hwnd){uint32_t v=(uint32_t)(uintptr_t)hwnd;if(v<HWND_BASE||v>=HWND_BASE+WIN32_MAX_WINDOWS)return NULL;v-=HWND_BASE;return win_windows[v].used?&win_windows[v]:NULL;}
static void *handle_from_native(gui_window_t *native){if(!native)return NULL;for(uint32_t i=0;i<WIN32_MAX_WINDOWS;i++)if(win_windows[i].used&&!win_windows[i].control&&win_windows[i].native==native)return(void*)(uintptr_t)(HWND_BASE+i);return NULL;}
static gui_rect_t window_screen_rect(win_window_t *w){if(!w)return(gui_rect_t){0,0,0,0};if(!w->control)return w->native->bounds;win_window_t*p=window_from_handle(w->parent);if(!p)return w->bounds;gui_rect_t client=gui_window_content_rect(p->native);return(gui_rect_t){client.x+w->bounds.x,client.y+w->bounds.y,w->bounds.w,w->bounds.h};}
static void queue_message(void *hwnd,uint32_t msg,uint32_t wp,int32_t lp){uint8_t next=(uint8_t)((message_tail+1U)%WIN32_MESSAGE_QUEUE);if(next==message_head)return;message_queue[message_tail]=(winmsg_t){hwnd,msg,wp,(uint32_t)lp,0,0,0};message_tail=next;}
static void cleanup_control(win_window_t *w) {
    if (!w) return;
    if (w->edit_buffer) kfree(w->edit_buffer);
    if (w->undo_buffer) kfree(w->undo_buffer);
    kmemset(w, 0, sizeof(*w));
}

static void cleanup_window(void *hwnd) {
    win_window_t *w = window_from_handle(hwnd);
    if (!w) return;
    if (w->control) { cleanup_control(w); return; }
    for (uint32_t i = 0; i < WIN32_MAX_WINDOWS; i++)
        if (win_windows[i].used && win_windows[i].control &&
            win_windows[i].parent == hwnd)
            cleanup_control(&win_windows[i]);
    for (uint32_t i = 0; i < WIN32_MAX_GDI_COMMANDS; i++)
        if (gdi_commands[i].used && gdi_commands[i].hwnd == hwnd) { if(gdi_commands[i].pixels)kfree(gdi_commands[i].pixels); gdi_commands[i].used = false; }
    if (w->native) {
        gui_desktop_remove_window(gui_get_desktop(), w->native);
        gui_window_destroy(w->native);
    }
    kmemset(w, 0, sizeof(*w));
    gui_request_paint();
}

static void normal_window_paint(gui_window_t *window, gui_surface_t *surface,
                                void *context) {
    win_window_t *owner = (win_window_t *)context;
    gui_rect_t client = gui_window_content_rect(window);
    void *owner_hwnd = (void *)(uintptr_t)(HWND_BASE +
        (uint32_t)(owner - win_windows));
    for (uint32_t n = 0; n < WIN32_MAX_GDI_COMMANDS; n++) {
        gdi_command_t *command = &gdi_commands[n];
        if (!command->used || command->hwnd != owner_hwnd) continue;
        if (command->kind == 1U)
            gui_font_draw_string_clipped(surface, client.x + command->x1,
                client.y + command->y1, command->text, command->color, client);
        else if (command->kind == 2U)
            gui_gfx_draw_line(surface, client.x + command->x1,
                client.y + command->y1, client.x + command->x2,
                client.y + command->y2, command->color);
        else if (command->kind == 3U)
            gui_gfx_draw_rect(surface, (gui_rect_t){client.x + command->x1,
                client.y + command->y1, command->x2 - command->x1,
                command->y2 - command->y1}, command->color);
        else if (command->kind == 4U)
            gui_gfx_fill_rect(surface, (gui_rect_t){client.x + command->x1,
                client.y + command->y1, command->x2 - command->x1,
                command->y2 - command->y1}, command->color);
        else if (command->kind == 5U && command->pixels) {
            int width=command->x2-command->x1,height=command->y2-command->y1;
            for(int yy=0;yy<height;yy++)for(int xx=0;xx<width;xx++)
                gui_gfx_putpixel(surface,client.x+command->x1+xx,client.y+command->y1+yy,
                    command->pixels[(command->src_y+yy)*command->pitch+command->src_x+xx]);
        }
    }
    for (uint32_t i = 0; i < WIN32_MAX_WINDOWS; i++) {
        win_window_t *control = &win_windows[i];
        gui_rect_t rect, clip;
        if (!control->used || !control->control || !control->visible ||
            control->parent != owner_hwnd)
            continue;
        rect = (gui_rect_t){client.x + control->bounds.x,
            client.y + control->bounds.y, control->bounds.w, control->bounds.h};
        if (!gui_rect_intersect(rect, client, &clip)) continue;
        if (control->kind == 1U) {
            edit_draw(control, surface, rect);
        } else if (control->kind == 2U &&
                   ((control->style & BS_TYPEMASK) == BS_CHECKBOX ||
                    (control->style & BS_TYPEMASK) == BS_AUTOCHECKBOX)) {
            gui_rect_t box = {rect.x, rect.y + (rect.h - 12) / 2, 12, 12};
            uint32_t text_color = control->enabled ? 0x00101010U : 0x00808080U;
            gui_gfx_fill_rect(surface, box, 0x00FFFFFFU);
            gui_gfx_draw_rect(surface, box, 0x00505050U);
            if (control->check_state == BST_CHECKED) {
                gui_gfx_draw_line(surface, box.x + 2, box.y + 6,
                                  box.x + 5, box.y + 9, 0x00000000U);
                gui_gfx_draw_line(surface, box.x + 5, box.y + 9,
                                  box.x + 10, box.y + 2, 0x00000000U);
            }
            gui_font_draw_string_clipped(surface, rect.x + 17, rect.y + 2,
                control->text, text_color, rect);
        } else if (control->kind == 2U &&
                   ((control->style & BS_TYPEMASK) == BS_RADIOBUTTON ||
                    (control->style & BS_TYPEMASK) == BS_AUTORADIOBUTTON)) {
            gui_rect_t radio = {rect.x, rect.y + (rect.h - 12) / 2, 12, 12};
            uint32_t text_color = control->enabled ? 0x00101010U : 0x00808080U;
            gui_gfx_fill_rect(surface, (gui_rect_t){radio.x + 2, radio.y, 8, 12}, 0x00FFFFFFU);
            gui_gfx_fill_rect(surface, (gui_rect_t){radio.x, radio.y + 2, 12, 8}, 0x00FFFFFFU);
            gui_gfx_draw_line(surface, radio.x + 3, radio.y,
                              radio.x + 8, radio.y, 0x00505050U);
            gui_gfx_draw_line(surface, radio.x + 3, radio.y + 11,
                              radio.x + 8, radio.y + 11, 0x00505050U);
            gui_gfx_draw_line(surface, radio.x, radio.y + 3,
                              radio.x, radio.y + 8, 0x00505050U);
            gui_gfx_draw_line(surface, radio.x + 11, radio.y + 3,
                              radio.x + 11, radio.y + 8, 0x00505050U);
            if (control->check_state == BST_CHECKED)
                gui_gfx_fill_rect(surface,
                    (gui_rect_t){radio.x + 4, radio.y + 4, 4, 4}, 0x00000000U);
            gui_font_draw_string_clipped(surface, rect.x + 17, rect.y + 2,
                control->text, text_color, rect);
        } else if (control->kind == 2U) {
            uint32_t face = control->pressed ? 0x00B8B8B8U : 0x00D8D8D8U;
            uint32_t light = control->pressed ? 0x00606060U : 0x00FFFFFFU;
            uint32_t dark = control->pressed ? 0x00FFFFFFU : 0x00606060U;
            int offset = control->pressed ? 1 : 0;
            int tx;
            gui_gfx_fill_rect(surface, rect, face);
            gui_gfx_draw_line(surface, rect.x, rect.y,
                              rect.x + rect.w - 1, rect.y, light);
            gui_gfx_draw_line(surface, rect.x, rect.y,
                              rect.x, rect.y + rect.h - 1, light);
            gui_gfx_draw_line(surface, rect.x, rect.y + rect.h - 1,
                              rect.x + rect.w - 1, rect.y + rect.h - 1, dark);
            gui_gfx_draw_line(surface, rect.x + rect.w - 1, rect.y,
                              rect.x + rect.w - 1, rect.y + rect.h - 1, dark);
            tx = rect.x + (rect.w - (int)gui_font_text_width(control->text)) / 2;
            gui_font_draw_string_clipped(surface, tx + offset, rect.y + 6 + offset,
                control->text, control->enabled ? 0x00101010U : 0x00808080U, rect);
        } else if (control->kind == 4U) {
            gui_gfx_fill_rect(surface, rect, 0x00D8D8D8U);
            gui_gfx_draw_line(surface, rect.x, rect.y, rect.x + rect.w - 1,
                              rect.y, 0x00FFFFFFU);
            gui_gfx_draw_line(surface, rect.x, rect.y + rect.h - 1,
                              rect.x + rect.w - 1, rect.y + rect.h - 1,
                              0x00606060U);
            int bx = rect.x + 3;
            int bw = control->toolbar_button_width > 0 ? control->toolbar_button_width : 24;
            int bh = control->toolbar_button_height > 0 ? control->toolbar_button_height : rect.h - 4;
            for (uint32_t b = 0; b < control->toolbar_count && b < 32U; b++) {
                if (control->toolbar_styles[b] & 0x01U) { bx += bw / 2; continue; }
                if (control->toolbar_states[b] & 0x08U) { bx += bw; continue; }
                gui_rect_t br = {bx, rect.y + 2, bw - 2, bh};
                uint32_t face = (control->toolbar_states[b] & 0x04U) ? 0x00C0C0C0U : 0x00E0E0E0U;
                gui_gfx_fill_rect(surface, br, face);
                gui_gfx_draw_rect(surface, br, 0x00808080U);
                char glyph[2] = {(char)('A' + (b % 26U)), '\0'};
                gui_font_draw_string_clipped(surface, br.x + (br.w - 8) / 2,
                    br.y + 5, glyph,
                    (control->toolbar_states[b] & 0x01U) ? 0x00101010U : 0x00808080U,
                    br);
                bx += bw;
            }
        } else if (control->kind == 5U) {
            gui_gfx_fill_rect(surface, rect, 0x00D8D8D8U);
            gui_gfx_draw_line(surface, rect.x, rect.y, rect.x + rect.w - 1,
                              rect.y, 0x00808080U);
            int left = rect.x;
            uint32_t parts = control->status_part_count ? control->status_part_count : 1U;
            for (uint32_t part = 0; part < parts && part < 8U; part++) {
                int right = control->status_parts[part] < 0 ||
                            control->status_parts[part] > rect.w
                    ? rect.x + rect.w : rect.x + control->status_parts[part];
                if (right <= left) right = rect.x + rect.w;
                if (part + 1U < parts)
                    gui_gfx_draw_line(surface, right - 1, rect.y + 2,
                                      right - 1, rect.y + rect.h - 2, 0x00808080U);
                gui_font_draw_string_clipped(surface, left + 4, rect.y + 4,
                    control->status_text[part], 0x00101010U,
                    (gui_rect_t){left, rect.y, right - left, rect.h});
                left = right;
            }
        } else {
            gui_font_draw_string_clipped(surface, rect.x, rect.y + 4,
                control->text, 0x00101010U, rect);
        }
    }
}
static gdi_command_t*gdi_slot(void*hwnd,uint8_t kind){if(!window_from_handle(hwnd))return NULL;for(uint32_t i=0;i<WIN32_MAX_GDI_COMMANDS;i++)if(!gdi_commands[i].used){kmemset(&gdi_commands[i],0,sizeof(gdi_commands[i]));gdi_commands[i].used=true;gdi_commands[i].hwnd=hwnd;gdi_commands[i].kind=kind;return &gdi_commands[i];}return NULL;}
void win32_gdi_begin(void*hwnd){for(uint32_t i=0;i<WIN32_MAX_GDI_COMMANDS;i++)if(gdi_commands[i].used&&gdi_commands[i].hwnd==hwnd){if(gdi_commands[i].pixels)kfree(gdi_commands[i].pixels);gdi_commands[i].used=false;}}
bool win32_gdi_text(void*hwnd,int x,int y,const char*text,uint32_t color){gdi_command_t*c=gdi_slot(hwnd,1);if(!c)return false;c->x1=x;c->y1=y;c->color=color;kstrncpy(c->text,text?text:"",sizeof(c->text)-1U);window_from_handle(hwnd)->native->dirty=true;gui_request_paint();return true;}
bool win32_gdi_line(void*hwnd,int x1,int y1,int x2,int y2,uint32_t color){gdi_command_t*c=gdi_slot(hwnd,2);if(!c)return false;c->x1=x1;c->y1=y1;c->x2=x2;c->y2=y2;c->color=color;window_from_handle(hwnd)->native->dirty=true;gui_request_paint();return true;}
bool win32_gdi_rect(void*hwnd,int l,int t,int r,int b,uint32_t color){gdi_command_t*c=gdi_slot(hwnd,3);if(!c)return false;c->x1=l;c->y1=t;c->x2=r;c->y2=b;c->color=color;window_from_handle(hwnd)->native->dirty=true;gui_request_paint();return true;}
bool win32_gdi_fill_rect(void*hwnd,int l,int t,int r,int b,uint32_t color){gdi_command_t*c=gdi_slot(hwnd,4);if(!c)return false;c->x1=l;c->y1=t;c->x2=r;c->y2=b;c->color=color;window_from_handle(hwnd)->native->dirty=true;gui_request_paint();return true;}
bool win32_gdi_blit(void*hwnd,int dx,int dy,int w,int h,const uint32_t*pixels,int pitch,int sx,int sy){if(!pixels||w<=0||h<=0||pitch<=0)return false;gdi_command_t*c=gdi_slot(hwnd,5);if(!c)return false;uint32_t count=(uint32_t)pitch*(uint32_t)(sy+h);c->pixels=(uint32_t*)kmalloc(count*sizeof(uint32_t));if(!c->pixels){c->used=false;return false;}kmemcpy(c->pixels,pixels,count*sizeof(uint32_t));c->x1=dx;c->y1=dy;c->x2=dx+w;c->y2=dy+h;c->pitch=pitch;c->src_x=sx;c->src_y=sy;window_from_handle(hwnd)->native->dirty=true;gui_request_paint();return true;}

bool win32_toolbar_configure(void *hwnd, const void *raw_buttons,
                               uint32_t count, int button_width,
                               int button_height) {
    win_window_t *w = window_from_handle(hwnd);
    const win_toolbar_button_t *buttons = (const win_toolbar_button_t *)raw_buttons;
    if (!w || !w->control || w->kind != 4U) return false;
    if (count > 32U) count = 32U;
    w->toolbar_count = (uint8_t)count;
    w->toolbar_button_width = (int16_t)(button_width > 0 ? button_width : 24);
    w->toolbar_button_height = (int16_t)(button_height > 0 ? button_height : 22);
    for (uint32_t i = 0; i < count; i++) {
        w->toolbar_commands[i] = buttons ? (uint16_t)buttons[i].idCommand : 0U;
        w->toolbar_states[i] = buttons ? buttons[i].fsState : 0x04U;
        w->toolbar_styles[i] = buttons ? buttons[i].fsStyle : 0U;
    }
    if (w->native) { w->native->dirty = true; gui_request_paint(); }
    return true;
}

static int32_t WIN32_API win32_EditWndProc(void *hwnd, uint32_t msg,
                                           uint32_t wp, int32_t lp) {
    win_window_t *w = window_from_handle(hwnd);
    const char *replacement;
    uint32_t start, end, line, line_start, line_end, length;
    if (!w || !w->control || w->kind != 1U) return 0;
    switch (msg) {
        case WM_SETTEXT:
            return edit_set_text_internal(w, (const char *)(uintptr_t)lp, false) ? 1 : 0;
        case WM_GETTEXT:
            if (!lp || wp == 0U) return 0;
            length = w->edit_length < wp - 1U ? w->edit_length : wp - 1U;
            kmemcpy((void *)(uintptr_t)lp, edit_text(w), length);
            ((char *)(uintptr_t)lp)[length] = '\0';
            return (int32_t)length;
        case WM_GETTEXTLENGTH:
            return (int32_t)w->edit_length;
        case WM_SETFONT:
            w->font = (void *)(uintptr_t)wp;
            if (lp && w->native) { w->native->dirty = true; gui_request_paint(); }
            return 0;
        case WM_GETFONT:
            return (int32_t)(uintptr_t)w->font;
        case WM_GETDLGCODE:
            return (int32_t)(DLGC_WANTARROWS | DLGC_WANTCHARS |
                ((w->style & ES_MULTILINE) ? DLGC_WANTALLKEYS : 0U));
        case WM_SETFOCUS:
            w->focused = true;
            if (w->native) { w->native->dirty = true; gui_request_paint(); }
            return 0;
        case WM_KILLFOCUS:
            w->focused = false;
            w->edit_selecting = false;
            if (w->native) { w->native->dirty = true; gui_request_paint(); }
            return 0;
        case WM_KEYDOWN: {
            uint32_t key = wp & 0xFFU;
            bool extend = win_key_shift;
            if (win_key_ctrl) {
                if (key == 'A' || key == 'a') { edit_set_selection(w, 0, -1); return 0; }
                if (key == 'C' || key == 'c') { (void)edit_copy_selection(w, false); return 0; }
                if (key == 'X' || key == 'x') { (void)edit_copy_selection(w, true); return 0; }
                if (key == 'V' || key == 'v') { (void)edit_paste(w); return 0; }
                if (key == 'Z' || key == 'z') { (void)edit_undo(w); return 0; }
            }
            if (key == VK_LEFT) {
                uint32_t pos = w->edit_caret;
                if (!extend && w->selection_start != w->selection_end) pos = w->selection_start;
                else if (pos) pos--;
                if (pos && edit_text(w)[pos] == '\n' && edit_text(w)[pos - 1U] == '\r') pos--;
                edit_move_caret(w, pos, extend); return 0;
            }
            if (key == VK_RIGHT) {
                uint32_t pos = w->edit_caret;
                if (!extend && w->selection_start != w->selection_end) pos = w->selection_end;
                else if (pos < w->edit_length) {
                    if (edit_text(w)[pos] == '\r' && pos + 1U < w->edit_length && edit_text(w)[pos + 1U] == '\n') pos += 2U;
                    else pos++;
                }
                edit_move_caret(w, pos, extend); return 0;
            }
            if (key == VK_HOME) {
                line = edit_line_from_char(w, w->edit_caret);
                edit_move_caret(w, edit_line_start(w, line), extend); return 0;
            }
            if (key == VK_END) {
                line = edit_line_from_char(w, w->edit_caret);
                edit_move_caret(w, edit_line_end(w, line), extend); return 0;
            }
            if (key == VK_UP || key == VK_DOWN) {
                uint32_t column = edit_column_from_char(w, w->edit_caret);
                line = edit_line_from_char(w, w->edit_caret);
                if (key == VK_UP && line) line--;
                else if (key == VK_DOWN && line + 1U < edit_line_count(w)) line++;
                edit_move_caret(w, edit_char_from_line_column(w, line, column), extend);
                return 0;
            }
            if (key == VK_DELETE) {
                edit_clamp_selection(w);
                start = w->selection_start; end = w->selection_end;
                if (start == end && end < w->edit_length) {
                    end++;
                    if (edit_text(w)[start] == '\r' && end < w->edit_length && edit_text(w)[end] == '\n') end++;
                }
                if (end > start) (void)edit_replace(w, start, end, "", 0U, true, true);
                return 0;
            }
            return 0;
        }
        case WM_CHAR: {
            uint8_t ch = (uint8_t)wp;
            if (win_key_ctrl) {
                if (ch == 1U || ch == 'a' || ch == 'A') { edit_set_selection(w, 0, -1); return 0; }
                if (ch == 3U || ch == 'c' || ch == 'C') { (void)edit_copy_selection(w, false); return 0; }
                if (ch == 22U || ch == 'v' || ch == 'V') { (void)edit_paste(w); return 0; }
                if (ch == 24U || ch == 'x' || ch == 'X') { (void)edit_copy_selection(w, true); return 0; }
                if (ch == 26U || ch == 'z' || ch == 'Z') { (void)edit_undo(w); return 0; }
                return 0;
            }
            if (ch < 32U) {
                if (ch == 1U) { edit_set_selection(w, 0, -1); return 0; }
                if (ch == 3U) { (void)edit_copy_selection(w, false); return 0; }
                if (ch == 22U) { (void)edit_paste(w); return 0; }
                if (ch == 24U) { (void)edit_copy_selection(w, true); return 0; }
                if (ch == 26U) { (void)edit_undo(w); return 0; }
            }
            edit_clamp_selection(w);
            if (ch == VK_BACK) {
                start = w->selection_start; end = w->selection_end;
                if (start == end && start) {
                    start--;
                    if (start && edit_text(w)[start] == '\n' && edit_text(w)[start - 1U] == '\r') start--;
                }
                if (end > start) (void)edit_replace(w, start, end, "", 0U, true, true);
                return 0;
            }
            if (ch == '\r' || ch == '\n') {
                if (w->style & ES_MULTILINE)
                    (void)edit_replace(w, w->selection_start, w->selection_end,
                                       "\r\n", 2U, true, true);
                return 0;
            }
            if (ch == '\t') {
                if (w->style & ES_MULTILINE)
                    (void)edit_replace(w, w->selection_start, w->selection_end,
                                       "\t", 1U, true, true);
                return 0;
            }
            if (ch >= 32U) {
                char value[2] = {(char)ch, '\0'};
                if ((w->style & ES_NUMBER) && (ch < '0' || ch > '9')) return 0;
                (void)edit_replace(w, w->selection_start, w->selection_end,
                                   value, 1U, true, true);
            }
            return 0;
        }
        case EM_GETSEL:
            edit_clamp_selection(w);
            if (wp) *(uint32_t *)(uintptr_t)wp = w->selection_start;
            if (lp) *(uint32_t *)(uintptr_t)lp = w->selection_end;
            return (int32_t)(((w->selection_end & 0xFFFFU) << 16) |
                             (w->selection_start & 0xFFFFU));
        case EM_SETSEL:
            edit_set_selection(w, (int32_t)wp, lp);
            return 0;
        case EM_GETRECT:
            if (lp) {
                int32_t *rect = (int32_t *)(uintptr_t)lp;
                rect[0] = 2; rect[1] = 2;
                rect[2] = w->bounds.w - 2 - ((w->style & WS_VSCROLL) ? 12 : 0);
                rect[3] = w->bounds.h - 2 - ((w->style & WS_HSCROLL) ? 12 : 0);
            }
            return 0;
        case EM_SETRECT:
        case EM_SETRECTNP:
            return 0;
        case EM_SCROLL: {
            int visible = (w->bounds.h - 8) / WIN32_EDIT_LINE_HEIGHT;
            switch (wp & 0xFFFFU) {
                case 0U: w->edit_first_line--; break;
                case 1U: w->edit_first_line++; break;
                case 2U: w->edit_first_line -= visible; break;
                case 3U: w->edit_first_line += visible; break;
                case 6U: w->edit_first_line = 0; break;
                case 7U: w->edit_first_line = (int32_t)edit_line_count(w) - visible; break;
                default: break;
            }
            if (w->edit_first_line < 0) w->edit_first_line = 0;
            edit_update_scroll_info(w);
            if (w->native) { w->native->dirty = true; gui_request_paint(); }
            return 0;
        }
        case EM_LINESCROLL:
            w->edit_hscroll += (int32_t)wp;
            w->edit_first_line += lp;
            if (w->edit_hscroll < 0) w->edit_hscroll = 0;
            if (w->edit_first_line < 0) w->edit_first_line = 0;
            edit_update_scroll_info(w);
            if (w->native) { w->native->dirty = true; gui_request_paint(); }
            return 1;
        case EM_SCROLLCARET:
            edit_scroll_caret(w); return 1;
        case EM_GETMODIFY:
            return w->edit_modified ? 1 : 0;
        case EM_SETMODIFY:
            w->edit_modified = wp != 0U; return 0;
        case EM_GETLINECOUNT:
            return (int32_t)edit_line_count(w);
        case EM_LINEINDEX:
            line = (int32_t)wp == -1 ? edit_line_from_char(w, w->edit_caret) : wp;
            if (line >= edit_line_count(w)) return -1;
            return (int32_t)edit_line_start(w, line);
        case EM_GETHANDLE:
            return (int32_t)(uintptr_t)w->edit_buffer;
        case EM_SETHANDLE:
            return edit_set_text_internal(w, (const char *)(uintptr_t)wp, false) ? 1 : 0;
        case EM_GETTHUMB:
            return w->edit_first_line;
        case EM_LINELENGTH: {
            uint32_t character = (int32_t)wp == -1 ? w->edit_caret : wp;
            line = edit_line_from_char(w, character);
            line_start = edit_line_start(w, line);
            line_end = edit_line_end(w, line);
            return (int32_t)(line_end - line_start);
        }
        case EM_REPLACESEL:
            replacement = (const char *)(uintptr_t)lp;
            edit_clamp_selection(w);
            return edit_replace(w, w->selection_start, w->selection_end,
                                replacement, replacement ? (uint32_t)kstrlen(replacement) : 0U,
                                wp != 0U, true) ? 1 : 0;
        case EM_GETLINE:
            if (!lp || wp >= edit_line_count(w)) return 0;
            line_start = edit_line_start(w, wp);
            line_end = edit_line_end(w, wp);
            length = *(uint16_t *)(uintptr_t)lp;
            if (length > line_end - line_start) length = line_end - line_start;
            kmemcpy((void *)(uintptr_t)lp, edit_text(w) + line_start, length);
            return (int32_t)length;
        case EM_SETLIMITTEXT:
            w->edit_limit = wp ? wp : WIN32_EDIT_DEFAULT_LIMIT;
            if (w->edit_limit > WIN32_EDIT_DEFAULT_LIMIT) w->edit_limit = WIN32_EDIT_DEFAULT_LIMIT;
            if (w->edit_length > w->edit_limit) {
                w->edit_length = w->edit_limit;
                w->edit_buffer[w->edit_length] = '\0';
                edit_clamp_selection(w);
            }
            return 0;
        case EM_GETLIMITTEXT:
            return (int32_t)w->edit_limit;
        case EM_CANUNDO:
            return w->undo_buffer ? 1 : 0;
        case EM_UNDO:
        case WM_UNDO:
            return edit_undo(w) ? 1 : 0;
        case EM_EMPTYUNDOBUFFER:
            edit_clear_undo(w); return 0;
        case EM_FMTLINES:
            w->edit_format_lines = wp != 0U; return 0;
        case EM_LINEFROMCHAR:
            return (int32_t)edit_line_from_char(w,
                (int32_t)wp == -1 ? w->edit_caret : wp);
        case EM_GETFIRSTVISIBLELINE:
            return w->edit_first_line;
        case EM_SETREADONLY:
            w->edit_readonly = wp != 0U;
            if (w->edit_readonly) w->style |= ES_READONLY;
            else w->style &= ~ES_READONLY;
            return 1;
        case EM_SETMARGINS:
            return 0;
        case EM_GETMARGINS:
            return 0;
        case EM_POSFROMCHAR: {
            uint32_t character = wp > w->edit_length ? w->edit_length : wp;
            uint32_t char_line = edit_line_from_char(w, character);
            uint32_t pos = edit_line_start(w, char_line);
            int x = 4, y = 4 + ((int)char_line - w->edit_first_line) * WIN32_EDIT_LINE_HEIGHT;
            for (int skip = 0; skip < w->edit_hscroll && pos < character; skip++) pos++;
            while (pos < character) x += edit_char_width(edit_text(w)[pos++]);
            return (int32_t)(((uint32_t)(uint16_t)y << 16) | (uint16_t)x);
        }
        case EM_CHARFROMPOS: {
            int x = (int16_t)(lp & 0xFFFF);
            int y = (int16_t)((uint32_t)lp >> 16);
            uint32_t character = edit_position_from_point(w, x, y);
            uint32_t char_line = edit_line_from_char(w, character);
            return (int32_t)(((char_line & 0xFFFFU) << 16) | (character & 0xFFFFU));
        }
        case EM_EXGETSEL: {
            win_char_range_t *range = (win_char_range_t *)(uintptr_t)lp;
            edit_clamp_selection(w);
            if (range) {
                range->cpMin = (int32_t)w->selection_start;
                range->cpMax = (int32_t)w->selection_end;
            }
            return 0;
        }
        case EM_EXSETSEL: {
            const win_char_range_t *range = (const win_char_range_t *)(uintptr_t)lp;
            if (!range) return -1;
            edit_set_selection(w, range->cpMin, range->cpMax);
            return (int32_t)w->selection_end;
        }
        case EM_EXLIMITTEXT:
            w->edit_limit = lp > 0 ? (uint32_t)lp : WIN32_EDIT_DEFAULT_LIMIT;
            if (w->edit_limit > WIN32_EDIT_DEFAULT_LIMIT)
                w->edit_limit = WIN32_EDIT_DEFAULT_LIMIT;
            return 0;
        case EM_EXLINEFROMCHAR:
            return (int32_t)edit_line_from_char(w,
                lp < 0 ? w->edit_caret : (uint32_t)lp);
        case EM_GETSELTEXT: {
            char *out = (char *)(uintptr_t)lp;
            edit_clamp_selection(w);
            length = w->selection_end - w->selection_start;
            if (out) {
                if (length) kmemcpy(out, edit_text(w) + w->selection_start, length);
                out[length] = '\0';
            }
            return (int32_t)length;
        }
        case EM_GETTEXTRANGE: {
            win_text_range_a_t *range = (win_text_range_a_t *)(uintptr_t)lp;
            uint32_t first, last;
            if (!range || !range->lpstrText) return 0;
            first = range->chrg.cpMin < 0 ? 0U : (uint32_t)range->chrg.cpMin;
            last = range->chrg.cpMax < 0 || (uint32_t)range->chrg.cpMax > w->edit_length
                ? w->edit_length : (uint32_t)range->chrg.cpMax;
            if (first > last) first = last;
            length = last - first;
            if (length) kmemcpy(range->lpstrText, edit_text(w) + first, length);
            range->lpstrText[length] = '\0';
            return (int32_t)length;
        }
        case EM_FINDTEXT:
        case EM_FINDTEXTEX: {
            win_find_text_ex_a_t *find = (win_find_text_ex_a_t *)(uintptr_t)lp;
            if (!find) return -1;
            return edit_find_text_range(w, find->chrg.cpMin, find->chrg.cpMax,
                find->lpstrText, wp, msg == EM_FINDTEXTEX ? &find->chrgText : NULL);
        }
        case EM_HIDESELECTION:
            w->rich_hide_selection = wp != 0U;
            if (w->native) { w->native->dirty = true; gui_request_paint(); }
            return 0;
        case EM_SELECTIONTYPE:
            edit_clamp_selection(w);
            return w->selection_start == w->selection_end ? SEL_EMPTY : SEL_TEXT;
        case EM_SETBKGNDCOLOR: {
            uint32_t old = w->rich_background ? w->rich_background : 0x00FFFFFFU;
            w->rich_background = wp ? 0x00FFFFFFU : (uint32_t)lp;
            if (w->native) { w->native->dirty = true; gui_request_paint(); }
            return (int32_t)old;
        }
        case EM_SETCHARFORMAT:
        case EM_SETPARAFORMAT:
            return 1;
        case EM_GETCHARFORMAT:
        case EM_GETPARAFORMAT:
            return 0;
        case EM_GETEVENTMASK:
            return (int32_t)w->rich_event_mask;
        case EM_SETEVENTMASK: {
            uint32_t old = w->rich_event_mask;
            w->rich_event_mask = (uint32_t)lp;
            return (int32_t)old;
        }
        case EM_GETOLEINTERFACE:
        case EM_SETOLECALLBACK:
            return 0;
        case EM_PASTESPECIAL:
            return edit_paste(w) ? 1 : 0;
        case EM_REQUESTRESIZE:
            return 0;
        case EM_SETTARGETDEVICE:
            return 1;
        case EM_FORMATRANGE:
            return wp ? (int32_t)w->edit_length : 0;
        case EM_FINDWORDBREAK:
            return 0;
        case EM_SETOPTIONS: {
            uint32_t old = w->rich_options;
            uint32_t value = (uint32_t)lp;
            if (wp == ECOOP_SET) w->rich_options = value;
            else if (wp == ECOOP_OR) w->rich_options |= value;
            else if (wp == ECOOP_AND) w->rich_options &= value;
            else if (wp == ECOOP_XOR) w->rich_options ^= value;
            return (int32_t)old;
        }
        case EM_GETOPTIONS:
            return (int32_t)w->rich_options;
        case EM_SETUNDOLIMIT: {
            uint32_t old = w->rich_undo_limit;
            w->rich_undo_limit = wp;
            if (!wp) edit_clear_undo(w);
            return (int32_t)old;
        }
        case EM_REDO:
        case EM_CANREDO:
            return 0;
        case EM_STOPGROUPTYPING:
            return 0;
        case EM_SETTEXTMODE:
            if (w->edit_length) return -1;
            w->rich_text_mode = wp;
            return 0;
        case EM_GETTEXTMODE:
            return (int32_t)w->rich_text_mode;
        case EM_AUTOURLDETECT:
            w->rich_auto_url = wp != 0U;
            return 0;
        case EM_GETAUTOURLDETECT:
            return w->rich_auto_url ? 1 : 0;
        case EM_GETTEXTEX: {
            win_get_text_ex_t *request = (win_get_text_ex_t *)(uintptr_t)wp;
            char *out = (char *)(uintptr_t)lp;
            uint32_t capacity;
            if (!request || !out || request->cb == 0U) return 0;
            capacity = request->cb;
            length = w->edit_length < capacity - 1U ? w->edit_length : capacity - 1U;
            if (length) kmemcpy(out, edit_text(w), length);
            out[length] = '\0';
            if (request->used_default_char) *request->used_default_char = 0;
            return (int32_t)length;
        }
        case EM_GETTEXTLENGTHEX:
            return (int32_t)w->edit_length;
        case EM_SETTEXTEX:
            return edit_set_text_internal(w, (const char *)(uintptr_t)lp, false) ? 1 : 0;
        case EM_SHOWSCROLLBAR:
            if ((int)wp == 0 || (int)wp == 3) {
                if (lp) w->style |= WS_HSCROLL; else w->style &= ~WS_HSCROLL;
            }
            if ((int)wp == 1 || (int)wp == 3) {
                if (lp) w->style |= WS_VSCROLL; else w->style &= ~WS_VSCROLL;
            }
            if (w->native) { w->native->dirty = true; gui_request_paint(); }
            return 1;
        case EM_STREAMIN: {
            win_edit_stream_t *stream = (win_edit_stream_t *)(uintptr_t)lp;
            uint8_t buffer[1024];
            int32_t got = 0;
            if (!stream || !stream->callback) return 0;
            stream->error = 0;
            if (!edit_set_text_internal(w, "", false)) return 0;
            for (;;) {
                got = 0;
                stream->error = stream->callback(stream->cookie, buffer,
                    (int32_t)sizeof(buffer), &got);
                if (stream->error || got <= 0) break;
                if (!edit_replace(w, w->edit_length, w->edit_length,
                    (const char *)buffer, (uint32_t)got, false, false)) {
                    stream->error = 8U; break;
                }
            }
            return (int32_t)w->edit_length;
        }
        case EM_STREAMOUT: {
            win_edit_stream_t *stream = (win_edit_stream_t *)(uintptr_t)lp;
            uint32_t position = 0U;
            if (!stream || !stream->callback) return 0;
            stream->error = 0;
            while (position < w->edit_length) {
                int32_t ask = (int32_t)(w->edit_length - position);
                int32_t sent = 0;
                if (ask > 1024) ask = 1024;
                stream->error = stream->callback(stream->cookie,
                    (uint8_t *)edit_text(w) + position, ask, &sent);
                if (stream->error || sent <= 0) break;
                position += (uint32_t)sent;
            }
            return (int32_t)position;
        }
        case WM_COPY:
            return edit_copy_selection(w, false) ? 1 : 0;
        case WM_CUT:
            return edit_copy_selection(w, true) ? 1 : 0;
        case WM_PASTE:
            return edit_paste(w) ? 1 : 0;
        case WM_CLEAR:
            edit_clamp_selection(w);
            return edit_replace(w, w->selection_start, w->selection_end,
                                "", 0U, true, true) ? 1 : 0;
        default:
            return 0;
    }
}

/* RichEdit 1.x/2.x/4.x/5.x controls use the same plain-text engine as EDIT.
 * Keep this test centralized: Metapad loads RICHED20 dynamically and creates
 * RichEdit20A, not the literal class name "EDIT". */
static bool win32_is_edit_class_name(const char *name) {
    return name && (
        equal_ci(name, "EDIT") ||
        equal_ci(name, "RichEdit20A") ||
        equal_ci(name, "RichEdit20W") ||
        equal_ci(name, "RICHEDIT20A") ||
        equal_ci(name, "RICHEDIT20W") ||
        equal_ci(name, "RICHEDIT") ||
        equal_ci(name, "RichEdit50A") ||
        equal_ci(name, "RichEdit50W") ||
        equal_ci(name, "RICHEDIT50A") ||
        equal_ci(name, "RICHEDIT50W")
    );
}

static bool win32_is_edit_control(const win_window_t *w) {
    return w && w->used && w->control &&
           (w->kind == 1U || win32_is_edit_class_name(w->class_name));
}

static uint8_t win32_key_to_vk(uint8_t key) {
    if (key >= 'a' && key <= 'z') return (uint8_t)(key - 'a' + 'A');
    if (key == '\n') return VK_RETURN;
    if (key == '\b') return VK_BACK;
    return key;
}

static bool win32_edit_force_char(win_window_t *w, uint8_t ch) {
    uint32_t start, end;

    if (!win32_is_edit_control(w) || win_key_ctrl || win_key_alt)
        return false;

    edit_clamp_selection(w);
    start = w->selection_start;
    end = w->selection_end;

    if (ch == '\b' || ch == VK_BACK) {
        if (start == end && start) {
            start--;
            if (start && edit_text(w)[start] == '\n' &&
                edit_text(w)[start - 1U] == '\r')
                start--;
        }
        return end > start &&
               edit_replace(w, start, end, "", 0U, true, true);
    }

    if (ch == '\r' || ch == '\n') {
        if (!(w->style & ES_MULTILINE)) return false;
        return edit_replace(w, start, end, "\r\n", 2U, true, true);
    }

    if (ch == '\t') {
        if (!(w->style & ES_MULTILINE)) return false;
        return edit_replace(w, start, end, "\t", 1U, true, true);
    }

    if (ch >= 32U && ch < 127U) {
        char value[2] = {(char)ch, '\0'};
        if ((w->style & ES_NUMBER) && (ch < '0' || ch > '9'))
            return false;
        return edit_replace(w, start, end, value, 1U, true, true);
    }

    return false;
}

static bool normal_window_event(gui_window_t *window,
                                const gui_event_t *event, void *context) {
    win_window_t *owner = (win_window_t *)context;
    void *hwnd;
    gui_rect_t client;

    if (!event || !owner || !owner->enabled) return false;

    win_key_shift = event->shift;
    win_key_ctrl = event->ctrl;
    win_key_alt = event->alt;
    win_mouse_buttons = event->buttons;
    win_cursor_x = event->x;
    win_cursor_y = event->y;

    hwnd = (void *)(uintptr_t)(HWND_BASE + (uint32_t)(owner - win_windows));
    client = gui_window_content_rect(window);

    if (event->type == GUI_EVENT_MOUSE_DOWN) {
        win_window_t *hit = NULL;
        void *hit_hwnd = NULL;

        /* Search backwards: controls created later are visually on top. */
        for (uint32_t n = WIN32_MAX_WINDOWS; n > 0U; n--) {
            uint32_t i = n - 1U;
            win_window_t *control = &win_windows[i];
            gui_rect_t rect;
            if (!control->used || !control->control || !control->visible ||
                control->parent != hwnd || !control->enabled) continue;
            rect = (gui_rect_t){client.x + control->bounds.x,
                client.y + control->bounds.y, control->bounds.w, control->bounds.h};
            if (gui_rect_contains(rect, event->x, event->y)) {
                hit = control;
                hit_hwnd = (void *)(uintptr_t)(HWND_BASE + i);
                break;
            }
        }

        for (uint32_t i = 0; i < WIN32_MAX_WINDOWS; i++) {
            win_window_t *control = &win_windows[i];
            void *control_hwnd;
            if (!control->used || !control->control || control->parent != hwnd)
                continue;
            control_hwnd = (void *)(uintptr_t)(HWND_BASE + i);
            if (control->focused && control != hit && control->proc)
                control->proc(control_hwnd, WM_KILLFOCUS,
                              (uint32_t)(uintptr_t)hit_hwnd, 0);
            control->focused = control == hit;
        }

        if (hit) {
            if (hit->proc)
                hit->proc(hit_hwnd, WM_SETFOCUS, 0U, 0);

            if (win32_is_edit_control(hit)) {
                int local_x = event->x - (client.x + hit->bounds.x);
                int local_y = event->y - (client.y + hit->bounds.y);
                uint32_t position = edit_position_from_point(hit, local_x, local_y);
                if (!event->shift) hit->edit_anchor = position;
                hit->edit_caret = position;
                hit->edit_selecting = true;
                edit_clamp_selection(hit);
                edit_scroll_caret(hit);
            } else if (hit->kind == 2U) {
                hit->pressed = true;
            }
        }

        window->dirty = true;
        gui_request_paint();
        return hit != NULL;
    }

    if (event->type == GUI_EVENT_MOUSE_MOVE) {
        for (uint32_t i = 0; i < WIN32_MAX_WINDOWS; i++) {
            win_window_t *control = &win_windows[i];
            if (!control->used || !control->control || control->parent != hwnd ||
                !win32_is_edit_control(control) || !control->edit_selecting)
                continue;
            control->edit_caret = edit_position_from_point(control,
                event->x - (client.x + control->bounds.x),
                event->y - (client.y + control->bounds.y));
            edit_clamp_selection(control);
            edit_scroll_caret(control);
            window->dirty = true;
            gui_request_paint();
            return true;
        }
        return false;
    }

    if (event->type == GUI_EVENT_MOUSE_UP) {
        bool handled = false;
        for (uint32_t i = 0; i < WIN32_MAX_WINDOWS; i++) {
            win_window_t *control = &win_windows[i];
            if (!control->used || !control->control || control->parent != hwnd)
                continue;

            if (win32_is_edit_control(control) && control->edit_selecting) {
                control->edit_selecting = false;
                handled = true;
            }

            if (control->kind == 2U && control->pressed) {
                control->pressed = false;
                if (control->enabled) {
                    uint32_t type = control->style & BS_TYPEMASK;
                    if (type == BS_AUTOCHECKBOX)
                        control->check_state = control->check_state == BST_CHECKED ?
                                               BST_UNCHECKED : BST_CHECKED;
                    if (type == BS_AUTORADIOBUTTON) {
                        for (uint32_t n = 0; n < WIN32_MAX_WINDOWS; n++)
                            if (win_windows[n].used && win_windows[n].control &&
                                win_windows[n].parent == hwnd &&
                                (win_windows[n].style & BS_TYPEMASK) == BS_AUTORADIOBUTTON)
                                win_windows[n].check_state = BST_UNCHECKED;
                        control->check_state = BST_CHECKED;
                    }
                    queue_message(hwnd, WM_COMMAND, control->id,
                                  (int32_t)(HWND_BASE + i));
                }
                handled = true;
            }
        }
        window->dirty = true;
        gui_request_paint();
        return handled;
    }

    if (event->type == GUI_EVENT_KEY) {
        win_window_t *target = NULL;
        void *target_hwnd = NULL;
        uint8_t key = (uint8_t)event->key;
        uint8_t vk = win32_key_to_vk(key);

        /* First use the explicitly focused edit. */
        for (uint32_t i = 0; i < WIN32_MAX_WINDOWS; i++) {
            win_window_t *control = &win_windows[i];
            if (!control->used || !control->visible || !control->enabled ||
                control->parent != hwnd || !control->focused ||
                !win32_is_edit_control(control)) continue;
            target = control;
            target_hwnd = (void *)(uintptr_t)(HWND_BASE + i);
            break;
        }

        /* Win32 gives the first editable child keyboard focus when the frame is
         * activated. Metapad relies on this after creating its RichEdit client.
         * Recover that behavior if no child currently owns focus. */
        if (!target) {
            for (uint32_t n = WIN32_MAX_WINDOWS; n > 0U; n--) {
                uint32_t i = n - 1U;
                win_window_t *control = &win_windows[i];
                if (!control->used || !control->visible || !control->enabled ||
                    control->parent != hwnd || !win32_is_edit_control(control))
                    continue;
                target = control;
                target_hwnd = (void *)(uintptr_t)(HWND_BASE + i);
                break;
            }
            if (target) {
                for (uint32_t i = 0; i < WIN32_MAX_WINDOWS; i++) {
                    win_window_t *control = &win_windows[i];
                    if (!control->used || !control->control || control->parent != hwnd)
                        continue;
                    control->focused = control == target;
                }
                if (target->proc)
                    target->proc(target_hwnd, WM_SETFOCUS, 0U, 0);
            }
        }

        if (target) {
            bool produces_char = (key >= 32U && key < 127U) ||
                                 key == 8U || key == 9U || key == 13U;

            /* Metapad/RichEdit physical keyboard fallback.
             *
             * A subclassed RichEdit receives messages in the application's
             * EditProc first.  Normally that procedure calls CallWindowProc
             * with the original control procedure returned by SetWindowLong.
             * Some Win32 programs keep that pointer in application globals and
             * the current compatibility layer can complete the call without
             * modifying the EDIT state.  Let the subclass run first, then use
             * the preserved default_proc only when caret/text/selection stayed
             * unchanged.  This avoids duplicate characters when subclassing is
             * already working while guaranteeing real physical text input. */
            if (!win_key_ctrl && !win_key_alt) {
                uint32_t before_length = target->edit_length;
                uint32_t before_caret = target->edit_caret;
                uint32_t before_anchor = target->edit_anchor;
                bool before_modified = target->edit_modified;
                wndproc_t active_proc = target->proc ? target->proc
                                                     : target->default_proc;

                if (active_proc)
                    active_proc(target_hwnd, WM_KEYDOWN, vk, 0);

                if (target->edit_length == before_length &&
                    target->edit_caret == before_caret &&
                    target->edit_anchor == before_anchor &&
                    target->edit_modified == before_modified &&
                    (target->default_proc || win32_is_edit_control(target)) &&
                    target->default_proc != active_proc) {
                    (target->default_proc ? target->default_proc : win32_EditWndProc)(
                        target_hwnd, WM_KEYDOWN, vk, 0);
                }

                if (produces_char) {
                    before_length = target->edit_length;
                    before_caret = target->edit_caret;
                    before_anchor = target->edit_anchor;
                    before_modified = target->edit_modified;
                    active_proc = target->proc ? target->proc
                                               : target->default_proc;

                    if (active_proc)
                        active_proc(target_hwnd, WM_CHAR, key, 0);

                    if (target->edit_length == before_length &&
                        target->edit_caret == before_caret &&
                        target->edit_anchor == before_anchor &&
                        target->edit_modified == before_modified &&
                        (target->default_proc || win32_is_edit_control(target)) &&
                        target->default_proc != active_proc) {
                        (target->default_proc ? target->default_proc : win32_EditWndProc)(
                            target_hwnd, WM_CHAR, key, 0);
                    }

                    if (target->edit_length == before_length &&
                        target->edit_caret == before_caret &&
                        target->edit_anchor == before_anchor &&
                        target->edit_modified == before_modified) {
                        (void)win32_edit_force_char(target, key);
                    }
                }
                return true;
            }

            /* Ctrl/Alt combinations must still pass through the application
             * message loop so accelerators and menu commands keep working. */
            queue_message(target_hwnd, WM_KEYDOWN, vk, 0);
            if (key != 0U)
                queue_message(target_hwnd, WM_CHAR, key, 0);
            return true;
        }

        queue_message(hwnd, WM_KEYDOWN, vk, 0);
        if ((key >= 32U && key < 127U) || key == 8U || key == 9U || key == 13U)
            queue_message(hwnd, WM_CHAR, key, 0);
        return true;
    }

    return false;
}

static gui_rect_t win32_effective_client_rect(win_window_t *w) {
    gui_rect_t rect = {0, 0, 1, 1};
    int fallback_width, fallback_height;

    if (!w || !w->native) return rect;

    rect = gui_window_content_rect(w->native);

    /*
     * A freshly created native window may not have its backing surface laid
     * out yet.  In that short interval gui_window_content_rect() can report
     * 0x0 even though the outer bounds are already valid.  Win32 programs
     * such as Metapad create their editor at 0x0 and size it from WM_SIZE, so
     * forwarding a zero client size leaves the child permanently at 1x1.
     */
    fallback_width = w->native->bounds.w - (GUI_BORDER_SIZE * 2);
    fallback_height = w->native->bounds.h - GUI_TITLEBAR_HEIGHT -
                      (GUI_BORDER_SIZE * 2);

    if (rect.w <= 0) rect.w = fallback_width;
    if (rect.h <= 0) rect.h = fallback_height;
    if (rect.w < 1) rect.w = 1;
    if (rect.h < 1) rect.h = 1;
    return rect;
}

static void win32_notify_move_size(void *hwnd, win_window_t *w,
                                   bool moved, bool sized) {
    gui_rect_t client;

    if (!w || w->control || !w->native || !w->proc) return;

    if (moved) {
        int x = w->native->bounds.x;
        int y = w->native->bounds.y;
        w->proc(hwnd, WM_MOVE, 0U,
                (int32_t)(((uint32_t)(uint16_t)y << 16) |
                          (uint16_t)x));
    }

    if (sized) {
        client = win32_effective_client_rect(w);
        w->proc(hwnd, WM_SIZE, 0U,
                (int32_t)(((uint32_t)(uint16_t)client.h << 16) |
                          (uint16_t)client.w));
    }
}

/*
 * RichEdit controls are commonly created at 0x0 and laid out from the
 * parent's first WM_SIZE.  Keep a conservative fallback so a missed/early
 * zero-sized WM_SIZE cannot make the editor disappear completely.
 */
static void win32_fallback_layout_edit(win_window_t *edit) {
    win_window_t *parent;
    gui_rect_t client;
    int top = 0, bottom;

    if (!win32_is_edit_control(edit) ||
        (edit->bounds.w > 1 && edit->bounds.h > 1))
        return;

    parent = window_from_handle(edit->parent);
    if (!parent || parent->control || !parent->native) return;

    client = win32_effective_client_rect(parent);
    bottom = client.h;

    for (uint32_t i = 0; i < WIN32_MAX_WINDOWS; i++) {
        win_window_t *sibling = &win_windows[i];
        if (!sibling->used || !sibling->control || !sibling->visible ||
            sibling == edit || sibling->parent != edit->parent)
            continue;

        if (sibling->kind == 4U && sibling->bounds.h > 1) {
            int edge = sibling->bounds.y + sibling->bounds.h;
            if (edge > top) top = edge;
        } else if (sibling->kind == 5U && sibling->bounds.h > 1) {
            if (sibling->bounds.y > top && sibling->bounds.y < bottom)
                bottom = sibling->bounds.y;
        }
    }

    if (bottom <= top) bottom = client.h;
    edit->bounds.x = 0;
    edit->bounds.y = top;
    edit->bounds.w = client.w;
    edit->bounds.h = bottom - top;
    if (edit->bounds.w < 1) edit->bounds.w = 1;
    if (edit->bounds.h < 1) edit->bounds.h = 1;
    edit_update_scroll_info(edit);
    edit_scroll_caret(edit);
}

static uint16_t WIN32_API win32_RegisterClassA(const wndclass_a_t *wc){if(!wc||!wc->proc||!wc->name)return 0;registered_class=*wc;kstrncpy(registered_name,wc->name,sizeof(registered_name)-1U);registered_class.name=registered_name;return 1;}
static uint16_t WIN32_API win32_RegisterClassExA(const void *raw){const uint8_t *p=(const uint8_t*)raw;return p?win32_RegisterClassA((const wndclass_a_t*)(p+4)):0;}
static void *WIN32_API win32_CreateWindowExA(uint32_t exstyle,
                                               const char *class_name,
                                               const char *title,
                                               uint32_t style,
                                               int x, int y, int w, int h,
                                               void *parent, void *menu,
                                               void *instance, void *param) {
    gui_desktop_t *desktop = gui_get_desktop();
    win_window_t *pw = window_from_handle(parent);
    bool control = is_edit_class(class_name) ||
                   equal_ci(class_name, "BUTTON") ||
                   equal_ci(class_name, "STATIC") ||
                   equal_ci(class_name, "ToolbarWindow32") ||
                   is_status_class(class_name);
    /* Stage 11C: RichEdit must pass the early child-control guard. */
    if (
                equal_ci(class_name, "RichEdit20A") ||
                equal_ci(class_name, "RichEdit20W") ||
                equal_ci(class_name, "RICHEDIT20A") ||
                equal_ci(class_name, "RICHEDIT20W") ||
                equal_ci(class_name, "RICHEDIT") ||
                equal_ci(class_name, "RichEdit50A") ||
                equal_ci(class_name, "RichEdit50W") ||
                equal_ci(class_name, "RICHEDIT50A") ||
                equal_ci(class_name, "RICHEDIT50W")
            ) control = true;

    if (!desktop || (!control && !equal_ci(class_name, registered_class.name)) ||
        (control && !pw)) return NULL;

    if (!control) {
        if ((uint32_t)x == 0x80000000U) x = 80;
        if ((uint32_t)y == 0x80000000U) y = 60;
        if ((uint32_t)w == 0x80000000U || w < 160) w = 480;
        if ((uint32_t)h == 0x80000000U || h < 90) h = 320;
    }

    for (uint32_t i = 0; i < WIN32_MAX_WINDOWS; i++) {
        void *hwnd;
        win_window_t *ww;

        if (win_windows[i].used) continue;
        ww = &win_windows[i];
        hwnd = (void *)(uintptr_t)(HWND_BASE + i);
        kmemset(ww, 0, sizeof(*ww));
        ww->used = true;
        ww->enabled = (style & WS_DISABLED) == 0U;
        ww->visible = control
            ? (((style & WS_VISIBLE) != 0U) ||
               (is_rich_edit_class(class_name) && w <= 0 && h <= 0))
            : true;
        ww->style = style;
        ww->exstyle = exstyle;
        ww->instance = instance;
        kstrncpy(ww->class_name, class_name ? class_name : "",
                 sizeof(ww->class_name) - 1U);

        if (control) {
            ww->control = true;
            ww->parent = parent;
            ww->native = pw->native;
            ww->bounds = (gui_rect_t){x, y, w, h};
            ww->id = (uint32_t)(uintptr_t)menu;
            ww->kind = is_edit_class(class_name) ? 1U :
                       (equal_ci(class_name, "BUTTON") ? 2U :
                       (equal_ci(class_name, "ToolbarWindow32") ? 4U :
                       (is_status_class(class_name) ? 5U : 3U)));
            /* Stage 11C: RichEdit uses the EDIT engine. */
            if (
                    equal_ci(class_name, "RichEdit20A") ||
                    equal_ci(class_name, "RichEdit20W") ||
                    equal_ci(class_name, "RICHEDIT20A") ||
                    equal_ci(class_name, "RICHEDIT20W") ||
                    equal_ci(class_name, "RICHEDIT") ||
                    equal_ci(class_name, "RichEdit50A") ||
                    equal_ci(class_name, "RichEdit50W") ||
                    equal_ci(class_name, "RICHEDIT50A") ||
                    equal_ci(class_name, "RICHEDIT50W")
                ) ww->kind = 1U;
            if (win32_is_edit_class_name(class_name)) ww->kind = 1U;
            if (ww->kind == 1U) {
                ww->rich_edit = is_rich_edit_class(class_name);
                ww->rich_background = 0x00FFFFFFU;
                ww->rich_undo_limit = 100U;
                ww->edit_limit = WIN32_EDIT_DEFAULT_LIMIT;
                ww->edit_readonly = (style & ES_READONLY) != 0U;
                ww->default_proc = win32_EditWndProc;
                ww->proc = ww->default_proc;
                if (!edit_set_text_internal(ww, title ? title : "", false)) {
                    cleanup_control(ww);
                    return NULL;
                }
                if (ww->rich_edit) win32_fallback_layout_edit(ww);
            } else {
                kstrncpy(ww->text, title ? title : "", sizeof(ww->text) - 1U);
            }
            pw->native->dirty = true;
            return hwnd;
        }

        ww->native = gui_desktop_create_window(desktop, x, y, w, h,
                                                title ? title : "");
        if (!ww->native) {
            kmemset(ww, 0, sizeof(*ww));
            return NULL;
        }
        ww->proc = registered_class.proc;
        ww->native->owner_pid = task_current_pid();
        ww->native->visible = ww->visible;
        gui_window_set_content(ww->native, normal_window_paint, ww);
        gui_window_set_event_handler(ww->native, normal_window_event, ww);
        /* normal_window_* es parte de USER32, pero termina invocando el
         * WndProc de la aplicacion. Se ejecuta como upcall en la tarea Win32
         * para que el WndProc nunca sea llamado desde el compositor CPL0. */
        ww->native->content_pid = task_current_pid();
        ww->native->event_pid = task_current_pid();

        /* Win32 creates top-level windows synchronously.  Applications such
         * as WineCalc initialize all menus, lookup tables and drawing state
         * from WM_CREATE, so queuing only WM_PAINT leaves their state zeroed. */
        if (ww->proc) {
            create_struct_a_t create = {
                param, instance, menu, parent, h, w, y, x, (int32_t)style,
                title, class_name, exstyle
            };
            int32_t accepted = ww->proc(hwnd, WM_NCCREATE, 0,
                                        (int32_t)(uintptr_t)&create);
            if (!accepted) {
                cleanup_window(hwnd);
                return NULL;
            }
            if (ww->proc(hwnd, WM_CREATE, 0,
                         (int32_t)(uintptr_t)&create) == -1) {
                cleanup_window(hwnd);
                return NULL;
            }

            /* These messages are normally generated during creation.  Use
             * an effective client rectangle so an uninitialized native
             * surface cannot report a transient 0x0 client area. */
            win32_notify_move_size(hwnd, ww, true, true);
        }

        queue_message(hwnd, WM_PAINT, 0, 0);
        return hwnd;
    }
    return NULL;
}
static int WIN32_API win32_ShowWindow(void *hwnd, int command) {
    win_window_t *w = window_from_handle(hwnd);
    int was_visible;

    if (!w) return 0;
    was_visible = w->visible;
    w->visible = command != SW_HIDE;

    if (w->control) {
        if (w->visible && win32_is_edit_control(w))
            win32_fallback_layout_edit(w);
    } else {
        w->native->visible = w->visible;
        /*
         * Deliver a second, now-authoritative WM_SIZE when the frame becomes
         * visible.  Metapad sizes RichEdit20A from this notification.
         */
        if (w->visible)
            win32_notify_move_size(hwnd, w, false, true);
    }

    if (w->native) w->native->dirty = true;
    gui_request_paint();
    return was_visible;
}
static int WIN32_API win32_UpdateWindow(void *hwnd){win_window_t*w=window_from_handle(hwnd);if(!w)return 0;w->native->dirty=true;gui_request_paint();return 1;}
static int WIN32_API win32_DestroyWindow(void *hwnd){win_window_t*w=window_from_handle(hwnd);if(!w)return 0;if(w->control){cleanup_control(w);return 1;}queue_message(hwnd,WM_DESTROY,0,0);gui_window_close(w->native);return 1;}
static void WIN32_API win32_PostQuitMessage(int code){queue_message(NULL,WM_QUIT,(uint32_t)code,0);}
static int WIN32_API win32_GetMessageA(winmsg_t *msg,void *hwnd UNUSED,uint32_t min UNUSED,uint32_t max UNUSED){if(!msg)return -1;for(;;){uint32_t now=pit_get_ticks(),hz=pit_get_frequency_hz();for(uint32_t i=0;i<8U;i++)if(win_timers[i].used&&(int32_t)(now-win_timers[i].next)>=0){queue_message(win_timers[i].hwnd,WM_TIMER,win_timers[i].id,(int32_t)(uintptr_t)win_timers[i].callback);win_timers[i].next=now+(win_timers[i].interval*(hz?hz:100U)+999U)/1000U;}for(uint32_t i=0;i<WIN32_MAX_WINDOWS;i++)if(win_windows[i].used&&!win_windows[i].control&&!win_windows[i].native->visible&&!win_windows[i].destroy_sent){win_windows[i].destroy_sent=true;queue_message((void*)(uintptr_t)(HWND_BASE+i),WM_DESTROY,0,0);}if(message_head!=message_tail){*msg=message_queue[message_head];message_head=(uint8_t)((message_head+1U)%WIN32_MESSAGE_QUEUE);return msg->message==WM_QUIT?0:1;}task_sleep(1U);}}
static int WIN32_API win32_PeekMessageA(winmsg_t *msg,void *hwnd UNUSED,uint32_t min UNUSED,uint32_t max UNUSED,uint32_t remove){if(!msg||message_head==message_tail)return 0;*msg=message_queue[message_head];if(remove&1U)message_head=(uint8_t)((message_head+1U)%WIN32_MESSAGE_QUEUE);return 1;}
static int32_t WIN32_API win32_DispatchMessageA(const winmsg_t *msg){win_window_t*w;int32_t result=0;if(!msg)return 0;w=window_from_handle(msg->hwnd);if(w&&w->proc)result=w->proc(msg->hwnd,msg->message,msg->wparam,(int32_t)msg->lparam);if(w&&msg->message==WM_DESTROY)cleanup_window(msg->hwnd);return result;}
static int WIN32_API win32_TranslateMessage(const winmsg_t *msg UNUSED){return 1;}
static int32_t WIN32_API win32_DefWindowProcA(void *hwnd,uint32_t msg,
                                                 uint32_t wp UNUSED,
                                                 int32_t lp UNUSED) {
    /* Returning TRUE for WM_NCCREATE is required for CreateWindowEx to
     * continue when the application's WNDPROC delegates this message. */
    if (msg == WM_NCCREATE) return 1;
    if (msg == WM_CLOSE) {
        win32_DestroyWindow(hwnd);
        return 0;
    }
    return 0;
}
static int WIN32_API win32_SetWindowTextA(void *hwnd,const char*text){
    win_window_t*w=window_from_handle(hwnd);
    if(!w)return 0;
    if(w->control&&w->kind==1U)return edit_set_text_internal(w,text?text:"",false)?1:0;
    if(w->control)kstrncpy(w->text,text?text:"",sizeof(w->text)-1U);
    else kstrncpy(w->native->title,text?text:"",sizeof(w->native->title)-1U);
    w->native->dirty=true;gui_request_paint();return 1;
}
static int WIN32_API win32_GetWindowTextLengthA(void*hwnd){
    win_window_t*w=window_from_handle(hwnd);
    if(!w)return 0;
    if(w->control&&w->kind==1U)return(int)w->edit_length;
    return(int)kstrlen(w->control?w->text:w->native->title);
}
static int WIN32_API win32_GetWindowTextA(void*hwnd,char*out,int size){
    win_window_t*w=window_from_handle(hwnd);const char*t;uint32_t length;
    if(!w||!out||size<=0)return 0;
    if(w->control&&w->kind==1U){
        length=w->edit_length<(uint32_t)size-1U?w->edit_length:(uint32_t)size-1U;
        kmemcpy(out,edit_text(w),length);out[length]='\0';return(int)length;
    }
    t=w->control?w->text:w->native->title;kstrncpy(out,t,(size_t)size-1U);out[size-1]='\0';return(int)kstrlen(out);
}
static int WIN32_API win32_GetClientRect(void *hwnd,int32_t *rect){win_window_t*w=window_from_handle(hwnd);gui_rect_t r;if(!w||!rect)return 0;if(w->control)r=w->bounds;else r=gui_window_content_rect(w->native);rect[0]=0;rect[1]=0;rect[2]=r.w;rect[3]=r.h;return 1;}

/* GetDC/ReleaseDC and BeginPaint/EndPaint are USER32 exports on Windows.
 * The HDC used by this compatibility layer is currently the HWND itself,
 * which is also what the GDI32 drawing bridge expects. */
static void *WIN32_API win32_GetDC(void *hwnd) {
    return window_from_handle(hwnd) ? hwnd : NULL;
}

static int WIN32_API win32_ReleaseDC(void *hwnd, void *dc) {
    return window_from_handle(hwnd) && dc == hwnd ? 1 : 0;
}

static void *WIN32_API win32_BeginPaint(void *hwnd, void *paint) {
    int32_t *rect;

    if (!window_from_handle(hwnd)) return NULL;
    win32_gdi_begin(hwnd);

    if (paint) {
        kmemset(paint, 0, 64U);
        *(void **)paint = hwnd;
        /* PAINTSTRUCT.rcPaint begins at byte 8 on Win32. */
        rect = (int32_t *)((uint8_t *)paint + 8U);
        win32_GetClientRect(hwnd, rect);
    }

    return hwnd;
}

static int WIN32_API win32_EndPaint(void *hwnd, const void *paint UNUSED) {
    win_window_t *window = window_from_handle(hwnd);
    if (!window) return 0;
    window->native->dirty = true;
    gui_request_paint();
    return 1;
}
static int32_t WIN32_API win32_SendMessageA(void *hwnd, uint32_t msg,
                                               uint32_t wp, int32_t lp) {
    win_window_t *w = window_from_handle(hwnd);
    if (!w) return 0;
    /* EDIT controls have a real default window procedure.  When an
     * application subclasses the control with SetWindowLong(GWL_WNDPROC),
     * messages reach that procedure first and CallWindowProc(old, ...)
     * continues into win32_EditWndProc. */
    if (w->control && w->kind == 1U)
        return w->proc ? w->proc(hwnd, msg, wp, lp)
                       : win32_EditWndProc(hwnd, msg, wp, lp);
    if (msg == WM_SETTEXT)
        return win32_SetWindowTextA(hwnd, (const char *)(uintptr_t)lp);
    if (msg == WM_GETTEXT)
        return win32_GetWindowTextA(hwnd, (char *)(uintptr_t)lp, (int)wp);
    if (msg == WM_GETTEXTLENGTH) return win32_GetWindowTextLengthA(hwnd);
    if (msg == WM_SETFONT) { w->font = (void *)(uintptr_t)wp; return 0; }
    if (msg == WM_GETFONT) return (int32_t)(uintptr_t)w->font;
    if (w->control && w->kind == 2U) {
        if (msg == BM_GETCHECK) return (int32_t)w->check_state;
        if (msg == BM_SETCHECK) {
            w->check_state = wp;
            w->native->dirty = true;
            gui_request_paint();
            return 0;
        }
    }
    if (w->control && w->kind == 4U) {
        if (msg == TB_BUTTONCOUNT) return w->toolbar_count;
        if (msg == TB_AUTOSIZE) {
            win_window_t *parent = window_from_handle(w->parent);
            if (parent) {
                gui_rect_t client = gui_window_content_rect(parent->native);
                w->bounds.x = 0; w->bounds.y = 0; w->bounds.w = client.w;
            }
            w->native->dirty = true; gui_request_paint(); return 1;
        }
        if (msg == TB_SETBUTTONSIZE) {
            w->toolbar_button_width = (int16_t)(lp & 0xFFFF);
            w->toolbar_button_height = (int16_t)((uint32_t)lp >> 16);
            return 1;
        }
        if (msg == TB_SETBITMAPSIZE) return 1;
        if (msg == TB_ENABLEBUTTON || msg == TB_CHECKBUTTON || msg == TB_HIDEBUTTON) {
            for (uint32_t i = 0; i < w->toolbar_count; i++)
                if (w->toolbar_commands[i] == (uint16_t)wp) {
                    if (msg == TB_ENABLEBUTTON) {
                        if (lp) w->toolbar_states[i] |= 0x04U;
                        else w->toolbar_states[i] &= (uint8_t)~0x04U;
                    } else if (msg == TB_CHECKBUTTON) {
                        if (lp) w->toolbar_states[i] |= 0x01U;
                        else w->toolbar_states[i] &= (uint8_t)~0x01U;
                    } else {
                        if (lp) w->toolbar_states[i] |= 0x08U;
                        else w->toolbar_states[i] &= (uint8_t)~0x08U;
                    }
                    w->native->dirty = true; gui_request_paint(); return 1;
                }
            return 0;
        }
        if (msg == TB_GETITEMRECT && lp && wp < w->toolbar_count) {
            int32_t *rect = (int32_t *)(uintptr_t)lp;
            int bw = w->toolbar_button_width > 0 ? w->toolbar_button_width : 24;
            int x = 3;
            for (uint32_t i = 0; i < wp; i++)
                x += (w->toolbar_styles[i] & 0x01U) ? bw / 2 : bw;
            rect[0] = x; rect[1] = 2;
            rect[2] = x + ((w->toolbar_styles[wp] & 0x01U) ? bw / 2 : bw) - 2;
            rect[3] = w->bounds.h - 2;
            return 1;
        }
    }
    if (w->control && w->kind == 5U) {
        if (msg == SB_SETPARTS) {
            uint32_t count = wp > 8U ? 8U : wp;
            const int32_t *parts = (const int32_t *)(uintptr_t)lp;
            w->status_part_count = (uint8_t)count;
            for (uint32_t i = 0; i < count; i++) w->status_parts[i] = parts ? parts[i] : -1;
            w->native->dirty = true; gui_request_paint(); return 1;
        }
        if (msg == SB_GETPARTS) {
            int32_t *parts = (int32_t *)(uintptr_t)lp;
            uint32_t count = wp < w->status_part_count ? wp : w->status_part_count;
            if (parts) for (uint32_t i = 0; i < count; i++) parts[i] = w->status_parts[i];
            return w->status_part_count;
        }
        if (msg == SB_SETTEXTA) {
            uint32_t part = wp & 0xFFU;
            if (part >= 8U) return 0;
            const char *text = (const char *)(uintptr_t)lp;
            kstrncpy(w->status_text[part], text ? text : "",
                     sizeof(w->status_text[part]) - 1U);
            w->status_text[part][sizeof(w->status_text[part]) - 1U] = '\0';
            if (part >= w->status_part_count) w->status_part_count = (uint8_t)(part + 1U);
            w->native->dirty = true; gui_request_paint(); return 1;
        }
        if (msg == SB_GETTEXTLENGTHA) {
            uint32_t part = wp & 0xFFU;
            return part < 8U ? (int32_t)kstrlen(w->status_text[part]) : 0;
        }
        if (msg == SB_GETTEXTA) {
            uint32_t part = wp & 0xFFU;
            if (part >= 8U || !lp) return 0;
            kstrcpy((char *)(uintptr_t)lp, w->status_text[part]);
            return (int32_t)kstrlen(w->status_text[part]);
        }
        if (msg == SB_SETMINHEIGHT) {
            if ((int)wp > w->bounds.h) w->bounds.h = (int)wp;
            return 0;
        }
    }
    return w->proc ? w->proc(hwnd, msg, wp, lp) : 0;
}
static int WIN32_API win32_PostMessageA(void*hwnd,uint32_t msg,uint32_t wp,int32_t lp){if(hwnd&&!window_from_handle(hwnd))return 0;queue_message(hwnd,msg,wp,lp);return 1;}
static void *WIN32_API win32_SetFocus(void *hwnd) {
    win_window_t *w = window_from_handle(hwnd);
    void *old = NULL;
    if (!w) return NULL;
    for (uint32_t i = 0; i < WIN32_MAX_WINDOWS; i++) {
        if (!win_windows[i].used || !win_windows[i].focused) continue;
        old = (void *)(uintptr_t)(HWND_BASE + i);
        if (old != hwnd && win_windows[i].proc)
            win_windows[i].proc(old, WM_KILLFOCUS, (uint32_t)(uintptr_t)hwnd, 0);
        win_windows[i].focused = false;
    }
    w->focused = true;
    if (w->proc) w->proc(hwnd, WM_SETFOCUS, (uint32_t)(uintptr_t)old, 0);
    if (w->native) { w->native->dirty = true; gui_request_paint(); }
    return old;
}
static void*WIN32_API win32_GetFocus(void){for(uint32_t i=0;i<WIN32_MAX_WINDOWS;i++)if(win_windows[i].used&&win_windows[i].focused)return(void*)(uintptr_t)(HWND_BASE+i);return NULL;}
static bool wide_to_ansi(const uint16_t *wide, char *out, uint32_t size) {
    uint32_t i = 0;
    if (!wide || !out || !size) return false;
    while (wide[i]) {
        if (i + 1U >= size) return false;
        out[i] = wide[i] <= 0xFFU ? (char)wide[i] : '?';
        i++;
    }
    out[i] = '\0';
    return true;
}

static uint32_t ansi_to_wide(const char *text, uint16_t *out, uint32_t size) {
    uint32_t length = text ? (uint32_t)kstrlen(text) : 0U;
    if (!out || !size) return length;
    uint32_t copied = length < size - 1U ? length : size - 1U;
    for (uint32_t i = 0; i < copied; i++) out[i] = (uint8_t)text[i];
    out[copied] = 0U;
    return copied;
}

static int WIN32_API win32_LoadStringW(void *instance, uint32_t id,
                                       uint16_t *buffer, int max_chars) {
    uint32_t block = id / 16U + 1U;
    uint32_t index = id % 16U;
    void *resource = win32_resource_find_w(instance,
        (const void *)(uintptr_t)WIN32_RT_STRING,
        (const void *)(uintptr_t)block, 0U, false);
    const uint16_t *data = (const uint16_t *)win32_resource_lock(resource);
    uint32_t bytes = win32_resource_size(instance, resource);
    if (!data || bytes < sizeof(uint16_t) || max_chars <= 0 || !buffer) return 0;
    const uint16_t *end = (const uint16_t *)((const uint8_t *)data + bytes);
    for (uint32_t n = 0; n < index; n++) {
        if (data >= end) return 0;
        uint16_t length = *data++;
        if ((uint32_t)(end - data) < length) return 0;
        data += length;
    }
    if (data >= end) return 0;
    uint16_t length = *data++;
    if ((uint32_t)(end - data) < length) return 0;
    uint32_t copied = length < (uint32_t)max_chars - 1U ?
                      length : (uint32_t)max_chars - 1U;
    for (uint32_t i = 0; i < copied; i++) buffer[i] = data[i];
    buffer[copied] = 0U;
    return (int)copied;
}

static int WIN32_API win32_LoadStringA(void *instance, uint32_t id,
                                       char *buffer, int max_chars) {
    uint16_t wide[256];
    int length = win32_LoadStringW(instance, id, wide,
                                   (int)(sizeof(wide) / sizeof(wide[0])));
    if (!buffer || max_chars <= 0 || length <= 0) return 0;
    uint32_t copied = (uint32_t)length < (uint32_t)max_chars - 1U ?
                      (uint32_t)length : (uint32_t)max_chars - 1U;
    for (uint32_t i = 0; i < copied; i++)
        buffer[i] = wide[i] <= 0xFFU ? (char)wide[i] : '?';
    buffer[copied] = '\0';
    return (int)copied;
}

static void *load_resource_object_a(void *instance, const char *name,
                                    uint32_t primary_type,
                                    uint32_t fallback_type) {
    if (!instance && (uint32_t)(uintptr_t)name <= 0xFFFFU) return (void *)name;
    void *resource = win32_resource_find(instance,
        (const void *)(uintptr_t)primary_type, name, 0U, false);
    if (!resource && fallback_type)
        resource = win32_resource_find(instance,
            (const void *)(uintptr_t)fallback_type, name, 0U, false);
    return resource;
}

static void *load_resource_object_w(void *instance, const uint16_t *name,
                                    uint32_t primary_type,
                                    uint32_t fallback_type) {
    if (!instance && (uint32_t)(uintptr_t)name <= 0xFFFFU) return (void *)name;
    void *resource = win32_resource_find_w(instance,
        (const void *)(uintptr_t)primary_type, name, 0U, false);
    if (!resource && fallback_type)
        resource = win32_resource_find_w(instance,
            (const void *)(uintptr_t)fallback_type, name, 0U, false);
    return resource;
}

static void *WIN32_API win32_LoadCursorA(void *instance, const char *name) {
    return load_resource_object_a(instance, name, WIN32_RT_GROUP_CURSOR,
                                  WIN32_RT_CURSOR);
}
static void *WIN32_API win32_LoadCursorW(void *instance, const uint16_t *name) {
    return load_resource_object_w(instance, name, WIN32_RT_GROUP_CURSOR,
                                  WIN32_RT_CURSOR);
}
static void *WIN32_API win32_LoadIconA(void *instance, const char *name) {
    return load_resource_object_a(instance, name, WIN32_RT_GROUP_ICON,
                                  WIN32_RT_ICON);
}
static void *WIN32_API win32_LoadIconW(void *instance, const uint16_t *name) {
    return load_resource_object_w(instance, name, WIN32_RT_GROUP_ICON,
                                  WIN32_RT_ICON);
}
static void *WIN32_API win32_LoadBitmapA(void *instance, const char *name) {
    return load_resource_object_a(instance, name, WIN32_RT_BITMAP, 0U);
}
static void *WIN32_API win32_LoadBitmapW(void *instance, const uint16_t *name) {
    return load_resource_object_w(instance, name, WIN32_RT_BITMAP, 0U);
}
static int WIN32_API win32_InvalidateRect(void*hwnd,const int32_t*rect UNUSED,int erase UNUSED){win_window_t*w=window_from_handle(hwnd);if(!w)return 0;w->native->dirty=true;if(!w->control)queue_message(hwnd,WM_PAINT,0,0);gui_request_paint();return 1;}
static int WIN32_API win32_MoveWindow(void *hwnd, int x, int y,
                                        int width, int height, int repaint) {
    win_window_t *w = window_from_handle(hwnd);
    if (!w) return 0;

    if (width < 1) width = 1;
    if (height < 1) height = 1;

    if (w->control) {
        w->bounds = (gui_rect_t){x, y, width, height};
        if (win32_is_edit_control(w)) {
            edit_update_scroll_info(w);
            edit_scroll_caret(w);
        }
    } else {
        w->native->bounds = (gui_rect_t){x, y, width, height};
        win32_notify_move_size(hwnd, w, true, true);
    }

    if (repaint && w->native) {
        w->native->dirty = true;
        gui_request_paint();
    }
    return 1;
}
static int WIN32_API win32_GetWindowRect(void*hwnd,int32_t*rect){win_window_t*w=window_from_handle(hwnd);gui_rect_t r;if(!w||!rect)return 0;r=window_screen_rect(w);rect[0]=r.x;rect[1]=r.y;rect[2]=r.x+r.w;rect[3]=r.y+r.h;return 1;}
static int WIN32_API win32_SetWindowPos(void *hwnd, void *after,
                                         int x, int y, int width, int height,
                                         uint32_t flags) {
    win_window_t *w = window_from_handle(hwnd);
    gui_desktop_t *desktop = gui_get_desktop();
    bool moved = (flags & SWP_NOMOVE) == 0U;
    bool sized = (flags & SWP_NOSIZE) == 0U;

    if (!w) return 0;

    if (!(flags & SWP_NOMOVE)) {
        if (w->control) {
            w->bounds.x = x;
            w->bounds.y = y;
        } else {
            w->native->bounds.x = x;
            w->native->bounds.y = y;
        }
    }

    if (!(flags & SWP_NOSIZE)) {
        if (width < 1) width = 1;
        if (height < 1) height = 1;
        if (w->control) {
            w->bounds.w = width;
            w->bounds.h = height;
            if (win32_is_edit_control(w)) {
                edit_update_scroll_info(w);
                edit_scroll_caret(w);
            }
        } else {
            w->native->bounds.w = width;
            w->native->bounds.h = height;
        }
    }

    /* Metapad creates RichEdit20A hidden and reveals it later through
     * SetWindowPos(..., SWP_SHOWWINDOW). */
    if (flags & SWP_SHOWWINDOW) {
        w->visible = true;
        if (!w->control && w->native) {
            w->native->visible = true;
        } else if (w->control && win32_is_edit_control(w)) {
            win32_fallback_layout_edit(w);
        }
    }

    if (flags & SWP_HIDEWINDOW) {
        if (w->focused) {
            w->focused = false;
            if (w->proc) w->proc(hwnd, WM_KILLFOCUS, 0U, 0);
        }
        w->visible = false;
        if (!w->control && w->native) w->native->visible = false;
    }

    if (!(flags & SWP_NOZORDER) && !w->control && desktop &&
        (after == NULL || (uint32_t)(uintptr_t)after == 0U)) {
        gui_desktop_raise_window(desktop, w->native);
    }

    if (!(flags & SWP_NOACTIVATE)) {
        if (!w->control && desktop) {
            gui_desktop_focus_window(desktop, w->native);
        } else if (w->control && w->visible && w->enabled && win32_is_edit_control(w)) {
            (void)win32_SetFocus(hwnd);
        }
    }

    if (flags & SWP_FRAMECHANGED) {
        if (w->control && win32_is_edit_control(w)) {
            edit_update_scroll_info(w);
            edit_scroll_caret(w);
        }
    }

    if (!w->control && (moved || sized))
        win32_notify_move_size(hwnd, w, moved, sized);

    if (w->native) w->native->dirty = true;
    gui_request_paint();
    return 1;
}
static int WIN32_API win32_EnableWindow(void*hwnd,int enable){win_window_t*w=window_from_handle(hwnd);int was_disabled;if(!w)return 0;was_disabled=!w->enabled;w->enabled=enable!=0;if(!w->enabled)w->focused=false;w->native->dirty=true;gui_request_paint();return was_disabled;}
static int WIN32_API win32_IsWindowEnabled(void*hwnd){win_window_t*w=window_from_handle(hwnd);return w&&w->enabled;}
static int WIN32_API win32_IsWindowVisible(void*hwnd){win_window_t*w=window_from_handle(hwnd);return w&&w->visible;}
static void*WIN32_API win32_GetParent(void*hwnd){win_window_t*w=window_from_handle(hwnd);return w?w->parent:NULL;}
static void*WIN32_API win32_GetForegroundWindow(void){gui_desktop_t*d=gui_get_desktop();return d?handle_from_native(d->focused_window):NULL;}
static int WIN32_API win32_SetForegroundWindow(void*hwnd){win_window_t*w=window_from_handle(hwnd);gui_desktop_t*d=gui_get_desktop();if(!w||w->control||!d)return 0;gui_desktop_focus_window(d,w->native);gui_desktop_raise_window(d,w->native);gui_request_paint();return 1;}
static int WIN32_API win32_BringWindowToTop(void*hwnd){win_window_t*w=window_from_handle(hwnd);gui_desktop_t*d=gui_get_desktop();if(!w||w->control||!d)return 0;gui_desktop_raise_window(d,w->native);w->native->dirty=true;gui_request_paint();return 1;}
static int WIN32_API win32_ClientToScreen(void*hwnd,int32_t*point){win_window_t*w=window_from_handle(hwnd);gui_rect_t r;if(!w||!point)return 0;if(w->control)r=window_screen_rect(w);else r=gui_window_content_rect(w->native);point[0]+=r.x;point[1]+=r.y;return 1;}
static int WIN32_API win32_ScreenToClient(void*hwnd,int32_t*point){win_window_t*w=window_from_handle(hwnd);gui_rect_t r;if(!w||!point)return 0;if(w->control)r=window_screen_rect(w);else r=gui_window_content_rect(w->native);point[0]-=r.x;point[1]-=r.y;return 1;}

static int16_t WIN32_API win32_GetKeyState(int key) {
    switch ((uint32_t)key & 0xFFU) {
        case VK_SHIFT: return win_key_shift ? (int16_t)0x8000 : 0;
        case VK_CONTROL: return win_key_ctrl ? (int16_t)0x8000 : 0;
        case VK_MENU: return win_key_alt ? (int16_t)0x8000 : 0;
        case VK_CAPITAL: return 0;
        case 0x01U: return (win_mouse_buttons & 1U) ? (int16_t)0x8000 : 0;
        default: return 0;
    }
}

static int WIN32_API win32_MessageBeep(uint32_t type) {
    uint32_t frequency = 750U;
    switch (type & 0xF0U) {
        case 0x10U: frequency = 220U; break;
        case 0x20U: frequency = 440U; break;
        case 0x30U: frequency = 660U; break;
        case 0x40U: frequency = 880U; break;
        default: break;
    }
    (void)bk_sound_tone(frequency, 90U);
    return 1;
}

static bool clipboard_is_open_by_current_process(void) {
    return win_clipboard_open_pid != 0U &&
           win_clipboard_open_pid == task_current_process_id();
}

static int WIN32_API win32_OpenClipboard(void *window) {
    uint32_t pid = task_current_process_id();
    int result = 0;
    task_preempt_disable();
    if (win_clipboard_open_pid == 0U || win_clipboard_open_pid == pid) {
        win_clipboard_open_pid = pid;
        win_clipboard_open_window = window;
        result = 1;
    }
    task_preempt_enable();
    return result;
}

static int WIN32_API win32_CloseClipboard(void) {
    int result = 0;
    task_preempt_disable();
    if (clipboard_is_open_by_current_process()) {
        win_clipboard_open_pid = 0U;
        win_clipboard_open_window = NULL;
        result = 1;
    }
    task_preempt_enable();
    return result;
}

static int WIN32_API win32_EmptyClipboard(void) {
    if (!clipboard_is_open_by_current_process()) return 0;
    for (uint32_t i = 0; i < WIN32_CLIPBOARD_SLOTS; i++) {
        void *handle = win_clipboard[i].handle;
        if (handle && win32_global_handle_valid(handle))
            win32_global_release_handle(handle);
        win_clipboard[i].format = 0U;
        win_clipboard[i].handle = NULL;
    }
    return 1;
}

static void *WIN32_API win32_SetClipboardData(uint32_t format, void *handle) {
    win_clipboard_entry_t *free_entry = NULL;
    if (!clipboard_is_open_by_current_process() || !format || !handle) return NULL;
    for (uint32_t i = 0; i < WIN32_CLIPBOARD_SLOTS; i++) {
        if (win_clipboard[i].format == format) {
            if (win_clipboard[i].handle != handle &&
                win32_global_handle_valid(win_clipboard[i].handle))
                win32_global_release_handle(win_clipboard[i].handle);
            win_clipboard[i].handle = handle;
            return handle;
        }
        if (!free_entry && win_clipboard[i].format == 0U)
            free_entry = &win_clipboard[i];
    }
    if (!free_entry) return NULL;
    free_entry->format = format;
    free_entry->handle = handle;
    return handle;
}

static void *WIN32_API win32_GetClipboardData(uint32_t format) {
    if (!clipboard_is_open_by_current_process()) return NULL;
    for (uint32_t i = 0; i < WIN32_CLIPBOARD_SLOTS; i++)
        if (win_clipboard[i].format == format) return win_clipboard[i].handle;
    return NULL;
}

static int WIN32_API win32_IsClipboardFormatAvailable(uint32_t format) {
    for (uint32_t i = 0; i < WIN32_CLIPBOARD_SLOTS; i++)
        if (win_clipboard[i].format == format && win_clipboard[i].handle)
            return 1;
    return 0;
}

static int WIN32_API win32_GetSystemMetrics(int index){gui_desktop_t*d=gui_get_desktop();switch(index){case 0:return d?d->surface.width:800;case 1:return d?d->surface.height:600;case 2:return 16;case 3:return 16;case 4:return GUI_TITLEBAR_HEIGHT;case 5:case 6:return GUI_BORDER_SIZE;case 15:return 1;case 16:case 17:return 16;case 30:case 31:return 1;default:return 0;}}
static int WIN32_API win32_GetClassNameA(void*hwnd,char*out,int size){win_window_t*w=window_from_handle(hwnd);if(!w||!out||size<=0)return 0;kstrncpy(out,w->class_name,(size_t)size-1U);out[size-1]='\0';return(int)kstrlen(out);}
static void*WIN32_API win32_FindWindowA(const char*class_name,const char*title){for(uint32_t i=0;i<WIN32_MAX_WINDOWS;i++){win_window_t*w=&win_windows[i];if(!w->used||w->control)continue;if(class_name&& !equal_ci(class_name,w->class_name))continue;if(title&& !equal(title,w->native->title))continue;return(void*)(uintptr_t)(HWND_BASE+i);}return NULL;}
static int32_t WIN32_API win32_GetWindowLongA(void*hwnd,int index){win_window_t*w=window_from_handle(hwnd);if(!w)return 0;switch(index){case GWL_WNDPROC:return(int32_t)(uintptr_t)w->proc;case GWL_HINSTANCE:return(int32_t)(uintptr_t)w->instance;case GWL_HWNDPARENT:return(int32_t)(uintptr_t)w->parent;case GWL_ID:return(int32_t)w->id;case GWL_STYLE:return(int32_t)w->style;case GWL_EXSTYLE:return(int32_t)w->exstyle;case GWL_USERDATA:return w->user_data;default:return 0;}}
static int32_t WIN32_API win32_SetWindowLongA(void*hwnd,int index,int32_t value){win_window_t*w=window_from_handle(hwnd);int32_t old;if(!w)return 0;old=win32_GetWindowLongA(hwnd,index);switch(index){case GWL_WNDPROC:w->proc=(wndproc_t)(uintptr_t)value;break;case GWL_HINSTANCE:w->instance=(void*)(uintptr_t)value;break;case GWL_HWNDPARENT:w->parent=(void*)(uintptr_t)value;break;case GWL_ID:w->id=(uint32_t)value;break;case GWL_STYLE:w->style=(uint32_t)value;if(w->control&&w->kind==1U)w->edit_readonly=(w->style&ES_READONLY)!=0U;break;case GWL_EXSTYLE:w->exstyle=(uint32_t)value;break;case GWL_USERDATA:w->user_data=value;break;default:return 0;}return old;}
static int32_t WIN32_API win32_GetWindowLongW(void*hwnd,int index){return win32_GetWindowLongA(hwnd,index);}
static int32_t WIN32_API win32_SetWindowLongW(void*hwnd,int index,int32_t value){return win32_SetWindowLongA(hwnd,index,value);}
static int32_t WIN32_API win32_CallWindowProcA(void*proc,void*hwnd,uint32_t msg,uint32_t wp,int32_t lp){return proc?((wndproc_t)proc)(hwnd,msg,wp,lp):0;}
static int WIN32_API win32_SetRect(int32_t*rect,int left,int top,int right,int bottom){if(!rect)return 0;rect[0]=left;rect[1]=top;rect[2]=right;rect[3]=bottom;return 1;}
static int WIN32_API win32_SetRectEmpty(int32_t*rect){return win32_SetRect(rect,0,0,0,0);}
static int WIN32_API win32_IsRectEmpty(const int32_t*rect){return !rect||rect[2]<=rect[0]||rect[3]<=rect[1];}
static int WIN32_API win32_EqualRect(const int32_t*a,const int32_t*b){return a&&b&&a[0]==b[0]&&a[1]==b[1]&&a[2]==b[2]&&a[3]==b[3];}
static int WIN32_API win32_PtInRect(const int32_t*rect,int32_t point_x,int32_t point_y){return rect&&point_x>=rect[0]&&point_x<rect[2]&&point_y>=rect[1]&&point_y<rect[3];}
static int WIN32_API win32_OffsetRect(int32_t*rect,int dx,int dy){if(!rect)return 0;rect[0]+=dx;rect[2]+=dx;rect[1]+=dy;rect[3]+=dy;return 1;}
static int WIN32_API win32_InflateRect(int32_t*rect,int dx,int dy){if(!rect)return 0;rect[0]-=dx;rect[2]+=dx;rect[1]-=dy;rect[3]+=dy;return 1;}
static uint32_t brush_color(void*brush){uint32_t v=(uint32_t)(uintptr_t)brush;if((v&0xFF000000U)==0x74000000U)return v&0x00FFFFFFU;if(v<=32U)return v==1U?0x00FFFFFFU:0x00D8D8D8U;return 0x00D8D8D8U;}
static int WIN32_API win32_FillRect(void*dc,const int32_t*rect,void*brush){return rect&&win32_gdi_fill_rect(dc,rect[0],rect[1],rect[2],rect[3],brush_color(brush));}
static int WIN32_API win32_DrawFocusRect(void*dc,const int32_t*rect){return rect&&win32_gdi_rect(dc,rect[0],rect[1],rect[2],rect[3],0x00000000U);}
static void*WIN32_API win32_GetDlgItem(void*parent,int id){for(uint32_t i=0;i<WIN32_MAX_WINDOWS;i++)if(win_windows[i].used&&win_windows[i].control&&win_windows[i].parent==parent&&win_windows[i].id==(uint32_t)id)return(void*)(uintptr_t)(HWND_BASE+i);return NULL;}
static int WIN32_API win32_IsWindow(void*hwnd){return window_from_handle(hwnd)!=NULL;}
static int WIN32_API win32_DestroyMenu(void *handle);

static win_menu_t *menu_from_handle(void *handle) {
    uint32_t value = (uint32_t)(uintptr_t)handle;
    if (value < MENU_BASE || value >= MENU_BASE + WIN32_MAX_MENUS) return NULL;
    value -= MENU_BASE;
    return win_menus[value].used ? &win_menus[value] : NULL;
}

static void *WIN32_API win32_CreateMenu(void) {
    for (uint32_t i = 0; i < WIN32_MAX_MENUS; i++) {
        if (win_menus[i].used) continue;
        kmemset(&win_menus[i], 0, sizeof(win_menus[i]));
        win_menus[i].used = true;
        return (void *)(uintptr_t)(MENU_BASE + i);
    }
    return NULL;
}

static void *WIN32_API win32_CreatePopupMenu(void) { return win32_CreateMenu(); }

static int WIN32_API win32_AppendMenuA(void *handle, uint32_t flags,
                                       uint32_t id, const char *text) {
    win_menu_t *menu = menu_from_handle(handle);
    if (!menu || menu->count >= WIN32_MAX_MENU_ITEMS) return 0;
    win_menu_item_t *item = &menu->items[menu->count++];
    kmemset(item, 0, sizeof(*item));
    item->id = id;
    item->flags = flags;
    if (flags & MF_POPUP) item->submenu = (void *)(uintptr_t)id;
    kstrncpy(item->text, (flags & MF_SEPARATOR) ? "-" : (text ? text : ""),
             sizeof(item->text) - 1U);
    return 1;
}

static int WIN32_API win32_AppendMenuW(void *handle, uint32_t flags,
                                       uint32_t id, const uint16_t *text) {
    char ansi[64];
    if (flags & MF_SEPARATOR) ansi[0] = '\0';
    else if (!wide_to_ansi(text, ansi, sizeof(ansi))) return 0;
    return win32_AppendMenuA(handle, flags, id, ansi);
}

typedef struct { const uint8_t *cursor, *end; } menu_reader_t;

static bool menu_read_u16(menu_reader_t *reader, uint16_t *value) {
    if (!reader || !value || reader->end - reader->cursor < 2) return false;
    *value = (uint16_t)(reader->cursor[0] | ((uint16_t)reader->cursor[1] << 8));
    reader->cursor += 2;
    return true;
}

static bool menu_read_text(menu_reader_t *reader, char *out, uint32_t size) {
    uint32_t used = 0;
    uint16_t ch;
    if (!reader || !out || !size) return false;
    for (;;) {
        if (!menu_read_u16(reader, &ch)) return false;
        if (!ch) break;
        if (used + 1U < size) out[used++] = ch <= 0xFFU ? (char)ch : '?';
    }
    out[used] = '\0';
    return true;
}

static bool parse_standard_menu_level(menu_reader_t *reader, void *menu,
                                      uint32_t depth) {
    if (!reader || !menu || depth > 8U) return false;
    for (;;) {
        uint16_t option, id = 0;
        char text[64];
        if (!menu_read_u16(reader, &option)) return false;
        if ((option & MF_POPUP) == 0U && !menu_read_u16(reader, &id)) return false;
        if (!menu_read_text(reader, text, sizeof(text))) return false;
        if (option & MF_POPUP) {
            void *popup = win32_CreatePopupMenu();
            if (!popup || !parse_standard_menu_level(reader, popup, depth + 1U) ||
                !win32_AppendMenuA(menu, option | MF_POPUP,
                                   (uint32_t)(uintptr_t)popup, text)) return false;
        } else if (!win32_AppendMenuA(menu, option, id, text)) return false;
        if (option & MF_END) return true;
    }
}

static void *load_menu_resource(void *instance, const void *name, bool wide) {
    void *resource = wide
        ? win32_resource_find_w(instance, (const void *)(uintptr_t)WIN32_RT_MENU,
                                name, 0U, false)
        : win32_resource_find(instance, (const void *)(uintptr_t)WIN32_RT_MENU,
                              name, 0U, false);
    const uint8_t *data = (const uint8_t *)win32_resource_lock(resource);
    uint32_t size = win32_resource_size(instance, resource);
    if (!data || size < 4U) return NULL;
    uint16_t version = (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
    uint16_t header = (uint16_t)(data[2] | ((uint16_t)data[3] << 8));
    if (version != 0U || 4U + header > size) return NULL; /* MENUEX queda para otra etapa. */
    void *menu = win32_CreateMenu();
    if (!menu) return NULL;
    menu_reader_t reader = { data + 4U + header, data + size };
    if (!parse_standard_menu_level(&reader, menu, 0U)) {
        win32_DestroyMenu(menu);
        return NULL;
    }
    return menu;
}

static void *WIN32_API win32_LoadMenuA(void *instance, const char *name) {
    return load_menu_resource(instance, name, false);
}

static void *WIN32_API win32_LoadMenuW(void *instance, const uint16_t *name) {
    return load_menu_resource(instance, name, true);
}

static void menu_callback(gui_window_t *native UNUSED, uint32_t id,
                          void *context) {
    win_window_t *window = (win_window_t *)context;
    void *hwnd = (void *)(uintptr_t)(HWND_BASE + (uint32_t)(window - win_windows));
    queue_message(hwnd, WM_INITMENU,
                  (uint32_t)(uintptr_t)window->menu_handle, 0);
    win_menu_t *root = menu_from_handle(window->menu_handle);
    if (root) {
        for (uint32_t i = 0; i < root->count; i++) {
            win_menu_t *popup = menu_from_handle(root->items[i].submenu);
            if (!popup) continue;
            for (uint32_t j = 0; j < popup->count; j++) {
                if (popup->items[j].id != id) continue;
                queue_message(hwnd, WM_INITMENUPOPUP,
                    (uint32_t)(uintptr_t)root->items[i].submenu,
                    (int32_t)i);
                i = root->count;
                break;
            }
        }
    }
    queue_message(hwnd, WM_COMMAND, id, 0);
}

static int WIN32_API win32_SetMenu(void *hwnd, void *handle) {
    win_window_t *window = window_from_handle(hwnd);
    win_menu_t *root = menu_from_handle(handle);
    if (!window || window->control || !root) return 0;
    window->menu_handle = handle;
    window->native->menu_count = 0;
    for (uint32_t i = 0; i < root->count; i++) {
        win_menu_item_t *top = &root->items[i];
        win_menu_t *popup = menu_from_handle(top->submenu);
        int index = gui_window_add_menu(window->native, top->text);
        if (index < 0) continue;
        if (popup) {
            for (uint32_t j = 0; j < popup->count; j++) {
                win_menu_item_t *item = &popup->items[j];
                gui_window_add_menu_item(window->native, index, item->id,
                    (item->flags & MF_SEPARATOR) ? "-" : item->text,
                    menu_callback, window);
            }
        }
    }
    window->native->dirty = true;
    gui_request_paint();
    return 1;
}

static void *WIN32_API win32_GetMenu(void *hwnd) {
    win_window_t *window = window_from_handle(hwnd);
    return window ? window->menu_handle : NULL;
}

static int WIN32_API win32_DrawMenuBar(void *hwnd) { return win32_UpdateWindow(hwnd); }
static win_menu_item_t *menu_find_item(win_menu_t *menu,uint32_t item,uint32_t flags){if(!menu)return NULL;if(flags&MF_BYPOSITION)return item<menu->count?&menu->items[item]:NULL;for(uint32_t i=0;i<menu->count;i++){if(menu->items[i].id==item)return &menu->items[i];win_menu_t*sub=menu_from_handle(menu->items[i].submenu);win_menu_item_t*found=menu_find_item(sub,item,flags);if(found)return found;}return NULL;}
static uint32_t WIN32_API win32_CheckMenuItem(void *handle,uint32_t item,uint32_t flags){win_menu_t*menu=menu_from_handle(handle);win_menu_item_t*entry=menu_find_item(menu,item,flags);if(!entry)return 0xFFFFFFFFU;uint32_t old=entry->flags&(MF_CHECKED|MF_DISABLED|MF_GRAYED);entry->flags=(entry->flags&~MF_CHECKED)|(flags&MF_CHECKED);for(uint32_t i=0;i<WIN32_MAX_WINDOWS;i++)if(win_windows[i].used&&win_windows[i].menu_handle==handle)win32_SetMenu((void*)(uintptr_t)(HWND_BASE+i),handle);return old;}

static void refresh_all_attached_menus(void) {
    for (uint32_t i = 0; i < WIN32_MAX_WINDOWS; i++) {
        if (!win_windows[i].used || !win_windows[i].menu_handle) continue;
        win32_SetMenu((void *)(uintptr_t)(HWND_BASE + i),
                      win_windows[i].menu_handle);
    }
}

static int WIN32_API win32_SetMenuItemInfoA(void *handle, uint32_t item,
                                             int by_position,
                                             const win_menu_item_info_a_t *info) {
    win_menu_t *menu = menu_from_handle(handle);
    win_menu_item_t *entry;
    uint32_t mask;
    if (!menu || !info || info->cbSize < 44U) return 0;
    entry = by_position
        ? (item < menu->count ? &menu->items[item] : NULL)
        : menu_find_item(menu, item, 0U);
    if (!entry) return 0;
    mask = info->fMask;
    if (mask & MIIM_STATE) {
        entry->flags &= ~(MF_CHECKED | MF_DISABLED | MF_GRAYED);
        if (info->fState & MFS_CHECKED) entry->flags |= MF_CHECKED;
        if (info->fState & MFS_DISABLED)
            entry->flags |= MF_DISABLED | MF_GRAYED;
    }
    if (mask & MIIM_ID) entry->id = info->wID;
    if (mask & MIIM_SUBMENU) {
        entry->submenu = info->hSubMenu;
        if (info->hSubMenu) entry->flags |= MF_POPUP;
        else entry->flags &= ~MF_POPUP;
    }
    if (mask & MIIM_FTYPE) {
        if (info->fType & MFT_SEPARATOR) {
            entry->flags |= MF_SEPARATOR;
            kstrncpy(entry->text, "-", sizeof(entry->text) - 1U);
        } else entry->flags &= ~MF_SEPARATOR;
    }
    if ((mask & (MIIM_STRING | MIIM_TYPE)) && info->dwTypeData) {
        kstrncpy(entry->text, info->dwTypeData, sizeof(entry->text) - 1U);
        entry->text[sizeof(entry->text) - 1U] = '\0';
    }
    refresh_all_attached_menus();
    return 1;
}



static int WIN32_API win32_GetMenuItemCount(void *handle) {win_menu_t*m=menu_from_handle(handle);return m?(int)m->count:-1;}
static void *WIN32_API win32_GetSubMenu(void *handle,int position){win_menu_t*m=menu_from_handle(handle);return m&&position>=0&&(uint32_t)position<m->count?m->items[position].submenu:NULL;}
static int WIN32_API win32_EnableMenuItem(void*handle,uint32_t item,uint32_t flags){win_menu_t*m=menu_from_handle(handle);win_menu_item_t*e=menu_find_item(m,item,flags);if(!e)return -1;uint32_t old=e->flags&(MF_DISABLED|MF_GRAYED);e->flags&=~(MF_DISABLED|MF_GRAYED);if(flags&(MF_DISABLED|MF_GRAYED))e->flags|=flags&(MF_DISABLED|MF_GRAYED);refresh_all_attached_menus();return(int)old;}
static int WIN32_API win32_CheckMenuRadioItem(void*handle,uint32_t first,uint32_t last,uint32_t check,uint32_t flags){win_menu_t*m=menu_from_handle(handle);if(!m)return 0;for(uint32_t id=first;id<=last;id++){win_menu_item_t*e=menu_find_item(m,id,flags);if(e){e->flags&=~MF_CHECKED;if(id==check)e->flags|=MF_CHECKED;}if(id==0xFFFFFFFFU)break;}refresh_all_attached_menus();return 1;}
static int WIN32_API win32_GetMenuItemInfoA(void*handle,uint32_t item,int by_position,win_menu_item_info_a_t*info){win_menu_t*m=menu_from_handle(handle);win_menu_item_t*e;if(!m||!info||info->cbSize<44U)return 0;e=by_position?(item<m->count?&m->items[item]:NULL):menu_find_item(m,item,0U);if(!e)return 0;if(info->fMask&MIIM_STATE)info->fState=(e->flags&MF_CHECKED?MFS_CHECKED:0U)|((e->flags&(MF_DISABLED|MF_GRAYED))?MFS_DISABLED:0U);if(info->fMask&MIIM_ID)info->wID=e->id;if(info->fMask&MIIM_SUBMENU)info->hSubMenu=e->submenu;if(info->fMask&MIIM_FTYPE)info->fType=(e->flags&MF_SEPARATOR)?MFT_SEPARATOR:0U;if((info->fMask&(MIIM_STRING|MIIM_TYPE))&&info->dwTypeData&&info->cch){kstrncpy(info->dwTypeData,e->text,info->cch-1U);info->dwTypeData[info->cch-1U]='\0';info->cch=(uint32_t)kstrlen(info->dwTypeData);}return 1;}
static int WIN32_API win32_InsertMenuItemA(void*handle,uint32_t item,int by_position,const win_menu_item_info_a_t*info){win_menu_t*m=menu_from_handle(handle);uint32_t pos;if(!m||!info||m->count>=WIN32_MAX_MENU_ITEMS)return 0;pos=by_position?(item<m->count?item:m->count):m->count;for(uint32_t i=m->count;i>pos;i--)m->items[i]=m->items[i-1U];kmemset(&m->items[pos],0,sizeof(m->items[pos]));m->count++;m->items[pos].id=(info->fMask&MIIM_ID)?info->wID:item;if(info->fMask&MIIM_SUBMENU){m->items[pos].submenu=info->hSubMenu;if(info->hSubMenu)m->items[pos].flags|=MF_POPUP;}if((info->fMask&(MIIM_STRING|MIIM_TYPE))&&info->dwTypeData)kstrncpy(m->items[pos].text,info->dwTypeData,sizeof(m->items[pos].text)-1U);if((info->fMask&MIIM_FTYPE)&&(info->fType&MFT_SEPARATOR)){m->items[pos].flags|=MF_SEPARATOR;kstrcpy(m->items[pos].text,"-");}if((info->fMask&MIIM_STATE)&&(info->fState&MFS_DISABLED))m->items[pos].flags|=MF_DISABLED|MF_GRAYED;refresh_all_attached_menus();return 1;}
static int WIN32_API win32_DeleteMenu(void*handle,uint32_t item,uint32_t flags){win_menu_t*m=menu_from_handle(handle);if(!m)return 0;uint32_t pos=0xFFFFFFFFU;if(flags&MF_BYPOSITION){if(item<m->count)pos=item;}else for(uint32_t i=0;i<m->count;i++)if(m->items[i].id==item){pos=i;break;}if(pos==0xFFFFFFFFU)return 0;for(uint32_t i=pos+1U;i<m->count;i++)m->items[i-1U]=m->items[i];m->count--;kmemset(&m->items[m->count],0,sizeof(m->items[m->count]));refresh_all_attached_menus();return 1;}
static uint32_t WIN32_API win32_TrackPopupMenuEx(void*handle,uint32_t flags,int x UNUSED,int y UNUSED,void*owner,void*params UNUSED){win_menu_t*m=menu_from_handle(handle);if(!m)return 0;for(uint32_t i=0;i<m->count;i++){win_menu_item_t*e=&m->items[i];if(e->flags&(MF_DISABLED|MF_GRAYED|MF_SEPARATOR))continue;if(flags&TPM_RETURNCMD)return e->id;if(owner)queue_message(owner,WM_COMMAND,e->id,0);return 1;}return 0;}

static int WIN32_API win32_DestroyMenu(void *handle) {
    win_menu_t *menu = menu_from_handle(handle);
    if (!menu) return 0;
    kmemset(menu, 0, sizeof(*menu));
    return 1;
}


static void *load_accel_resource(void *instance,const void *name,bool wide){void*resource=wide?win32_resource_find_w(instance,(const void*)(uintptr_t)WIN32_RT_ACCELERATOR,name,0U,false):win32_resource_find(instance,(const void*)(uintptr_t)WIN32_RT_ACCELERATOR,name,0U,false);const uint8_t*data=(const uint8_t*)win32_resource_lock(resource);uint32_t size=win32_resource_size(instance,resource);if(!data||size<8U)return NULL;for(uint32_t i=0;i<WIN32_MAX_ACCELS;i++)if(!win_accels[i].used){win_accel_t*a=&win_accels[i];kmemset(a,0,sizeof(*a));a->used=true;for(uint32_t off=0;off+8U<=size&&a->count<WIN32_MAX_ACCEL_ITEMS;off+=8U){uint16_t flags=(uint16_t)(data[off]|((uint16_t)data[off+1]<<8));win_accel_item_t*e=&a->items[a->count++];e->flags=(uint8_t)flags;e->key=(uint16_t)(data[off+2]|((uint16_t)data[off+3]<<8));e->command=(uint16_t)(data[off+4]|((uint16_t)data[off+5]<<8));if(flags&ACCEL_END)break;}return(void*)(uintptr_t)(ACCEL_BASE+i);}return NULL;}
static void *WIN32_API win32_LoadAcceleratorsA(void*instance,const char*name){return load_accel_resource(instance,name,false);}
static void *WIN32_API win32_LoadAcceleratorsW(void*instance,const uint16_t*name){return load_accel_resource(instance,name,true);}
static int WIN32_API win32_DestroyAcceleratorTable(void*handle){win_accel_t*a=accel_from_handle(handle);if(!a)return 0;kmemset(a,0,sizeof(*a));return 1;}
static int WIN32_API win32_TranslateAcceleratorA(void*hwnd,void*handle,const winmsg_t*msg){win_accel_t*a=accel_from_handle(handle);if(!a||!msg||(msg->message!=WM_KEYDOWN&&msg->message!=WM_CHAR))return 0;uint16_t key=(uint16_t)msg->wparam;for(uint32_t i=0;i<a->count;i++){win_accel_item_t*e=&a->items[i];bool keymsg=(e->flags&FVIRTKEY)!=0U;if((keymsg&&msg->message!=WM_KEYDOWN)||(!keymsg&&msg->message!=WM_CHAR)||e->key!=key)continue;win32_SendMessageA(hwnd,WM_COMMAND,e->command,0);return 1;}return 0;}
static int WIN32_API win32_TranslateAcceleratorW(void*hwnd,void*handle,const winmsg_t*msg){return win32_TranslateAcceleratorA(hwnd,handle,msg);}
static void *next_dialog_control(void*dialog,void*current,bool reverse){int start=-1;for(uint32_t i=0;i<WIN32_MAX_WINDOWS;i++)if(win_windows[i].used&&(void*)(uintptr_t)(HWND_BASE+i)==current){start=(int)i;break;}for(uint32_t step=1;step<=WIN32_MAX_WINDOWS;step++){int idx=reverse?start-(int)step:start+(int)step;while(idx<0)idx+=WIN32_MAX_WINDOWS;idx%=WIN32_MAX_WINDOWS;win_window_t*w=&win_windows[idx];if(w->used&&w->control&&w->visible&&w->parent==dialog&&w->enabled)return(void*)(uintptr_t)(HWND_BASE+(uint32_t)idx);}return NULL;}
static int WIN32_API win32_IsDialogMessageA(void*dialog,winmsg_t*msg){win_window_t*d=window_from_handle(dialog);if(!d||!d->dialog||!msg)return 0;if(msg->message==WM_KEYDOWN&&msg->wparam==VK_TAB){void*next=next_dialog_control(dialog,win32_GetFocus(),false);if(next)win32_SetFocus(next);return 1;}if(msg->message==WM_KEYDOWN&&msg->wparam==VK_RETURN){void*focus=win32_GetFocus();win_window_t*w=window_from_handle(focus);if(w&&w->parent==dialog&&w->kind==2U)win32_SendMessageA(dialog,WM_COMMAND,w->id,(int32_t)(uintptr_t)focus);else win32_SendMessageA(dialog,WM_COMMAND,WIN32_IDOK,0);return 1;}if(msg->message==WM_KEYDOWN&&msg->wparam==VK_ESCAPE){win32_SendMessageA(dialog,WM_COMMAND,WIN32_IDCANCEL,0);return 1;}return 0;}
static int WIN32_API win32_IsDialogMessageW(void*dialog,winmsg_t*msg){return win32_IsDialogMessageA(dialog,msg);}

typedef struct { const uint8_t *cursor, *end; } dialog_reader_t;
typedef struct { uint8_t kind; uint16_t ordinal; char text[96]; } dialog_field_t;

static bool dialog_read_u16(dialog_reader_t *reader, uint16_t *value) {
    if (!reader || !value || reader->end - reader->cursor < 2) return false;
    *value = (uint16_t)(reader->cursor[0] | ((uint16_t)reader->cursor[1] << 8));
    reader->cursor += 2;
    return true;
}

static bool dialog_read_s16(dialog_reader_t *reader, int16_t *value) {
    uint16_t raw;
    if (!dialog_read_u16(reader, &raw)) return false;
    *value = (int16_t)raw;
    return true;
}

static bool dialog_read_u32(dialog_reader_t *reader, uint32_t *value) {
    uint16_t low, high;
    if (!dialog_read_u16(reader, &low) || !dialog_read_u16(reader, &high))
        return false;
    *value = (uint32_t)low | ((uint32_t)high << 16);
    return true;
}

static bool dialog_align4(dialog_reader_t *reader) {
    uintptr_t aligned = ((uintptr_t)reader->cursor + 3U) & ~(uintptr_t)3U;
    if (aligned > (uintptr_t)reader->end) return false;
    reader->cursor = (const uint8_t *)aligned;
    return true;
}

static bool dialog_read_field(dialog_reader_t *reader, dialog_field_t *field) {
    uint16_t first;
    if (!reader || !field || !dialog_read_u16(reader, &first)) return false;
    kmemset(field, 0, sizeof(*field));
    if (first == 0U) return true;
    if (first == 0xFFFFU) {
        field->kind = 1U;
        return dialog_read_u16(reader, &field->ordinal);
    }
    field->kind = 2U;
    uint32_t used = 0;
    uint16_t ch = first;
    for (;;) {
        if (used + 1U < sizeof(field->text))
            field->text[used++] = ch <= 0xFFU ? (char)ch : '?';
        if (!dialog_read_u16(reader, &ch)) return false;
        if (!ch) break;
    }
    field->text[used] = '\0';
    return true;
}

static const char *dialog_class_name(const dialog_field_t *field) {
    if (!field) return "STATIC";
    if (field->kind == 2U) return field->text;
    if (field->kind != 1U) return "STATIC";
    switch (field->ordinal) {
        case 0x0080U: return "BUTTON";
        case 0x0081U: return "EDIT";
        case 0x0082U: return "STATIC";
        case 0x0083U: return "LISTBOX";
        case 0x0085U: return "COMBOBOX";
        default: return "STATIC";
    }
}

static void *create_dialog_from_template(void *instance, const uint8_t *data,
                                         uint32_t size, void *parent,
                                         wndproc_t dialog_proc,
                                         int32_t init_param) {
    dialog_reader_t reader = { data, data + size };
    uint32_t style, exstyle;
    uint16_t item_count;
    int16_t x, y, cx, cy;
    dialog_field_t menu_field, class_field, title_field;
    wndclass_a_t saved_class = registered_class;
    char saved_name[sizeof(registered_name)];
    void *hwnd = NULL;

    if (!data || size < 18U || !dialog_proc) return NULL;
    kstrncpy(saved_name, registered_name, sizeof(saved_name) - 1U);
    saved_name[sizeof(saved_name) - 1U] = '\0';

    /* DIALOGEX comienza con dlgVer=1, signature=0xffff. */
    if (size >= 4U && data[0] == 1U && data[1] == 0U &&
        data[2] == 0xFFU && data[3] == 0xFFU) return NULL;
    if (!dialog_read_u32(&reader, &style) ||
        !dialog_read_u32(&reader, &exstyle) ||
        !dialog_read_u16(&reader, &item_count) ||
        !dialog_read_s16(&reader, &x) || !dialog_read_s16(&reader, &y) ||
        !dialog_read_s16(&reader, &cx) || !dialog_read_s16(&reader, &cy) ||
        !dialog_read_field(&reader, &menu_field) ||
        !dialog_read_field(&reader, &class_field) ||
        !dialog_read_field(&reader, &title_field)) return NULL;
    if (style & DS_SETFONT) {
        uint16_t point_size;
        dialog_field_t font_name;
        if (!dialog_read_u16(&reader, &point_size) ||
            !dialog_read_field(&reader, &font_name)) return NULL;
        (void)point_size;
    }

    kmemset(&registered_class, 0, sizeof(registered_class));
    registered_class.proc = dialog_proc;
    kstrncpy(registered_name, "#32770", sizeof(registered_name) - 1U);
    registered_class.name = registered_name;
    int px = x > 0 ? x * 2 : 80;
    int py = y > 0 ? y * 2 : 60;
    int width = cx > 0 ? cx * 2 : 300;
    int height = cy > 0 ? cy * 2 : 180;
    hwnd = win32_CreateWindowExA(exstyle, "#32770", title_field.text,
                                 style, px, py, width, height,
                                 parent, NULL, instance,
                                 (void *)(uintptr_t)init_param);
    registered_class = saved_class;
    kstrncpy(registered_name, saved_name, sizeof(registered_name) - 1U);
    registered_class.name = registered_name;
    if (!hwnd) return NULL;

    win_window_t *dialog = window_from_handle(hwnd);
    dialog->dialog = true;
    dialog->proc = dialog_proc;
    if (menu_field.kind) {
        void *menu = menu_field.kind == 1U
            ? win32_LoadMenuA(instance,
                (const char *)(uintptr_t)menu_field.ordinal)
            : win32_LoadMenuA(instance, menu_field.text);
        if (menu) win32_SetMenu(hwnd, menu);
    }

    void *first_control = NULL;
    for (uint16_t item = 0; item < item_count; item++) {
        uint32_t item_style, item_exstyle;
        int16_t ix, iy, icx, icy;
        uint16_t id, extra;
        dialog_field_t item_class, item_title;
        if (!dialog_align4(&reader) ||
            !dialog_read_u32(&reader, &item_style) ||
            !dialog_read_u32(&reader, &item_exstyle) ||
            !dialog_read_s16(&reader, &ix) || !dialog_read_s16(&reader, &iy) ||
            !dialog_read_s16(&reader, &icx) || !dialog_read_s16(&reader, &icy) ||
            !dialog_read_u16(&reader, &id) ||
            !dialog_read_field(&reader, &item_class) ||
            !dialog_read_field(&reader, &item_title) ||
            !dialog_read_u16(&reader, &extra)) {
            cleanup_window(hwnd);
            return NULL;
        }
        if ((uint32_t)(reader.end - reader.cursor) < extra) {
            cleanup_window(hwnd);
            return NULL;
        }
        reader.cursor += extra;
        const char *control_class = dialog_class_name(&item_class);
        if (!equal(control_class, "BUTTON") &&
            !equal(control_class, "EDIT") &&
            !equal(control_class, "STATIC")) control_class = "STATIC";
        void *control = win32_CreateWindowExA(item_exstyle, control_class,
            item_title.kind == 2U ? item_title.text : "", item_style,
            ix * 2, iy * 2, icx * 2, icy * 2, hwnd,
            (void *)(uintptr_t)id, instance, NULL);
        if (!first_control && control) first_control = control;
    }
    if (first_control) win32_SetFocus(first_control);
    dialog_proc(hwnd, WM_INITDIALOG, (uint32_t)(uintptr_t)first_control,
                init_param);
    return hwnd;
}

static void *load_dialog_resource(void *instance, const void *name, bool wide,
                                  void *parent, wndproc_t proc,
                                  int32_t init_param) {
    void *resource = wide
        ? win32_resource_find_w(instance,
            (const void *)(uintptr_t)WIN32_RT_DIALOG, name, 0U, false)
        : win32_resource_find(instance,
            (const void *)(uintptr_t)WIN32_RT_DIALOG, name, 0U, false);
    const uint8_t *data = (const uint8_t *)win32_resource_lock(resource);
    uint32_t size = win32_resource_size(instance, resource);
    return create_dialog_from_template(instance, data, size, parent, proc,
                                       init_param);
}

static void *WIN32_API win32_CreateDialogParamA(void *instance,
                                                const char *template_name,
                                                void *parent, void *proc,
                                                int32_t init_param) {
    return load_dialog_resource(instance, template_name, false, parent,
                                (wndproc_t)proc, init_param);
}

static void *WIN32_API win32_CreateDialogParamW(void *instance,
                                                const uint16_t *template_name,
                                                void *parent, void *proc,
                                                int32_t init_param) {
    return load_dialog_resource(instance, template_name, true, parent,
                                (wndproc_t)proc, init_param);
}

static int WIN32_API win32_EndDialog(void *hwnd, int result) {
    win_window_t *dialog = window_from_handle(hwnd);
    if (!dialog || !dialog->dialog) return 0;
    dialog->dialog_result = result;
    dialog->dialog_done = true;
    if (dialog->native) dialog->native->visible = false;
    gui_request_paint();
    return 1;
}

static int32_t dialog_box_common(void *hwnd) {
    win_window_t *dialog = window_from_handle(hwnd);
    if (!dialog) return -1;
    while ((dialog = window_from_handle(hwnd)) && !dialog->dialog_done &&
           !task_exit_requested()) {
        winmsg_t message;
        int result = win32_GetMessageA(&message, NULL, 0U, 0U);
        if (result < 0) break;
        if (result == 0) continue;
        win32_TranslateMessage(&message);
        win32_DispatchMessageA(&message);
    }
    dialog = window_from_handle(hwnd);
    int32_t result = dialog ? dialog->dialog_result : WIN32_IDCANCEL;
    if (dialog) {
        if (dialog->proc) dialog->proc(hwnd, WM_DESTROY, 0U, 0);
        cleanup_window(hwnd);
    }
    return result;
}

static int32_t WIN32_API win32_DialogBoxParamA(void *instance,
                                               const char *template_name,
                                               void *parent, void *proc,
                                               int32_t init_param) {
    void *hwnd = win32_CreateDialogParamA(instance, template_name, parent,
                                          proc, init_param);
    return hwnd ? dialog_box_common(hwnd) : -1;
}

static int32_t WIN32_API win32_DialogBoxParamW(void *instance,
                                               const uint16_t *template_name,
                                               void *parent, void *proc,
                                               int32_t init_param) {
    void *hwnd = win32_CreateDialogParamW(instance, template_name, parent,
                                          proc, init_param);
    return hwnd ? dialog_box_common(hwnd) : -1;
}

static int WIN32_API win32_SetDlgItemTextA(void *dialog, int id,
                                           const char *text) {
    return win32_SetWindowTextA(win32_GetDlgItem(dialog, id), text);
}

static int WIN32_API win32_GetDlgItemTextA(void *dialog, int id,
                                           char *text, int size) {
    return win32_GetWindowTextA(win32_GetDlgItem(dialog, id), text, size);
}

static int WIN32_API win32_SetDlgItemTextW(void *dialog, int id,
                                           const uint16_t *text) {
    char ansi[1024];
    return wide_to_ansi(text, ansi, sizeof(ansi))
        ? win32_SetDlgItemTextA(dialog, id, ansi) : 0;
}

static int WIN32_API win32_GetDlgItemTextW(void *dialog, int id,
                                           uint16_t *text, int size) {
    char ansi[1024];
    int length = win32_GetDlgItemTextA(dialog, id, ansi, sizeof(ansi));
    if (!text || size <= 0) return 0;
    return (int)ansi_to_wide(ansi, text, (uint32_t)size < (uint32_t)length + 1U
                             ? (uint32_t)size : (uint32_t)length + 1U);
}

static int32_t WIN32_API win32_SendDlgItemMessageA(void *dialog, int id,
                                                   uint32_t message,
                                                   uint32_t wparam,
                                                   int32_t lparam) {
    return win32_SendMessageA(win32_GetDlgItem(dialog, id), message,
                              wparam, lparam);
}

static int32_t WIN32_API win32_SendDlgItemMessageW(void *dialog, int id,
                                                   uint32_t message,
                                                   uint32_t wparam,
                                                   int32_t lparam) {
    return win32_SendDlgItemMessageA(dialog, id, message, wparam, lparam);
}

static int WIN32_API win32_CheckDlgButton(void *dialog, int id,
                                          uint32_t check) {
    return win32_SendDlgItemMessageA(dialog, id, BM_SETCHECK, check, 0) == 0;
}

static uint32_t WIN32_API win32_IsDlgButtonChecked(void *dialog, int id) {
    return (uint32_t)win32_SendDlgItemMessageA(dialog, id, BM_GETCHECK, 0U, 0);
}

static uint32_t WIN32_API win32_SetTimer(void*hwnd,uint32_t id,uint32_t interval,void*callback){if(!interval)interval=10U;for(uint32_t i=0;i<8U;i++)if(!win_timers[i].used){uint32_t hz=pit_get_frequency_hz();win_timers[i]=(win_timer_t){true,hwnd,id?id:i+1U,interval,pit_get_ticks()+(interval*(hz?hz:100U)+999U)/1000U,callback};return win_timers[i].id;}return 0;}
static int WIN32_API win32_KillTimer(void*hwnd,uint32_t id){for(uint32_t i=0;i<8U;i++)if(win_timers[i].used&&win_timers[i].hwnd==hwnd&&win_timers[i].id==id){win_timers[i].used=false;return 1;}return 0;}
static void box_paint(gui_window_t *window UNUSED, gui_surface_t *surface, void *context) {
    win32_message_box_t *box = (win32_message_box_t *)context;
    gui_rect_t client;
    if (!box || !box->window || !box->window->visible) return;
    client = gui_window_content_rect(box->window);
    gui_font_draw_string_clipped(surface, client.x + 18, client.y + 22,
                                 box->text, 0x00101010U, client);
}
static void box_ok(gui_window_t *window, uint32_t id UNUSED) {
    win32_message_box_t *box = window ? (win32_message_box_t *)window->content_context : NULL;
    if (box) box->result = WIN32_IDOK;
    if (window) gui_window_close(window);
}
static int WIN32_API win32_MessageBoxA(void *owner UNUSED, const char *text,
                                       const char *caption, uint32_t type UNUSED) {
    win32_message_box_t box;
    gui_desktop_t *desktop = gui_get_desktop();
    int width = 360, height = 145, x, y;
    if (!desktop) return 0;
    x = ((int)desktop->surface.width - width) / 2;
    y = ((int)desktop->surface.height - height) / 2;
    box.window = gui_desktop_create_window(desktop, x < 0 ? 0 : x, y < 0 ? 0 : y,
                                            width, height, caption ? caption : "Message");
    if (!box.window) return 0;
    box.text = text ? text : ""; box.result = 0;
    box.window->owner_pid = task_current_pid(); box.window->listed = false;
    gui_window_set_content(box.window, box_paint, &box);
    if (!gui_widget_create_button(desktop, box.window, (gui_rect_t){140, 78, 76, 26}, "OK", box_ok)) {
        gui_desktop_remove_window(desktop, box.window); gui_window_destroy(box.window); return 0;
    }
    gui_desktop_focus_window(desktop, box.window); gui_request_paint();
    while (box.window->visible && !task_exit_requested()) task_sleep(1U);
    if (!box.result) box.result = WIN32_IDOK;
    gui_desktop_remove_window(desktop, box.window); gui_window_destroy(box.window); gui_request_paint();
    return box.result;
}



typedef struct {
    bool done, accepted;
    char *buffer;
    uint32_t capacity;
    void *edit;
} win_path_prompt_t;

typedef struct {
    uint32_t lStructSize;
    void *hwndOwner;
    void *hInstance;
    uint32_t Flags;
    char *lpstrFindWhat;
    char *lpstrReplaceWith;
    uint16_t wFindWhatLen;
    uint16_t wReplaceWithLen;
    int32_t lCustData;
    void *lpfnHook;
    const char *lpTemplateName;
} win_find_replace_a_t;

typedef struct {
    win_find_replace_a_t *find;
    void *owner;
    uint32_t notify_message;
    bool replace_mode;
    void *find_edit, *replace_edit, *match_case, *whole_word;
} win_find_dialog_t;

#define COMMON_ID_PATH 1001U
#define COMMON_ID_FIND 1002U
#define COMMON_ID_REPLACE 1003U
#define COMMON_ID_MATCH 1004U
#define COMMON_ID_WHOLE 1005U
#define COMMON_ID_FINDNEXT 1010U
#define COMMON_ID_REPLACEONE 1011U
#define COMMON_ID_REPLACEALL 1012U
#define FR_FINDNEXT 0x00000008U
#define FR_REPLACE 0x00000010U
#define FR_REPLACEALL 0x00000020U
#define FR_DIALOGTERM 0x00000040U

static void *win32_create_internal_dialog(const char *title, int width, int height,
                                           wndproc_t proc, void *context) {
    gui_desktop_t *desktop = gui_get_desktop();
    int x, y;
    if (!desktop || !proc) return NULL;
    x = ((int)desktop->surface.width - width) / 2;
    y = ((int)desktop->surface.height - height) / 2;
    for (uint32_t i = 0; i < WIN32_MAX_WINDOWS; i++) {
        win_window_t *w;
        void *hwnd;
        if (win_windows[i].used) continue;
        w = &win_windows[i];
        hwnd = (void *)(uintptr_t)(HWND_BASE + i);
        kmemset(w, 0, sizeof(*w));
        w->used = true; w->enabled = true; w->visible = true; w->dialog = true;
        w->proc = proc; w->user_data = (int32_t)(uintptr_t)context;
        kstrcpy(w->class_name, "#32770");
        w->native = gui_desktop_create_window(desktop, x < 0 ? 0 : x,
            y < 0 ? 0 : y, width, height, title ? title : "Dialog");
        if (!w->native) { kmemset(w, 0, sizeof(*w)); return NULL; }
        w->native->owner_pid = task_current_pid();
        w->native->listed = false;
        gui_window_set_content(w->native, normal_window_paint, w);
        gui_window_set_event_handler(w->native, normal_window_event, w);
        if (!proc(hwnd, WM_NCCREATE, 0U, 0) ||
            proc(hwnd, WM_CREATE, 0U, 0) == -1) {
            cleanup_window(hwnd); return NULL;
        }
        gui_desktop_focus_window(desktop, w->native);
        queue_message(hwnd, WM_PAINT, 0U, 0);
        gui_request_paint();
        return hwnd;
    }
    return NULL;
}

static int32_t WIN32_API path_prompt_proc(void *hwnd, uint32_t msg,
                                          uint32_t wp, int32_t lp UNUSED) {
    win_window_t *window = window_from_handle(hwnd);
    win_path_prompt_t *state = window
        ? (win_path_prompt_t *)(uintptr_t)(uint32_t)window->user_data : NULL;
    if (msg == WM_NCCREATE) return 1;
    if (!window || !state) return 0;
    if (msg == WM_CREATE) {
        (void)win32_CreateWindowExA(0U, "STATIC", "Path:", WS_CHILD | WS_VISIBLE,
            12, 12, 360, 18, hwnd, (void *)1000U, NULL, NULL);
        state->edit = win32_CreateWindowExA(0U, "EDIT", state->buffer,
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER,
            12, 34, 430, 24, hwnd, (void *)COMMON_ID_PATH, NULL, NULL);
        (void)win32_CreateWindowExA(0U, "BUTTON", "OK", WS_CHILD | WS_VISIBLE,
            274, 72, 78, 26, hwnd, (void *)WIN32_IDOK, NULL, NULL);
        (void)win32_CreateWindowExA(0U, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE,
            364, 72, 78, 26, hwnd, (void *)WIN32_IDCANCEL, NULL, NULL);
        if (state->edit) {
            win32_SetFocus(state->edit);
            win32_SendMessageA(state->edit, EM_SETSEL, 0U, -1);
        }
        return 0;
    }
    if (msg == WM_COMMAND) {
        if ((wp & 0xFFFFU) == WIN32_IDOK) {
            if (state->edit)
                win32_GetWindowTextA(state->edit, state->buffer, (int)state->capacity);
            state->accepted = state->buffer && state->buffer[0] != '\0';
            state->done = true; return 0;
        }
        if ((wp & 0xFFFFU) == WIN32_IDCANCEL) {
            state->accepted = false; state->done = true; return 0;
        }
    }
    if (msg == WM_CLOSE || msg == WM_DESTROY) {
        state->accepted = false; state->done = true; return 0;
    }
    return 0;
}

bool win32_user_path_dialog(const char *title, char *buffer,
                            uint32_t capacity, bool save_mode UNUSED) {
    win_path_prompt_t state;
    winmsg_t message;
    void *hwnd;
    win_window_t *window;
    if (!buffer || capacity < 2U) return false;
    kmemset(&state, 0, sizeof(state));
    state.buffer = buffer; state.capacity = capacity;
    hwnd = win32_create_internal_dialog(title, 470, 135, path_prompt_proc, &state);
    if (!hwnd) return false;
    window = window_from_handle(hwnd);
    while (!state.done && window && window->native && window->native->visible &&
           !task_exit_requested()) {
        if (win32_PeekMessageA(&message, NULL, 0U, 0U, 1U)) {
            if (message.message == WM_QUIT) { state.done = true; state.accepted = false; }
            else win32_DispatchMessageA(&message);
        } else task_sleep(1U);
        window = window_from_handle(hwnd);
    }
    if (window_from_handle(hwnd)) cleanup_window(hwnd);
    return state.accepted;
}

static void find_dialog_copy_values(win_find_dialog_t *state) {
    if (!state || !state->find) return;
    if (state->find_edit && state->find->lpstrFindWhat && state->find->wFindWhatLen)
        win32_GetWindowTextA(state->find_edit, state->find->lpstrFindWhat,
                             state->find->wFindWhatLen);
    if (state->replace_mode && state->replace_edit &&
        state->find->lpstrReplaceWith && state->find->wReplaceWithLen)
        win32_GetWindowTextA(state->replace_edit, state->find->lpstrReplaceWith,
                             state->find->wReplaceWithLen);
    state->find->Flags &= ~(FR_MATCHCASE | FR_WHOLEWORD);
    if (state->match_case && win32_SendMessageA(state->match_case, BM_GETCHECK, 0U, 0))
        state->find->Flags |= FR_MATCHCASE;
    if (state->whole_word && win32_SendMessageA(state->whole_word, BM_GETCHECK, 0U, 0))
        state->find->Flags |= FR_WHOLEWORD;
}

static int32_t WIN32_API find_dialog_proc(void *hwnd, uint32_t msg,
                                          uint32_t wp, int32_t lp UNUSED) {
    win_window_t *window = window_from_handle(hwnd);
    win_find_dialog_t *state = window
        ? (win_find_dialog_t *)(uintptr_t)(uint32_t)window->user_data : NULL;
    if (msg == WM_NCCREATE) return 1;
    if (!window || !state || !state->find) return 0;
    if (msg == WM_CREATE) {
        (void)win32_CreateWindowExA(0U, "STATIC", "Find what:", WS_CHILD | WS_VISIBLE,
            12, 12, 90, 18, hwnd, (void *)1000U, NULL, NULL);
        state->find_edit = win32_CreateWindowExA(0U, "EDIT",
            state->find->lpstrFindWhat ? state->find->lpstrFindWhat : "",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER,
            104, 10, 260, 24, hwnd, (void *)COMMON_ID_FIND, NULL, NULL);
        int y = 42;
        if (state->replace_mode) {
            (void)win32_CreateWindowExA(0U, "STATIC", "Replace with:", WS_CHILD | WS_VISIBLE,
                12, 44, 90, 18, hwnd, (void *)1001U, NULL, NULL);
            state->replace_edit = win32_CreateWindowExA(0U, "EDIT",
                state->find->lpstrReplaceWith ? state->find->lpstrReplaceWith : "",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER,
                104, 42, 260, 24, hwnd, (void *)COMMON_ID_REPLACE, NULL, NULL);
            y = 76;
        }
        state->match_case = win32_CreateWindowExA(0U, "BUTTON", "Match case",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 12, y, 120, 22,
            hwnd, (void *)COMMON_ID_MATCH, NULL, NULL);
        state->whole_word = win32_CreateWindowExA(0U, "BUTTON", "Whole word",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 140, y, 120, 22,
            hwnd, (void *)COMMON_ID_WHOLE, NULL, NULL);
        if (state->find->Flags & FR_MATCHCASE)
            win32_SendMessageA(state->match_case, BM_SETCHECK, BST_CHECKED, 0);
        if (state->find->Flags & FR_WHOLEWORD)
            win32_SendMessageA(state->whole_word, BM_SETCHECK, BST_CHECKED, 0);
        (void)win32_CreateWindowExA(0U, "BUTTON", "Find Next", WS_CHILD | WS_VISIBLE,
            376, 10, 92, 26, hwnd, (void *)COMMON_ID_FINDNEXT, NULL, NULL);
        if (state->replace_mode) {
            (void)win32_CreateWindowExA(0U, "BUTTON", "Replace", WS_CHILD | WS_VISIBLE,
                376, 42, 92, 26, hwnd, (void *)COMMON_ID_REPLACEONE, NULL, NULL);
            (void)win32_CreateWindowExA(0U, "BUTTON", "Replace All", WS_CHILD | WS_VISIBLE,
                376, 74, 92, 26, hwnd, (void *)COMMON_ID_REPLACEALL, NULL, NULL);
        }
        (void)win32_CreateWindowExA(0U, "BUTTON", "Close", WS_CHILD | WS_VISIBLE,
            376, state->replace_mode ? 106 : 74, 92, 26,
            hwnd, (void *)WIN32_IDCANCEL, NULL, NULL);
        if (state->find_edit) { win32_SetFocus(state->find_edit); win32_SendMessageA(state->find_edit, EM_SETSEL, 0U, -1); }
        return 0;
    }
    if (msg == WM_COMMAND) {
        uint32_t id = wp & 0xFFFFU;
        if (id == COMMON_ID_FINDNEXT || id == COMMON_ID_REPLACEONE ||
            id == COMMON_ID_REPLACEALL) {
            find_dialog_copy_values(state);
            state->find->Flags &= ~(FR_FINDNEXT | FR_REPLACE | FR_REPLACEALL | FR_DIALOGTERM);
            state->find->Flags |= id == COMMON_ID_FINDNEXT ? FR_FINDNEXT :
                (id == COMMON_ID_REPLACEONE ? FR_REPLACE : FR_REPLACEALL);
            queue_message(state->owner, state->notify_message, 0U,
                          (int32_t)(uintptr_t)state->find);
            return 0;
        }
        if (id == WIN32_IDCANCEL) { win32_DestroyWindow(hwnd); return 0; }
    }
    if (msg == WM_CLOSE) { win32_DestroyWindow(hwnd); return 0; }
    if (msg == WM_DESTROY) {
        find_dialog_copy_values(state);
        state->find->Flags &= ~(FR_FINDNEXT | FR_REPLACE | FR_REPLACEALL);
        state->find->Flags |= FR_DIALOGTERM;
        queue_message(state->owner, state->notify_message, 0U,
                      (int32_t)(uintptr_t)state->find);
        window->user_data = 0;
        kfree(state);
        return 0;
    }
    return 0;
}

void *win32_user_find_dialog(const char *title, void *owner,
                             uint32_t notify_message, void *find_replace,
                             bool replace_mode) {
    win_find_dialog_t *state = (win_find_dialog_t *)kzalloc(sizeof(*state));
    void *hwnd;
    if (!state || !owner || !find_replace || !notify_message) {
        if (state) kfree(state);
        return NULL;
    }
    state->find = (win_find_replace_a_t *)find_replace;
    state->owner = owner; state->notify_message = notify_message;
    state->replace_mode = replace_mode;
    hwnd = win32_create_internal_dialog(title,
        replace_mode ? 500 : 500, replace_mode ? 175 : 145,
        find_dialog_proc, state);
    if (!hwnd) kfree(state);
    return hwnd;
}

static int WIN32_API win32_CharLowerBuffA(char *text, uint32_t length) {
    if (!text) return 0;
    for (uint32_t i = 0; i < length; i++)
        if (text[i] >= 'A' && text[i] <= 'Z') text[i] += 'a' - 'A';
    return (int)length;
}
static int WIN32_API win32_CharUpperBuffA(char *text, uint32_t length) {
    if (!text) return 0;
    for (uint32_t i = 0; i < length; i++)
        if (text[i] >= 'a' && text[i] <= 'z') text[i] -= 'a' - 'A';
    return (int)length;
}
static char *WIN32_API win32_CharLowerA(char *text) {
    uintptr_t value = (uintptr_t)text;
    if ((value >> 16) == 0U) {
        uint8_t c = (uint8_t)value;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        return (char *)(uintptr_t)c;
    }
    if (text) win32_CharLowerBuffA(text, (uint32_t)kstrlen(text));
    return text;
}
static char *WIN32_API win32_CharUpperA(char *text) {
    uintptr_t value = (uintptr_t)text;
    if ((value >> 16) == 0U) {
        uint8_t c = (uint8_t)value;
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        return (char *)(uintptr_t)c;
    }
    if (text) win32_CharUpperBuffA(text, (uint32_t)kstrlen(text));
    return text;
}
static int WIN32_API win32_IsCharLowerA(uint16_t c) { return c >= 'a' && c <= 'z'; }
static int WIN32_API win32_IsCharUpperA(uint16_t c) { return c >= 'A' && c <= 'Z'; }
static int WIN32_API win32_IsCharAlphaA(uint16_t c) {
    return win32_IsCharLowerA(c) || win32_IsCharUpperA(c);
}
static int WIN32_API win32_CharToOemBuffA(const char *src,char *dst,uint32_t len){if(!src||!dst)return 0;kmemcpy(dst,src,len);return 1;}
static int WIN32_API win32_OemToCharBuffA(const char *src,char *dst,uint32_t len){if(!src||!dst)return 0;kmemcpy(dst,src,len);return 1;}
static int WIN32_API win32_wsprintfA(char *out,const char *format,...) {
    va_list args; int result;
    if(!out||!format)return 0;
    va_start(args,format); result=vsnprintf(out,1024U,format,args); va_end(args);
    return result;
}
static uint32_t WIN32_API win32_GetSysColor(int index) {
    switch(index){
        case 0: return 0x00C0C0C0U; /* scrollbar */
        case 1: return 0x00FFFFFFU; /* background */
        case 2: return 0x00800000U; /* active caption */
        case 5: return 0x00FFFFFFU; /* window */
        case 6: return 0x00000000U; /* window frame */
        case 7: return 0x00000000U; /* menu text */
        case 8: return 0x00000000U; /* window text */
        case 13:return 0x00800000U; /* highlight */
        case 14:return 0x00FFFFFFU; /* highlight text */
        case 15:return 0x00D8D8D8U; /* btn face */
        case 16:return 0x00808080U; /* btn shadow */
        case 17:return 0x00808080U; /* gray text */
        case 18:return 0x00000000U; /* btn text */
        case 20:return 0x00FFFFFFU; /* btn highlight */
        default:return 0x00D8D8D8U;
    }
}
static void *WIN32_API win32_GetSysColorBrush(int index) {
    typedef void * (WIN32_API *create_brush_t)(uint32_t);
    create_brush_t create;
    if(index<0||index>=32)return NULL;
    if(win_syscolor_brushes[index])return win_syscolor_brushes[index];
    create=(create_brush_t)(uintptr_t)pe_win32_resolve_export("GDI32.DLL","CreateSolidBrush");
    if(!create)return NULL;
    win_syscolor_brushes[index]=create(win32_GetSysColor(index));
    return win_syscolor_brushes[index];
}
static int32_t WIN32_API win32_SetClassLongA(void *hwnd UNUSED,int index,int32_t value) {
    int32_t old=0;
    switch(index){
        case GCL_STYLE:old=(int32_t)registered_class.style;registered_class.style=(uint32_t)value;break;
        case GCL_WNDPROC:old=(int32_t)(uintptr_t)registered_class.proc;registered_class.proc=(wndproc_t)(uintptr_t)value;break;
        case GCL_CBCLSEXTRA:old=registered_class.cls_extra;registered_class.cls_extra=value;break;
        case GCL_CBWNDEXTRA:old=registered_class.win_extra;registered_class.win_extra=value;break;
        case GCL_HMODULE:old=(int32_t)(uintptr_t)registered_class.instance;registered_class.instance=(void*)(uintptr_t)value;break;
        case GCL_HICON:old=(int32_t)(uintptr_t)registered_class.icon;registered_class.icon=(void*)(uintptr_t)value;break;
        case GCL_HCURSOR:old=(int32_t)(uintptr_t)registered_class.cursor;registered_class.cursor=(void*)(uintptr_t)value;break;
        case GCL_HBRBACKGROUND:old=(int32_t)(uintptr_t)registered_class.background;registered_class.background=(void*)(uintptr_t)value;break;
        default:return 0;
    }
    return old;
}
static int WIN32_API win32_RedrawWindow(void *hwnd,const int32_t *rect UNUSED,void *region UNUSED,uint32_t flags) {
    win_window_t*w=window_from_handle(hwnd);if(!w)return 0;
    if(flags&RDW_INVALIDATE)win32_InvalidateRect(hwnd,NULL,1);
    if(flags&RDW_UPDATENOW)win32_UpdateWindow(hwnd);
    return 1;
}
typedef struct {uint32_t cbSize,flags,showCmd;int32_t min_x,min_y,max_x,max_y,left,top,right,bottom;} window_placement_t;
static int WIN32_API win32_GetWindowPlacement(void*hwnd,window_placement_t*p){win_window_t*w=window_from_handle(hwnd);gui_rect_t r;if(!w||!p||p->cbSize<44U)return 0;r=window_screen_rect(w);p->flags=0;p->showCmd=w->visible?1U:0U;p->min_x=p->min_y=p->max_x=p->max_y=0;p->left=r.x;p->top=r.y;p->right=r.x+r.w;p->bottom=r.y+r.h;return 1;}
typedef struct {uint32_t cbSize,fMask;int32_t nMin,nMax;uint32_t nPage;int32_t nPos,nTrackPos;} scroll_info_t;
static int WIN32_API win32_GetScrollInfo(void*hwnd,int bar UNUSED,scroll_info_t*info){win_window_t*w=window_from_handle(hwnd);if(!w||!info||info->cbSize<28U)return 0;if(w->control&&w->kind==1U)edit_update_scroll_info(w);info->nMin=w->scroll_min;info->nMax=w->scroll_max;info->nPage=(uint32_t)(w->scroll_page<0?0:w->scroll_page);info->nPos=w->scroll_pos;info->nTrackPos=w->scroll_pos;return 1;}
static int WIN32_API win32_EnableScrollBar(void*hwnd,int bar UNUSED,uint32_t arrows UNUSED){return window_from_handle(hwnd)?1:0;}
static int WIN32_API win32_GetKeyboardState(uint8_t*state){if(!state)return 0;kmemset(state,0,256U);if(win_key_shift)state[VK_SHIFT]=0x80U;if(win_key_ctrl)state[VK_CONTROL]=0x80U;if(win_key_alt)state[VK_MENU]=0x80U;if(win_mouse_buttons&1U)state[1]=0x80U;return 1;}
static int WIN32_API win32_SetKeyboardState(const uint8_t*state){if(!state)return 0;win_key_shift=(state[VK_SHIFT]&0x80U)!=0;win_key_ctrl=(state[VK_CONTROL]&0x80U)!=0;win_key_alt=(state[VK_MENU]&0x80U)!=0;return 1;}
static uint32_t WIN32_API win32_RegisterWindowMessageA(const char*name){uint32_t hash=2166136261U;if(!name||!*name)return 0;while(*name){hash^=(uint8_t)*name++;hash*=16777619U;}return 0xC000U+(hash%0x3FFFU);}
static void *WIN32_API win32_SetCursor(void*cursor){void*old=win_current_cursor;win_current_cursor=cursor;return old;}
static int WIN32_API win32_GetCursorPos(int32_t*point){if(!point)return 0;point[0]=win_cursor_x;point[1]=win_cursor_y;return 1;}
static uint32_t WIN32_API win32_GetDialogBaseUnits(void){return (16U<<16)|8U;}
static int WIN32_API win32_SystemParametersInfoA(uint32_t action,uint32_t param UNUSED,void*data,uint32_t flags UNUSED){gui_desktop_t*d=gui_get_desktop();if(action==SPI_GETWORKAREA&&data){int32_t*r=(int32_t*)data;r[0]=0;r[1]=0;r[2]=d?d->surface.width:800;r[3]=d?d->surface.height:600;return 1;}if(action==SPI_GETWHEELSCROLLLINES&&data){*(uint32_t*)data=3U;return 1;}if(action==SPI_GETNONCLIENTMETRICS&&data){uint32_t size=*(uint32_t*)data;if(size>340U)size=340U;kmemset((uint8_t*)data+4,0,size>4?size-4:0);return 1;}return 0;}

static uint16_t WIN32_API win32_RegisterClassW(const wndclass_w_t *wc) {
    char name[64];
    if (!wc || !wc->proc || !wide_to_ansi(wc->name, name, sizeof(name))) return 0;
    wndclass_a_t ansi = {
        wc->style, wc->proc, wc->cls_extra, wc->win_extra,
        wc->instance, wc->icon, wc->cursor, wc->background, NULL, name
    };
    return win32_RegisterClassA(&ansi);
}

static uint16_t WIN32_API win32_RegisterClassExW(const void *raw) {
    const uint8_t *bytes = (const uint8_t *)raw;
    return bytes ? win32_RegisterClassW((const wndclass_w_t *)(bytes + 4)) : 0;
}

static const char *wide_class_to_ansi(const uint16_t *wide, char *out,
                                      uint32_t size) {
    uint32_t value = (uint32_t)(uintptr_t)wide;
    if (value <= 0xFFFFU) {
        switch (value) {
            case 0x0080U: return "BUTTON";
            case 0x0081U: return "EDIT";
            case 0x0082U: return "STATIC";
            default: return NULL;
        }
    }
    return wide_to_ansi(wide, out, size) ? out : NULL;
}

static void *WIN32_API win32_CreateWindowExW(uint32_t exstyle,
                                              const uint16_t *class_name,
                                              const uint16_t *title,
                                              uint32_t style, int x, int y,
                                              int width, int height,
                                              void *parent, void *menu,
                                              void *instance, void *param) {
    char class_buffer[64], title_buffer[1024];
    const char *ansi_class = wide_class_to_ansi(class_name, class_buffer,
                                                 sizeof(class_buffer));
    if (!ansi_class) return NULL;
    if (title && !wide_to_ansi(title, title_buffer, sizeof(title_buffer))) return NULL;
    return win32_CreateWindowExA(exstyle, ansi_class,
                                 title ? title_buffer : "", style,
                                 x, y, width, height, parent, menu,
                                 instance, param);
}

static int WIN32_API win32_SetWindowTextW(void *hwnd, const uint16_t *text) {
    char ansi[1024];
    return wide_to_ansi(text, ansi, sizeof(ansi))
        ? win32_SetWindowTextA(hwnd, ansi) : 0;
}

static int WIN32_API win32_GetWindowTextW(void *hwnd, uint16_t *text,
                                          int max_chars) {
    char ansi[1024];
    win32_GetWindowTextA(hwnd, ansi, sizeof(ansi));
    if (!text || max_chars <= 0) return 0;
    return (int)ansi_to_wide(ansi, text, (uint32_t)max_chars);
}

static int WIN32_API win32_GetWindowTextLengthW(void *hwnd) {
    return win32_GetWindowTextLengthA(hwnd);
}

static int32_t WIN32_API win32_DefWindowProcW(void *hwnd, uint32_t message,
                                              uint32_t wparam,
                                              int32_t lparam) {
    return win32_DefWindowProcA(hwnd, message, wparam, lparam);
}

static int32_t WIN32_API win32_SendMessageW(void *hwnd, uint32_t message,
                                             uint32_t wparam,
                                             int32_t lparam) {
    if (message == WM_SETTEXT)
        return win32_SetWindowTextW(hwnd,
            (const uint16_t *)(uintptr_t)lparam);
    if (message == WM_GETTEXT)
        return win32_GetWindowTextW(hwnd, (uint16_t *)(uintptr_t)lparam,
                                    (int)wparam);
    if (message == WM_GETTEXTLENGTH) return win32_GetWindowTextLengthW(hwnd);
    return win32_SendMessageA(hwnd, message, wparam, lparam);
}

static int WIN32_API win32_PostMessageW(void *hwnd, uint32_t message,
                                        uint32_t wparam, int32_t lparam) {
    return win32_PostMessageA(hwnd, message, wparam, lparam);
}

static int32_t WIN32_API win32_DispatchMessageW(const winmsg_t *message) {
    return win32_DispatchMessageA(message);
}

static int WIN32_API win32_GetMessageW(winmsg_t *message, void *hwnd,
                                       uint32_t min, uint32_t max) {
    return win32_GetMessageA(message, hwnd, min, max);
}

static int WIN32_API win32_PeekMessageW(winmsg_t *message, void *hwnd,
                                        uint32_t min, uint32_t max,
                                        uint32_t remove) {
    return win32_PeekMessageA(message, hwnd, min, max, remove);
}

static int32_t WIN32_API win32_CallWindowProcW(void *proc, void *hwnd,
                                               uint32_t message,
                                               uint32_t wparam,
                                               int32_t lparam) {
    return win32_CallWindowProcA(proc, hwnd, message, wparam, lparam);
}

static int WIN32_API win32_GetClassNameW(void *hwnd, uint16_t *out,
                                         int size) {
    char ansi[64];
    int length = win32_GetClassNameA(hwnd, ansi, sizeof(ansi));
    if (!out || size <= 0) return 0;
    return (int)ansi_to_wide(ansi, out, (uint32_t)size < (uint32_t)length + 1U
                             ? (uint32_t)size : (uint32_t)length + 1U);
}

static void *WIN32_API win32_FindWindowW(const uint16_t *class_name,
                                          const uint16_t *title) {
    char class_buffer[64], title_buffer[128];
    const char *class_a = NULL, *title_a = NULL;
    if (class_name) {
        if (!wide_to_ansi(class_name, class_buffer, sizeof(class_buffer))) return NULL;
        class_a = class_buffer;
    }
    if (title) {
        if (!wide_to_ansi(title, title_buffer, sizeof(title_buffer))) return NULL;
        title_a = title_buffer;
    }
    return win32_FindWindowA(class_a, title_a);
}

static int WIN32_API win32_MessageBoxW(void *owner, const uint16_t *text,
                                       const uint16_t *caption,
                                       uint32_t type) {
    char text_a[1024], caption_a[128];
    if (text && !wide_to_ansi(text, text_a, sizeof(text_a))) return 0;
    if (caption && !wide_to_ansi(caption, caption_a, sizeof(caption_a))) return 0;
    return win32_MessageBoxA(owner, text ? text_a : "",
                             caption ? caption_a : "Message", type);
}

uint32_t win32_user32_resolve(const char *name) {
#define U(api) if(equal(name,#api))return(uint32_t)(uintptr_t)&win32_##api
    U(MessageBoxA); U(MessageBoxW);
    U(RegisterClassA); U(RegisterClassW); U(RegisterClassExA); U(RegisterClassExW);
    U(CreateWindowExA); U(CreateWindowExW); U(ShowWindow); U(UpdateWindow);
    U(GetDC); U(ReleaseDC); U(BeginPaint); U(EndPaint);
    U(DestroyWindow); U(PostQuitMessage);
    U(GetMessageA); U(GetMessageW); U(PeekMessageA); U(PeekMessageW);
    U(DispatchMessageA); U(DispatchMessageW); U(TranslateMessage);
    U(DefWindowProcA); U(DefWindowProcW);
    U(SetWindowTextA); U(SetWindowTextW); U(GetClientRect);
    U(GetWindowTextA); U(GetWindowTextW);
    U(GetWindowTextLengthA); U(GetWindowTextLengthW);
    U(SendMessageA); U(SendMessageW); U(PostMessageA); U(PostMessageW);
    U(InvalidateRect); U(MoveWindow); U(GetWindowRect); U(SetWindowPos);
    U(EnableWindow); U(IsWindowEnabled); U(IsWindowVisible); U(GetParent);
    U(GetKeyState); U(MessageBeep);
    U(OpenClipboard); U(CloseClipboard); U(EmptyClipboard);
    U(SetClipboardData); U(GetClipboardData); U(IsClipboardFormatAvailable);
    U(GetForegroundWindow); U(SetForegroundWindow); U(BringWindowToTop);
    U(ClientToScreen); U(ScreenToClient); U(GetSystemMetrics);
    U(GetClassNameA); U(GetClassNameW); U(FindWindowA); U(FindWindowW);
    U(GetWindowLongA); U(GetWindowLongW); U(SetWindowLongA); U(SetWindowLongW);
    U(SetClassLongA); U(CallWindowProcA); U(CallWindowProcW);
    U(SetRect); U(SetRectEmpty); U(IsRectEmpty); U(EqualRect); U(PtInRect);
    U(OffsetRect); U(InflateRect); U(FillRect); U(DrawFocusRect);
    U(GetDlgItem); U(IsWindow); U(SetFocus); U(GetFocus);
    U(LoadStringA); U(LoadStringW); U(LoadCursorA); U(LoadCursorW);
    U(LoadIconA); U(LoadIconW); U(LoadBitmapA); U(LoadBitmapW);
    U(CreateMenu); U(CreatePopupMenu); U(AppendMenuA); U(AppendMenuW);
    U(LoadMenuA); U(LoadMenuW); U(SetMenu); U(GetMenu); U(DrawMenuBar);
    U(DestroyMenu); U(CheckMenuItem); U(SetMenuItemInfoA); U(GetMenuItemInfoA);
    U(EnableMenuItem); U(CheckMenuRadioItem); U(InsertMenuItemA); U(DeleteMenu);
    U(GetSubMenu); U(GetMenuItemCount); U(TrackPopupMenuEx);
    U(DialogBoxParamA); U(DialogBoxParamW);
    U(CreateDialogParamA); U(CreateDialogParamW); U(EndDialog);
    U(SetDlgItemTextA); U(SetDlgItemTextW);
    U(GetDlgItemTextA); U(GetDlgItemTextW);
    U(SendDlgItemMessageA); U(SendDlgItemMessageW);
    U(CheckDlgButton); U(IsDlgButtonChecked);
    U(LoadAcceleratorsA); U(LoadAcceleratorsW);
    U(TranslateAcceleratorA); U(TranslateAcceleratorW); U(DestroyAcceleratorTable);
    U(IsDialogMessageA); U(IsDialogMessageW);
    U(CharLowerBuffA); U(CharUpperBuffA); U(CharLowerA); U(CharUpperA);
    U(IsCharLowerA); U(IsCharUpperA); U(IsCharAlphaA);
    U(CharToOemBuffA); U(OemToCharBuffA); U(wsprintfA);
    U(GetSysColor); U(GetSysColorBrush); U(RedrawWindow);
    U(GetWindowPlacement); U(GetScrollInfo); U(EnableScrollBar);
    U(GetKeyboardState); U(SetKeyboardState); U(RegisterWindowMessageA);
    U(SetCursor); U(GetCursorPos); U(SystemParametersInfoA); U(GetDialogBaseUnits);
    U(SetTimer); U(KillTimer);
#undef U
    return 0;
}
