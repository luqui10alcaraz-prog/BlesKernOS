#include "win32.h"
#include "resources.h"
#include "../include/pe_loader.h"
#include "../include/file_dialog.h"
#include "../../gui/gui.h"
#include "../include/task.h"
#include "../include/memory.h"
#include "../include/pit.h"
#include "../stdio.h"
#include "../stdarg.h"

bool bk_sound_tone(uint32_t frequency_hz, uint32_t duration_ms);

#define WIN32_IDOK 1
#define WIN32_IDCANCEL 2
#define WIN32_IDABORT 3
#define WIN32_IDRETRY 4
#define WIN32_IDIGNORE 5
#define WIN32_IDYES 6
#define WIN32_IDNO 7
#define WIN32_IDTRYAGAIN 10
#define WIN32_IDCONTINUE 11
#define WM_INITDIALOG 0x0110U
#define BM_GETCHECK 0x00F0U
#define BM_SETCHECK 0x00F1U
#define LB_ADDSTRING 0x0180U
#define LB_INSERTSTRING 0x0181U
#define LB_DELETESTRING 0x0182U
#define LB_RESETCONTENT 0x0184U
#define LB_SETCURSEL 0x0186U
#define LB_GETCURSEL 0x0188U
#define LB_GETTEXT 0x0189U
#define LB_GETTEXTLEN 0x018AU
#define LB_GETCOUNT 0x018BU
#define LB_GETTOPINDEX 0x018EU
#define LB_FINDSTRING 0x018FU
#define LB_SETITEMDATA 0x019AU
#define LB_GETITEMDATA 0x0199U
#define LB_SETTOPINDEX 0x0197U
#define CB_ADDSTRING 0x0143U
#define CB_DELETESTRING 0x0144U
#define CB_GETCOUNT 0x0146U
#define CB_GETCURSEL 0x0147U
#define CB_GETLBTEXT 0x0148U
#define CB_GETLBTEXTLEN 0x0149U
#define CB_INSERTSTRING 0x014AU
#define CB_RESETCONTENT 0x014BU
#define CB_FINDSTRING 0x014CU
#define CB_SETCURSEL 0x014EU
#define CB_GETITEMDATA 0x0150U
#define CB_SETITEMDATA 0x0151U
#define CB_ERR (-1)
#define LBN_SELCHANGE 1U
#define CBN_SELCHANGE 1U
#define PBM_SETRANGE (0x0400U+1U)
#define PBM_SETPOS (0x0400U+2U)
#define PBM_DELTAPOS (0x0400U+3U)
#define PBM_SETSTEP (0x0400U+4U)
#define PBM_STEPIT (0x0400U+5U)
#define WM_NOTIFY 0x004EU
#define LVM_FIRST 0x1000U
/* BLES_WINE_LISTVIEW_IMAGELIST_20260724
 *
 * LVM_SETIMAGELIST returns the image list that was previously associated
 * with the requested slot. WinZip sets the small list once during startup,
 * replaces it later, and treats a NULL previous handle as listview.c:1049.
 */
#define LVM_GETIMAGELIST (LVM_FIRST+2U)
#define LVM_SETIMAGELIST (LVM_FIRST+3U)
#define LVSIL_NORMAL 0U
#define LVSIL_SMALL 1U
#define LVSIL_STATE 2U
#define WIN32_LISTVIEW_IMAGE_LISTS 3U
#define LVM_GETITEMCOUNT (LVM_FIRST+4U)
#define LVM_GETITEMA (LVM_FIRST+5U)
#define LVM_SETITEMA (LVM_FIRST+6U)
#define LVM_INSERTITEMA (LVM_FIRST+7U)
#define LVM_DELETEITEM (LVM_FIRST+8U)
#define LVM_DELETEALLITEMS (LVM_FIRST+9U)
#define LVM_GETNEXTITEM (LVM_FIRST+12U)
#define LVM_GETSTRINGWIDTHA (LVM_FIRST+17U)
#define LVM_ENSUREVISIBLE (LVM_FIRST+19U)
#define LVM_GETCOLUMNA (LVM_FIRST+25U)
#define LVM_INSERTCOLUMNA (LVM_FIRST+27U)
#define LVM_SETCOLUMNWIDTH (LVM_FIRST+30U)
#define LVM_UPDATE (LVM_FIRST+42U)
#define LVM_GETITEMSTATE (LVM_FIRST+44U)
#define LVM_GETITEMTEXTA (LVM_FIRST+45U)
#define LVM_SETITEMTEXTA (LVM_FIRST+46U)
#define LVM_SORTITEMS (LVM_FIRST+48U)
#define LVM_GETSELECTEDCOUNT (LVM_FIRST+50U)
#define LVM_SETEXTENDEDLISTVIEWSTYLE (LVM_FIRST+54U)
#define LVM_GETSUBITEMRECT (LVM_FIRST+58U)
#define LVIS_SELECTED 0x0002U
#define LVIF_TEXT 0x0001U
#define LVIF_PARAM 0x0004U
#define LVNI_SELECTED 0x0002U
#define TV_FIRST 0x1100U
#define TVM_INSERTITEMA (TV_FIRST+0U)
#define TVM_DELETEITEM (TV_FIRST+1U)
#define TVM_EXPAND (TV_FIRST+2U)
#define TVM_GETCOUNT (TV_FIRST+5U)
#define TVM_GETNEXTITEM (TV_FIRST+10U)
#define TVM_SELECTITEM (TV_FIRST+11U)
#define TVM_GETITEMA (TV_FIRST+12U)
#define TVM_SETITEMA (TV_FIRST+13U)
#define TVIF_TEXT 0x0001U
#define TVIF_PARAM 0x0004U
#define TVGN_ROOT 0x0000U
#define TVGN_NEXT 0x0001U
#define TVGN_PREVIOUS 0x0002U
#define TVGN_PARENT 0x0003U
#define TVGN_CHILD 0x0004U
#define TVGN_CARET 0x0009U
#define TCM_FIRST 0x1300U
#define TCM_GETITEMCOUNT (TCM_FIRST+4U)
#define TCM_GETITEMA (TCM_FIRST+5U)
#define TCM_SETITEMA (TCM_FIRST+6U)
#define TCM_INSERTITEMA (TCM_FIRST+7U)
#define TCM_DELETEITEM (TCM_FIRST+8U)
#define TCM_DELETEALLITEMS (TCM_FIRST+9U)
#define TCM_GETCURSEL (TCM_FIRST+11U)
#define TCM_SETCURSEL (TCM_FIRST+12U)
#define TCM_ADJUSTRECT (TCM_FIRST+40U)
#define TCIF_TEXT 0x0001U
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
#define SF_TEXT 0x0001U
#define SF_RTF 0x0002U
#define SCF_SELECTION 0x0001U
#define SCF_WORD 0x0002U
#define SCF_ALL 0x0004U
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
#define DS_CENTER 0x00000800U
#define WS_POPUP 0x80000000U
#define WS_TABSTOP 0x00010000U
#define SS_TYPEMASK 0x0000001FU
#define SS_LEFT 0x00000000U
#define SS_CENTER 0x00000001U
#define SS_RIGHT 0x00000002U
#define SS_ICON 0x00000003U
#define SS_BITMAP 0x0000000EU
#define SS_SIMPLE 0x0000000BU
#define SS_LEFTNOWORDWRAP 0x0000000CU
#define SS_NOPREFIX 0x00000080U
#define SS_NOTIFY 0x00000100U
#define STM_SETICON 0x0170U
#define STM_GETICON 0x0171U
#define STM_SETIMAGE 0x0172U
#define STM_GETIMAGE 0x0173U
#define IMAGE_BITMAP 0U
#define IMAGE_ICON 1U
#define MF_POPUP 0x0010U
#define MF_STRING 0x0000U
#define MF_BYCOMMAND 0x0000U
#define MF_ENABLED 0x0000U
#define MF_REMOVE 0x1000U
#define MF_DELETE 0x0200U
#define MF_END 0x0080U
#define MF_SEPARATOR 0x0800U
#define MF_BYPOSITION 0x0400U
#define SC_SIZE 0xF000U
#define SC_MOVE 0xF010U
#define SC_MINIMIZE 0xF020U
#define SC_MAXIMIZE 0xF030U
#define SC_CLOSE 0xF060U
#define SC_RESTORE 0xF120U
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
#define WIN32_MAX_WINDOWS 64U
#define WIN32_MAX_MENUS 16U
#define WIN32_MAX_MENU_ITEMS 64U
#define WIN32_MESSAGE_QUEUE 128U
#define WM_CREATE 0x0001U
#define WM_DESTROY 0x0002U
#define WM_MOVE 0x0003U
#define WM_SIZE 0x0005U
#define WM_WINDOWPOSCHANGING 0x0046U
#define WM_WINDOWPOSCHANGED 0x0047U
#define WM_NCCREATE 0x0081U
#define WM_GETDLGCODE 0x0087U
#define DLGC_WANTARROWS 0x0001U
#define DLGC_WANTTAB 0x0002U
#define DLGC_WANTALLKEYS 0x0004U
#define DLGC_WANTCHARS 0x0080U
#define WM_CLOSE 0x0010U
#define WM_GETICON 0x007FU
#define WM_SETICON 0x0080U
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
#define SB_GETBORDERS 0x0407U
#define SB_SETMINHEIGHT 0x0408U
#define SB_SIMPLE 0x0409U
#define SB_GETRECT 0x040AU
#define SB_SETTEXTW 0x040BU
#define SB_GETTEXTLENGTHW 0x040CU
#define SB_GETTEXTW 0x040DU
#define SB_ISSIMPLE 0x040EU
#define SB_SETICON 0x040FU
#define SB_SETTIPTEXTA 0x0410U
#define SB_SETTIPTEXTW 0x0411U
#define SB_GETTIPTEXTA 0x0412U
#define SB_GETTIPTEXTW 0x0413U
#define SB_GETICON 0x0414U
#define SBT_OWNERDRAW 0x1000U
#define CCS_TOP 0x00000001U
#define CW_USEDEFAULT 0x80000000U
#define CCM_SETBKCOLOR 0x2001U
#define TB_ENABLEBUTTON 0x0401U
#define TB_CHECKBUTTON 0x0402U
#define TB_HIDEBUTTON 0x0404U
#define TB_ADDBITMAP 0x0413U
#define TB_ADDBUTTONSA 0x0414U
#define TB_BUTTONCOUNT 0x0418U
#define TB_GETITEMRECT 0x041DU
#define TB_BUTTONSTRUCTSIZE 0x041EU
#define TB_SETBUTTONSIZE 0x041FU
#define TB_SETBITMAPSIZE 0x0420U
#define TB_SETIMAGELIST 0x0430U
#define TB_GETIMAGELIST 0x0431U
#define TB_AUTOSIZE 0x0421U

/* BLES_WINE_REBAR_CONTROL_20260723 */
#define RB_INSERTBANDA 0x0401U
#define RB_DELETEBAND 0x0402U
#define RB_GETBARINFO 0x0403U
#define RB_SETBARINFO 0x0404U
#define RB_GETBANDINFOA 0x0405U
#define RB_SETBANDINFOA 0x0406U
#define RB_SETPARENT 0x0407U
#define RB_HITTEST 0x0408U
#define RB_GETRECT 0x0409U
#define RB_INSERTBANDW 0x040AU
#define RB_SETBANDINFOW 0x040BU
#define RB_GETBANDCOUNT 0x040CU
#define RB_GETROWCOUNT 0x040DU
#define RB_GETROWHEIGHT 0x040EU
#define RB_IDTOINDEX 0x0410U
#define RB_SETBKCOLOR 0x0413U
#define RB_GETBKCOLOR 0x0414U
#define RB_SETTEXTCOLOR 0x0415U
#define RB_GETTEXTCOLOR 0x0416U
#define RB_SIZETORECT 0x0417U
#define RB_GETBARHEIGHT 0x041BU
#define RB_GETBANDINFOW 0x041CU
#define RB_SHOWBAND 0x0423U

#define RBBIM_STYLE 0x00000001U
#define RBBIM_COLORS 0x00000002U
#define RBBIM_TEXT 0x00000004U
#define RBBIM_IMAGE 0x00000008U
#define RBBIM_CHILD 0x00000010U
#define RBBIM_CHILDSIZE 0x00000020U
#define RBBIM_SIZE 0x00000040U
#define RBBIM_BACKGROUND 0x00000080U
#define RBBIM_ID 0x00000100U
#define RBBIM_IDEALSIZE 0x00000200U
#define RBBIM_LPARAM 0x00000400U
#define RBBIM_HEADERSIZE 0x00000800U
#define RBBS_HIDDEN 0x00000008U
#define WIN32_REBAR_MAX_BANDS 8U
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_SHOWMINIMIZED 2
#define SW_SHOWMAXIMIZED 3
#define SW_SHOWNOACTIVATE 4
#define SW_SHOW 5
#define SW_MINIMIZE 6
#define SW_SHOWMINNOACTIVE 7
#define SW_SHOWNA 8
#define SW_RESTORE 9
#define SW_SHOWDEFAULT 10
#define SW_FORCEMINIMIZE 11
#define WS_CHILD 0x40000000U
#define WS_MINIMIZE 0x20000000U
#define WS_VISIBLE 0x10000000U
#define WS_DISABLED 0x08000000U
#define WS_MAXIMIZE 0x01000000U
#define WS_BORDER 0x00800000U
/* BLES_WINE_CORE_IMPORT_FIX_20260723_USER32 */
#define WS_EX_TRANSPARENT 0x00000020U
#define CWP_SKIPINVISIBLE 0x0001U
#define CWP_SKIPDISABLED 0x0002U
#define CWP_SKIPTRANSPARENT 0x0004U
#define SWP_NOSIZE 0x0001U
#define SWP_NOMOVE 0x0002U
#define SWP_NOZORDER 0x0004U
#define SWP_NOACTIVATE 0x0010U
#define SWP_FRAMECHANGED 0x0020U
#define SWP_SHOWWINDOW 0x0040U
#define SWP_HIDEWINDOW 0x0080U
#define SW_SCROLLCHILDREN 0x0001U
#define SW_INVALIDATE 0x0002U
#define SW_ERASE 0x0004U
#define SW_SMOOTHSCROLL 0x0010U
#define NULLREGION 1
#define SIMPLEREGION 2
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
#define DESKTOP_HWND ((void *)(uintptr_t)0x72FFFFFFU)
#define GW_HWNDFIRST 0U
#define GW_HWNDLAST 1U
#define GW_HWNDNEXT 2U
#define GW_HWNDPREV 3U
#define GW_OWNER 4U
#define GW_CHILD 5U
#define GA_PARENT 1U
#define GA_ROOT 2U
#define GA_ROOTOWNER 3U
#define WIN32_WINDOW_PROPERTIES 8U
typedef int32_t (WIN32_API *wndproc_t)(void *,uint32_t,uint32_t,int32_t);
static int32_t WIN32_API win32_StatusBarWndProc(
    void *hwnd, uint32_t message, uint32_t wparam, int32_t lparam);
static int32_t WIN32_API win32_ReBarWndProc(
    void *hwnd, uint32_t message, uint32_t wparam, int32_t lparam);

static uint32_t win32_wndproc_target_pid(void *hwnd);

/* BLES_WINE_SYNC_WINDOW_FIX_20260723
 * SendMessage is a nonqueued, synchronous call for a window owned by the
 * calling UI task.  The API proxy cannot call PE code at CPL0, so the proxy
 * returns a call plan to a tiny Ring-3 thunk instead of enqueueing an upcall. */
static win32_user_message_plan_t *g_sync_message_plan;


/* WIN32_RING3_WNDPROC_UPCALL */
static bool win32_queue_wndproc_upcall(
        wndproc_t proc,
        void *hwnd,
        uint32_t message,
        uint32_t wparam,
        int32_t lparam,
        const void *payload,
        uint8_t payload_size,
        int8_t payload_argument) {
    uint32_t arguments[4];

    if (!proc) return true;

    arguments[0] = (uint32_t)(uintptr_t)hwnd;
    arguments[1] = message;
    arguments[2] = wparam;
    arguments[3] = (uint32_t)lparam;

    uint32_t target_pid = win32_wndproc_target_pid(hwnd);
    if (!target_pid) target_pid = task_current_pid();

    if (!task_queue_user_upcall(
            target_pid,
            (uint32_t)(uintptr_t)proc,
            arguments,
            4U,
            payload,
            payload_size,
            payload_argument)) {
        kprintf("[WIN32] ERROR encolando WndProc "
                "proc=%x msg=%x pid=%u\n",
                (uint32_t)(uintptr_t)proc,
                message,
                target_pid);
        return false;
    }

    /* BLES_WINE_WINEXEC_SPEED_FIX_20260723: serial output is much slower
     * than normal dispatch. Keep queue failures visible, not every success. */
#ifdef BLES_WIN32_VERBOSE_WNDPROC
    kprintf("[WIN32] WndProc diferido a Ring 3 "
            "proc=%x msg=%x pid=%u\n",
            (uint32_t)(uintptr_t)proc,
            message,
            target_pid);
#endif
    return true;
}

/* Los procedimientos PE deben ejecutarse mediante el upcall Ring 3.  Llamar
 * uno directamente desde USER32 deja el callback en CPL0 y convierte una
 * excepcion de la aplicacion en un kernel panic. */
static bool win32_wndproc_is_pe(wndproc_t proc) {
    return proc && pe_win32_query_image_region((const void *)(uintptr_t)proc,
                                                NULL, NULL);
}

static int32_t win32_call_or_queue_wndproc(
        wndproc_t proc, void *hwnd, uint32_t message,
        uint32_t wparam, int32_t lparam,
        const void *payload, uint8_t payload_size,
        int8_t payload_argument) {
    if (!proc) return 0;
    if (!win32_wndproc_is_pe(proc))
        return proc(hwnd, message, wparam, lparam);
    /* A SendMessage made by the owner task must behave like a direct
     * subroutine call and return the actual LRESULT.  Capture only the
     * original target HWND; notifications emitted internally by a
     * kernel control retain the normal queued-upcall path. */
    if (g_sync_message_plan &&
        g_sync_message_plan->hwnd == (uint32_t)(uintptr_t)hwnd &&
        win32_wndproc_target_pid(hwnd) == task_current_pid()) {
        g_sync_message_plan->invoke = 1U;
        g_sync_message_plan->proc = (uint32_t)(uintptr_t)proc;
        g_sync_message_plan->message = message;
        g_sync_message_plan->wparam = wparam;
        g_sync_message_plan->lparam = lparam;
#ifdef BLES_WIN32_VERBOSE_WNDPROC
        kprintf("[WIN32] SendMessage sync Ring3 proc=%x msg=%x hwnd=%x\n",
                (uint32_t)(uintptr_t)proc, message,
                (uint32_t)(uintptr_t)hwnd);
#endif
        return 0;
    }
    return win32_queue_wndproc_upcall(proc, hwnd, message, wparam, lparam,
                                      payload, payload_size,
                                      payload_argument) ? 0 : -1;
}

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
    uint32_t cb_size;
    uint32_t mask;
    uint32_t style;
    uint32_t clr_fore;
    uint32_t clr_back;
    void *text;
    uint32_t text_length;
    int32_t image;
    void *child;
    uint32_t cx_min_child;
    uint32_t cy_min_child;
    uint32_t cx;
    void *background;
    uint32_t id;
    uint32_t cy_child;
    uint32_t cy_max_child;
    uint32_t cy_integral;
    uint32_t cx_ideal;
    int32_t lparam;
    uint32_t cx_header;
} win32_rebar_band_info_t;

typedef struct {
    bool used, destroy_sent, control, focused, pressed, enabled, visible;
    bool dialog, dialog_done, dialog_modal, dialog_owner_was_enabled;
    uint8_t kind;
    uint32_t owner_process_id;
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
    uint8_t rich_char_format[116];
    uint8_t rich_para_format[188];
    void *font;
    int16_t font_pixel_height, dialog_base_x, dialog_base_y;
    bool font_bold, font_italic, font_monospace, paint_active;
    void *large_icon, *small_icon;
    void *listview_image_lists[WIN32_LISTVIEW_IMAGE_LISTS];
    int32_t scroll_min, scroll_max, scroll_page, scroll_pos;
    uint8_t status_part_count, toolbar_count;
    bool status_simple;
    uint32_t status_background;
    int16_t toolbar_button_width, toolbar_button_height;
    int16_t toolbar_bitmap_width, toolbar_bitmap_height;
    uint16_t toolbar_bitmap_count;
    uint32_t *toolbar_pixels;
    int32_t status_parts[8];
    uint16_t status_styles[8];
    uint32_t status_item_data[8];
    char status_text[8][64];
    char status_tips[8][64];
    void *status_icons[8];
    uint16_t toolbar_commands[32];
    int16_t toolbar_bitmap_indices[32];
    uint8_t toolbar_states[32], toolbar_styles[32];

    uint8_t rebar_band_count;
    uint32_t rebar_background, rebar_text_color;
    uint32_t rebar_masks[WIN32_REBAR_MAX_BANDS];
    uint32_t rebar_styles[WIN32_REBAR_MAX_BANDS];
    uint32_t rebar_ids[WIN32_REBAR_MAX_BANDS];
    int32_t rebar_cx_min[WIN32_REBAR_MAX_BANDS];
    int32_t rebar_cy_min[WIN32_REBAR_MAX_BANDS];
    int32_t rebar_cx[WIN32_REBAR_MAX_BANDS];
    void *rebar_children[WIN32_REBAR_MAX_BANDS];
    gui_rect_t rebar_rects[WIN32_REBAR_MAX_BANDS];
    char rebar_text[WIN32_REBAR_MAX_BANDS][64];

    char **control_items;
    int32_t *control_item_data;
    uint16_t control_item_count, control_item_capacity;
    int16_t control_cur_sel, control_top_index;
    void *instance;
    /* cbWndExtra belongs to each window instance and starts zeroed. */
    uint8_t *window_extra;
    uint32_t window_extra_size;
    int32_t user_data, dialog_result;
    struct { bool used; uint16_t atom; char name[32]; void *value; } properties[WIN32_WINDOW_PROPERTIES];
    char class_name[32];
    char text[1024];
} win_window_t;
#define WIN32_MAX_CLASSES 16U
typedef struct { bool used; uint32_t owner_process_id; wndclass_a_t definition; char name[48]; } win_class_t;
static wndclass_a_t registered_class;
static char registered_name[48];
static uint32_t registered_class_process_id;
static win_class_t registered_classes[WIN32_MAX_CLASSES];
static win_window_t win_windows[WIN32_MAX_WINDOWS];
static winmsg_t message_queue[WIN32_MESSAGE_QUEUE]; static uint8_t message_head,message_tail;
typedef struct{bool used;uint8_t kind,font_flags;int16_t font_height;void*hwnd;int x1,y1,x2,y2;uint32_t color;char text[96];uint32_t*pixels;uint32_t pixel_capacity;int pitch,src_x,src_y;}gdi_command_t;
#define WIN32_MAX_GDI_COMMANDS 256U
static gdi_command_t gdi_commands[WIN32_MAX_GDI_COMMANDS];
#define MENU_BASE 0x76000000U
typedef struct { uint32_t id, flags; void *submenu; char text[48]; } win_menu_item_t;
typedef struct { bool used; uint8_t count; win_menu_item_t items[WIN32_MAX_MENU_ITEMS]; } win_menu_t;
static win_menu_t win_menus[WIN32_MAX_MENUS];
static void *win_system_menus[WIN32_MAX_WINDOWS];
static void win32_release_system_menu(uint32_t window_index);
static void *load_menu_resource(void *instance, const void *name, bool wide);
static int WIN32_API win32_SetMenu(void *hwnd, void *handle);

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
typedef struct {
    void *instance;
    uint32_t bitmap_id;
} win_toolbar_add_bitmap_t;

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
static bool is_list_class(const char *name) { return equal_ci(name,"LISTBOX"); }
static bool is_combo_class(const char *name) { return equal_ci(name,"COMBOBOX"); }
static bool is_progress_class(const char *name) {
    return equal_ci(name,"msctls_progress32") || equal_ci(name,"PROGRESS_CLASS");
}
static bool is_listview_class(const char *name){return equal_ci(name,"SysListView32");}
static bool is_treeview_class(const char *name){return equal_ci(name,"SysTreeView32");}
static bool is_tab_class(const char *name){return equal_ci(name,"SysTabControl32");}

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
static int WIN32_API win32_SetForegroundWindow(void *hwnd);
static int WIN32_API win32_SetDlgItemTextA(void *dialog, int id, const char *text);
static int WIN32_API win32_GetDlgItemTextA(void *dialog, int id,
                                           char *out, int size);

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

static uint32_t rich_hex_value(char c) {
    if (c >= '0' && c <= '9') return (uint32_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint32_t)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (uint32_t)(c - 'A' + 10);
    return 0xFFFFFFFFU;
}

static bool rich_control_is(const char *word, uint32_t length,
                            const char *expected) {
    uint32_t i = 0;
    while (expected[i] && i < length && expected[i] == word[i]) i++;
    return i == length && expected[i] == '\0';
}

static char *rich_rtf_to_text(const char *input, uint32_t length,
                              uint32_t *out_length) {
    char *out;
    uint32_t o = 0U, depth = 0U, skip_depth = 0xFFFFFFFFU;
    uint32_t uc_skip = 1U, pending_fallback = 0U;
    if (out_length) *out_length = 0U;
    if (!input) return NULL;
    if (length > 0x3FFFFFFFU) return NULL;
    out = (char *)kmalloc(length * 2U + 1U);
    if (!out) return NULL;
    for (uint32_t i = 0; i < length; i++) {
        char c = input[i];
        if (c == '{') { depth++; continue; }
        if (c == '}') {
            if (skip_depth != 0xFFFFFFFFU && depth <= skip_depth)
                skip_depth = 0xFFFFFFFFU;
            if (depth) depth--;
            continue;
        }
        if (skip_depth != 0xFFFFFFFFU && depth >= skip_depth) {
            if (c == '\\') {
                while (i + 1U < length &&
                       ((input[i + 1U] >= 'a' && input[i + 1U] <= 'z') ||
                        (input[i + 1U] >= 'A' && input[i + 1U] <= 'Z'))) i++;
            }
            continue;
        }
        if (pending_fallback) { pending_fallback--; continue; }
        if (c == '\r' || c == '\n') continue;
        if (c != '\\') { out[o++] = c; continue; }
        if (++i >= length) break;
        c = input[i];
        if (c == '\\' || c == '{' || c == '}') { out[o++] = c; continue; }
        if (c == '\'') {
            uint32_t hi, lo;
            if (i + 2U < length &&
                (hi = rich_hex_value(input[i + 1U])) != 0xFFFFFFFFU &&
                (lo = rich_hex_value(input[i + 2U])) != 0xFFFFFFFFU) {
                out[o++] = (char)((hi << 4U) | lo); i += 2U;
            }
            continue;
        }
        if (c == '~') { out[o++] = ' '; continue; }
        if (c == '_') { out[o++] = '-'; continue; }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) continue;
        {
            uint32_t start = i, word_length, value = 0U;
            bool negative = false, has_value = false;
            while (i < length && ((input[i] >= 'a' && input[i] <= 'z') ||
                                  (input[i] >= 'A' && input[i] <= 'Z'))) i++;
            word_length = i - start;
            if (i < length && input[i] == '-') { negative = true; i++; }
            while (i < length && input[i] >= '0' && input[i] <= '9') {
                has_value = true; value = value * 10U + (uint32_t)(input[i] - '0'); i++;
            }
            if (i < length && input[i] != ' ') i--;
            if (rich_control_is(input + start, word_length, "fonttbl") ||
                rich_control_is(input + start, word_length, "colortbl") ||
                rich_control_is(input + start, word_length, "stylesheet") ||
                rich_control_is(input + start, word_length, "info") ||
                rich_control_is(input + start, word_length, "pict") ||
                rich_control_is(input + start, word_length, "object")) {
                skip_depth = depth; continue;
            }
            if (rich_control_is(input + start, word_length, "par") ||
                rich_control_is(input + start, word_length, "line")) {
                out[o++] = '\r'; out[o++] = '\n'; continue;
            }
            if (rich_control_is(input + start, word_length, "tab")) { out[o++] = '\t'; continue; }
            if (rich_control_is(input + start, word_length, "emdash")) { out[o++] = '-'; out[o++] = '-'; continue; }
            if (rich_control_is(input + start, word_length, "endash")) { out[o++] = '-'; continue; }
            if (rich_control_is(input + start, word_length, "bullet")) { out[o++] = '*'; continue; }
            if (rich_control_is(input + start, word_length, "uc") && has_value) { uc_skip = value; continue; }
            if (rich_control_is(input + start, word_length, "u") && has_value) {
                int32_t signed_value = negative ? -(int32_t)value : (int32_t)value;
                out[o++] = (signed_value >= 0 && signed_value <= 255) ? (char)signed_value : '?';
                pending_fallback = uc_skip; continue;
            }
        }
    }
    out[o] = '\0';
    if (out_length) *out_length = o;
    return out;
}

static char *rich_text_to_rtf(const char *text, uint32_t length,
                              uint32_t *out_length) {
    static const char header[] = "{\\rtf1\\ansi\\deff0 ";
    char *out;
    uint32_t cap, o = 0U;
    if (out_length) *out_length = 0U;
    if (length > 0x15555500U) return NULL;
    cap = length * 3U + sizeof(header) + 4U;
    out = (char *)kmalloc(cap);
    if (!out) return NULL;
    for (uint32_t i = 0; header[i]; i++) out[o++] = header[i];
    for (uint32_t i = 0; i < length; i++) {
        uint8_t c = (uint8_t)text[i];
        if (c == '\r' && i + 1U < length && text[i + 1U] == '\n') {
            const char par[] = "\\par ";
            for (uint32_t j = 0; par[j]; j++) out[o++] = par[j];
            i++; continue;
        }
        if (c == '\n') { const char par[] = "\\par "; for (uint32_t j=0;par[j];j++) out[o++]=par[j]; continue; }
        if (c == '\t') { const char tab[] = "\\tab "; for (uint32_t j=0;tab[j];j++) out[o++]=tab[j]; continue; }
        if (c == '\\' || c == '{' || c == '}') { out[o++] = '\\'; out[o++] = (char)c; continue; }
        if (c < 32U || c >= 127U) {
            static const char hex[] = "0123456789abcdef";
            out[o++]='\\';out[o++]='\'';out[o++]=hex[c>>4U];out[o++]=hex[c&15U];continue;
        }
        out[o++] = (char)c;
    }
    out[o++] = '}'; out[o] = '\0';
    if (out_length) *out_length = o;
    return out;
}

static int32_t rich_format_set(uint8_t *storage, uint32_t storage_size,
                               const void *source) {
    uint32_t size;
    if (!storage || !source) return 0;
    size = *(const uint32_t *)source;
    if (size < 4U) return 0;
    if (size > storage_size) size = storage_size;
    kmemset(storage, 0, storage_size);
    kmemcpy(storage, source, size);
    *(uint32_t *)storage = size;
    return 1;
}

static int32_t rich_format_get(const uint8_t *storage, uint32_t storage_size,
                               void *destination) {
    uint32_t requested, available;
    if (!storage || !destination) return 0;
    requested = *(uint32_t *)destination;
    if (requested < 4U) return 0;
    available = *(const uint32_t *)storage;
    if (available < 4U || available > storage_size) available = storage_size;
    if (requested < available) available = requested;
    kmemcpy(destination, storage, available);
    *(uint32_t *)destination = requested;
    return 1;
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

static uint32_t win32_wndproc_target_pid(void *hwnd) {
    win_window_t *window = window_from_handle(hwnd);
    while (window && window->control && window->parent) {
        win_window_t *parent = window_from_handle(window->parent);
        if (!parent) break;
        window = parent;
    }
    return window && window->native ? window->native->owner_pid : 0U;
}
static void *handle_from_native(gui_window_t *native){if(!native)return NULL;for(uint32_t i=0;i<WIN32_MAX_WINDOWS;i++)if(win_windows[i].used&&!win_windows[i].control&&win_windows[i].native==native)return(void*)(uintptr_t)(HWND_BASE+i);return NULL;}
static gui_rect_t window_screen_rect(win_window_t *w){if(!w)return(gui_rect_t){0,0,0,0};if(!w->control)return w->native->bounds;win_window_t*p=window_from_handle(w->parent);if(!p)return w->bounds;gui_rect_t client=gui_window_content_rect(p->native);return(gui_rect_t){client.x+w->bounds.x,client.y+w->bounds.y,w->bounds.w,w->bounds.h};}
static uint32_t win_last_message_time;
static void queue_message(void *hwnd,uint32_t msg,uint32_t wp,int32_t lp){uint8_t next=(uint8_t)((message_tail+1U)%WIN32_MESSAGE_QUEUE);uint32_t hz;if(next==message_head)return;hz=pit_get_frequency_hz();win_last_message_time=hz?(uint32_t)(((uint64_t)pit_get_ticks()*1000U)/hz):0U;message_queue[message_tail]=(winmsg_t){hwnd,msg,wp,(uint32_t)lp,win_last_message_time,win_cursor_x,win_cursor_y};message_tail=next;}
static void control_items_reset(win_window_t *w) {
    if (!w) return;
    for (uint32_t i = 0; i < w->control_item_count; i++)
        if (w->control_items && w->control_items[i]) kfree(w->control_items[i]);
    if (w->control_items) kfree(w->control_items);
    if (w->control_item_data) kfree(w->control_item_data);
    w->control_items = NULL; w->control_item_data = NULL;
    w->control_item_count = w->control_item_capacity = 0U;
    w->control_cur_sel = -1; w->control_top_index = 0;
}
static bool control_items_reserve(win_window_t *w, uint32_t needed) {
    uint32_t capacity;
    char **items;
    int32_t *data;
    if (!w || needed > 1024U) return false;
    if (needed <= w->control_item_capacity) return true;
    capacity = w->control_item_capacity ? w->control_item_capacity * 2U : 8U;
    while (capacity < needed) capacity *= 2U;
    items = (char **)kzalloc(capacity * sizeof(char *));
    data = (int32_t *)kzalloc(capacity * sizeof(int32_t));
    if (!items || !data) { if (items) kfree(items); if (data) kfree(data); return false; }
    if (w->control_item_count) {
        kmemcpy(items, w->control_items, w->control_item_count * sizeof(char *));
        kmemcpy(data, w->control_item_data, w->control_item_count * sizeof(int32_t));
    }
    if (w->control_items) kfree(w->control_items);
    if (w->control_item_data) kfree(w->control_item_data);
    w->control_items = items; w->control_item_data = data;
    w->control_item_capacity = (uint16_t)capacity;
    return true;
}
static int control_item_insert(win_window_t *w, int index, const char *text) {
    uint32_t length;
    char *copy;
    if (!w || !text) return CB_ERR;
    if (index < 0 || index > (int)w->control_item_count)
        index = (int)w->control_item_count;
    if (!control_items_reserve(w, w->control_item_count + 1U)) return CB_ERR;
    length = (uint32_t)kstrlen(text);
    copy = (char *)kmalloc(length + 1U);
    if (!copy) return CB_ERR;
    kmemcpy(copy, text, length + 1U);
    for (uint32_t i = w->control_item_count; i > (uint32_t)index; i--) {
        w->control_items[i] = w->control_items[i - 1U];
        w->control_item_data[i] = w->control_item_data[i - 1U];
    }
    w->control_items[index] = copy; w->control_item_data[index] = 0;
    w->control_item_count++;
    return index;
}
static int control_item_delete(win_window_t *w, int index) {
    if (!w || index < 0 || index >= (int)w->control_item_count) return CB_ERR;
    kfree(w->control_items[index]);
    for (uint32_t i = (uint32_t)index + 1U; i < w->control_item_count; i++) {
        w->control_items[i - 1U] = w->control_items[i];
        w->control_item_data[i - 1U] = w->control_item_data[i];
    }
    w->control_item_count--;
    if (w->control_cur_sel == index) w->control_cur_sel = -1;
    else if (w->control_cur_sel > index) w->control_cur_sel--;
    return (int)w->control_item_count;
}
static int control_item_find(win_window_t *w, int start, const char *needle) {
    uint32_t first;
    if (!w || !needle || !w->control_item_count) return CB_ERR;
    first = (uint32_t)(start + 1);
    if (first >= w->control_item_count) first = 0U;
    for (uint32_t n = 0; n < w->control_item_count; n++) {
        uint32_t i = (first + n) % w->control_item_count;
        const char *a = w->control_items[i], *b = needle;
        while (*a && *b) { uint8_t ca=(uint8_t)*a++,cb=(uint8_t)*b++;if(ca>='a'&&ca<='z')ca-=32;if(cb>='a'&&cb<='z')cb-=32;if(ca!=cb)break; }
        if (!*b) return (int)i;
    }
    return CB_ERR;
}
static void cleanup_control(win_window_t *w) {
    if (!w) return;
    if (w->edit_buffer) kfree(w->edit_buffer);
    if (w->undo_buffer) kfree(w->undo_buffer);
    if (w->window_extra) kfree(w->window_extra);
    if (w->toolbar_pixels) kfree(w->toolbar_pixels);
    control_items_reset(w);
    kmemset(w, 0, sizeof(*w));
}

static void cleanup_window(void *hwnd) {
    win_window_t *w = window_from_handle(hwnd);
    if (!w) return;
    win32_release_system_menu((uint32_t)(w - win_windows));
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
    if (w->window_extra) kfree(w->window_extra);
    kmemset(w, 0, sizeof(*w));
    gui_request_paint();
}

static bool message_owned_by_process(const winmsg_t *message,
                                     uint32_t process_id) {
    uint32_t value;
    if (!message || !message->hwnd) return false;
    value = (uint32_t)(uintptr_t)message->hwnd;
    if (value < HWND_BASE || value >= HWND_BASE + WIN32_MAX_WINDOWS)
        return false;
    return win_windows[value - HWND_BASE].used &&
           win_windows[value - HWND_BASE].owner_process_id == process_id;
}

void win32_user32_cleanup_process(uint32_t process_id) {
    uint8_t read, write;
    if (!process_id) return;

    /* Elimina mensajes antes de que sus HWND se reutilicen en otro PE. */
    read = message_head;
    write = message_head;
    while (read != message_tail) {
        winmsg_t message = message_queue[read];
        read = (uint8_t)((read + 1U) % WIN32_MESSAGE_QUEUE);
        if (message_owned_by_process(&message, process_id)) continue;
        message_queue[write] = message;
        write = (uint8_t)((write + 1U) % WIN32_MESSAGE_QUEUE);
    }
    message_tail = write;

    /* Los controles comparten el frame nativo: liberarlos primero. */
    for (uint32_t i = 0; i < WIN32_MAX_WINDOWS; i++)
        if (win_windows[i].used && win_windows[i].control &&
            win_windows[i].owner_process_id == process_id)
            cleanup_control(&win_windows[i]);
    for (uint32_t i = 0; i < WIN32_MAX_WINDOWS; i++)
        if (win_windows[i].used && !win_windows[i].control &&
            win_windows[i].owner_process_id == process_id)
            cleanup_window((void *)(uintptr_t)(HWND_BASE + i));

    for (uint32_t i = 0; i < WIN32_MAX_CLASSES; i++)
        if (registered_classes[i].used &&
            registered_classes[i].owner_process_id == process_id)
            kmemset(&registered_classes[i], 0, sizeof(registered_classes[i]));
    if (registered_class_process_id == process_id) {
        kmemset(&registered_class, 0, sizeof(registered_class));
        kmemset(registered_name, 0, sizeof(registered_name));
        registered_class_process_id = 0U;
    }
}

static void win32_update_native_icon(win_window_t *window) {
    const uint32_t *pixels = NULL;
    int width = 0, height = 0;
    void *icon;
    if (!window || window->control || !window->native) return;
    icon = window->small_icon ? window->small_icon : window->large_icon;
    if (!icon && registered_class_process_id == window->owner_process_id)
        icon = registered_class.icon;
    if (!win32_icon_get(icon, &pixels, &width, &height)) {
        pixels = NULL;
        width = height = 0;
    }
    window->native->icon_pixels = pixels;
    window->native->icon_width = (uint16_t)width;
    window->native->icon_height = (uint16_t)height;
    window->native->dirty = true;
}

/* BLES_WINE_DIALOG_UI_PERF_FIX_20260723 */
static void win32_window_font_values(win_window_t *window, int *height,
                                     bool *bold, bool *italic,
                                     bool *monospace) {
    int h = window && window->font_pixel_height ? window->font_pixel_height : 8;
    bool b = window && window->font_bold;
    bool i = window && window->font_italic;
    bool m = window && window->font_monospace;
    if (window && window->font)
        (void)win32_gdi_font_query(window->font, &h, &b, &i, &m);
    if (h < 8) h = 8;
    if (h > 32) h = 32;
    if (height) *height = h;
    if (bold) *bold = b;
    if (italic) *italic = i;
    if (monospace) *monospace = m;
}
static uint32_t win32_prepare_caption(const char *source, char *destination,
                                      uint32_t capacity, bool keep_newlines,
                                      bool no_prefix) {
    uint32_t used = 0;
    if (!destination || !capacity) return 0;
    if (!source) source = "";
    while (*source && used + 1U < capacity) {
        char c = *source++;
        if (c == '\r') continue;
        if (c == '\n') {
            if (keep_newlines) destination[used++] = c;
            else destination[used++] = ' ';
            continue;
        }
        if (c == '&' && !no_prefix) {
            if (*source == '&') { destination[used++] = '&'; source++; }
            continue;
        }
        destination[used++] = c;
    }
    destination[used] = '\0';
    return used;
}
static void win32_draw_control_line(gui_surface_t *surface, gui_rect_t rect,
                                    const char *line, uint32_t length,
                                    uint32_t color, int height, bool bold,
                                    bool italic, bool monospace,
                                    uint32_t alignment, int y) {
    int width = (int)gui_font_text_width_px(line, length, height, monospace, bold);
    int x = rect.x;
    if (alignment == SS_CENTER) x += (rect.w - width) / 2;
    else if (alignment == SS_RIGHT) x += rect.w - width;
    gui_font_draw_string_px_clipped(surface, x, y, line, length, color,
        height, bold, italic, monospace, rect);
}
static void win32_draw_static_text(gui_surface_t *surface, gui_rect_t rect,
                                   win_window_t *control, uint32_t color) {
    char clean[1024], line[256];
    uint32_t length, position = 0;
    int height; bool bold, italic, monospace;
    uint32_t alignment = control->style & 3U;
    bool wrap = (control->style & SS_TYPEMASK) != SS_SIMPLE &&
                (control->style & SS_TYPEMASK) != SS_LEFTNOWORDWRAP;
    int y = rect.y;
    win32_window_font_values(control, &height, &bold, &italic, &monospace);
    length = win32_prepare_caption(control->text, clean, sizeof(clean), true,
                                   (control->style & SS_NOPREFIX) != 0U);
    while (position < length && y + height <= rect.y + rect.h) {
        uint32_t used = 0, last_space = 0;
        while (position < length && clean[position] != '\n' && used + 1U < sizeof(line)) {
            line[used++] = clean[position++];
            if (line[used - 1U] == ' ') last_space = used;
            if (wrap && gui_font_text_width_px(line, used, height, monospace, bold) > (uint16_t)rect.w) {
                if (last_space) {
                    position -= used - last_space;
                    used = last_space;
                } else if (used > 1U) {
                    position--; used--;
                }
                break;
            }
        }
        while (used && line[used - 1U] == ' ') used--;
        line[used] = '\0';
        if (used) win32_draw_control_line(surface, rect, line, used, color,
                                           height, bold, italic, monospace,
                                           alignment, y);
        if (position < length && clean[position] == '\n') position++;
        y += height + 1;
    }
}
static void win32_draw_static_icon(gui_surface_t *surface, gui_rect_t rect,
                                   void *icon) {
    const uint32_t *pixels; int width, height;
    if (!win32_icon_get(icon, &pixels, &width, &height) || !pixels) return;
    int draw_w = width < rect.w ? width : rect.w;
    int draw_h = height < rect.h ? height : rect.h;
    int left = rect.x + (rect.w - draw_w) / 2;
    int top = rect.y + (rect.h - draw_h) / 2;
    for (int y = 0; y < draw_h; y++) for (int x = 0; x < draw_w; x++) {
        uint32_t pixel = pixels[(y * height / draw_h) * width +
                                (x * width / draw_w)];
        if (pixel >> 24) gui_gfx_putpixel(surface, left + x, top + y,
                                          pixel & 0x00FFFFFFU);
    }
}
static void win32_draw_static_bitmap(gui_surface_t *surface, gui_rect_t rect,
                                     void *bitmap) {
    const uint32_t *pixels;
    int width, height;
    if (!win32_gdi_bitmap_query(bitmap, &width, &height, &pixels) ||
        !pixels || width <= 0 || height <= 0) return;
    int draw_w = width < rect.w ? width : rect.w;
    int draw_h = height < rect.h ? height : rect.h;
    for (int y = 0; y < draw_h; y++)
        for (int x = 0; x < draw_w; x++)
            gui_gfx_putpixel(surface, rect.x + x, rect.y + y,
                pixels[(y * height / draw_h) * width +
                       (x * width / draw_w)] & 0x00FFFFFFU);
}
static void normal_window_paint(gui_window_t *window, gui_surface_t *surface,
                                void *context) {
    win_window_t *owner = (win_window_t *)context;
    gui_rect_t client = gui_window_content_rect(window);
    void *owner_hwnd = (void *)(uintptr_t)(HWND_BASE +
        (uint32_t)(owner - win_windows));
    for (uint32_t n = 0; n < WIN32_MAX_GDI_COMMANDS; n++) {
        gdi_command_t *command = &gdi_commands[n];
        win_window_t *command_window;
        gui_rect_t command_clip = client;
        int origin_x = client.x, origin_y = client.y;
        if (!command->used) continue;
        command_window = window_from_handle(command->hwnd);
        if (command->hwnd != owner_hwnd) {
            gui_rect_t control_rect;
            if (!command_window || !command_window->control ||
                !command_window->visible ||
                command_window->parent != owner_hwnd)
                continue;
            control_rect = (gui_rect_t){
                client.x + command_window->bounds.x,
                client.y + command_window->bounds.y,
                command_window->bounds.w, command_window->bounds.h};
            if (!gui_rect_intersect(control_rect, client, &command_clip))
                continue;
            origin_x = control_rect.x;
            origin_y = control_rect.y;
        }
        if (command->kind == 1U)
            gui_font_draw_string_px_clipped(surface, origin_x + command->x1,
                origin_y + command->y1, command->text,
                (uint32_t)kstrlen(command->text), command->color,
                command->font_height ? command->font_height : 8,
                (command->font_flags & 1U) != 0U,
                (command->font_flags & 2U) != 0U,
                (command->font_flags & 4U) != 0U, command_clip);
        else if (command->kind == 2U)
            gui_gfx_draw_line(surface, origin_x + command->x1,
                origin_y + command->y1, origin_x + command->x2,
                origin_y + command->y2, command->color);
        else if (command->kind == 3U)
            gui_gfx_draw_rect(surface, (gui_rect_t){origin_x + command->x1,
                origin_y + command->y1, command->x2 - command->x1,
                command->y2 - command->y1}, command->color);
        else if (command->kind == 4U)
            gui_gfx_fill_rect(surface, (gui_rect_t){origin_x + command->x1,
                origin_y + command->y1, command->x2 - command->x1,
                command->y2 - command->y1}, command->color);
        else if ((command->kind == 5U || command->kind == 6U) && command->pixels) {
            int width=command->x2-command->x1,height=command->y2-command->y1;
            for(int yy=0;yy<height;yy++)for(int xx=0;xx<width;xx++)
                gui_gfx_putpixel(surface,origin_x+command->x1+xx,origin_y+command->y1+yy,
                    command->pixels[(command->src_y+yy)*command->pitch+command->src_x+xx]);
        } else if (command->kind == 7U && command->pixels) {
            int width = command->x2 - command->x1;
            int height = command->y2 - command->y1;
            int source_width = command->pitch;
            int source_height = (int)command->color;
            for (int yy = 0; yy < height; yy++) for (int xx = 0; xx < width; xx++) {
                uint32_t pixel = command->pixels[
                    (yy * source_height / height) * source_width +
                    (xx * source_width / width)];
                if (pixel >> 24)
                    gui_gfx_putpixel(surface, origin_x + command->x1 + xx,
                        origin_y + command->y1 + yy, pixel & 0x00FFFFFFU);
            }
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
            {
                char caption[256]; int fh; bool fb, fi, fm;
                uint32_t length = win32_prepare_caption(control->text, caption,
                    sizeof(caption), false, false);
                win32_window_font_values(control, &fh, &fb, &fi, &fm);
                tx = rect.x + (rect.w - (int)gui_font_text_width_px(
                    caption, length, fh, fm, fb)) / 2;
                gui_font_draw_string_px_clipped(surface, tx + offset,
                    rect.y + (rect.h - fh) / 2 + offset, caption, length,
                    control->enabled ? 0x00101010U : 0x00808080U,
                    fh, fb, fi, fm, rect);
            }
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
                uint32_t face =
                    (control->pressed && control->selection_start == b) ?
                    0x00B8B8B8U :
                    ((control->toolbar_states[b] & 0x04U) ?
                     0x00E0E0E0U : 0x00C8C8C8U);
                gui_gfx_fill_rect(surface, br, face);
                gui_gfx_draw_rect(surface, br, 0x00808080U);
                int image = control->toolbar_bitmap_indices[b];
                int iw = control->toolbar_bitmap_width;
                int ih = control->toolbar_bitmap_height;
                if (control->toolbar_pixels && image >= 0 &&
                    image < control->toolbar_bitmap_count && iw > 0 && ih > 0) {
                    int dx = br.x + (br.w - iw) / 2;
                    int dy = br.y + (br.h - ih) / 2;
                    const uint32_t *pixels = control->toolbar_pixels +
                        (uint32_t)image * (uint32_t)iw * (uint32_t)ih;
                    uint32_t transparent = pixels[0] & 0x00FFFFFFU;
                    for (int y = 0; y < ih; y++)
                        for (int x = 0; x < iw; x++) {
                            uint32_t color =
                                pixels[(uint32_t)y * (uint32_t)iw +
                                       (uint32_t)x] & 0x00FFFFFFU;
                            if (color != transparent &&
                                dx + x >= br.x && dx + x < br.x + br.w &&
                                dy + y >= br.y && dy + y < br.y + br.h)
                                gui_gfx_putpixel(surface, dx + x, dy + y,
                                    (control->toolbar_states[b] & 0x04U) ?
                                    color : 0x00808080U);
                        }
                } else {
                    const uint32_t *pixels = NULL;
                    if (win32_comctl_image_list_get_pixels(
                            control->listview_image_lists[0], image,
                            &pixels, &iw, &ih) && pixels) {
                        int dx = br.x + (br.w - iw) / 2;
                        int dy = br.y + (br.h - ih) / 2;
                        uint32_t transparent = pixels[0] & 0x00FFFFFFU;
                        for (int y = 0; y < ih; y++)
                            for (int x = 0; x < iw; x++) {
                                uint32_t color =
                                    pixels[(uint32_t)y * (uint32_t)iw +
                                           (uint32_t)x] & 0x00FFFFFFU;
                                if (color != transparent &&
                                    dx + x >= br.x && dx + x < br.x + br.w &&
                                    dy + y >= br.y && dy + y < br.y + br.h)
                                    gui_gfx_putpixel(surface, dx + x, dy + y,
                                        (control->toolbar_states[b] & 0x04U) ?
                                        color : 0x00808080U);
                            }
                    }
                }
                bx += bw;
            }
        } else if (control->kind == 5U) {
            uint32_t background = control->status_background ?
                control->status_background : 0x00D8D8D8U;
            gui_gfx_fill_rect(surface, rect, background);
            gui_gfx_draw_line(surface, rect.x, rect.y, rect.x + rect.w - 1,
                              rect.y, 0x00808080U);
            int left = rect.x;
            uint32_t parts = control->status_simple ? 1U :
                (control->status_part_count ? control->status_part_count : 1U);
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
        } else if (control->kind == 6U || control->kind == 7U || control->kind == 9U || control->kind == 10U || control->kind == 11U) {
            int arrow_width = control->kind == 7U ? 18 : 0;
            int line_height = 16;
            int visible = control->kind == 7U ? 1 : (rect.h - 4) / line_height;
            int top = control->kind == 7U ? control->control_cur_sel
                                          : control->control_top_index;
            if (top < 0) top = 0;
            if (visible < 1) visible = 1;
            gui_gfx_fill_rect(surface, rect, 0x00FFFFFFU);
            gui_gfx_draw_rect(surface, rect, control->focused ? 0x00000080U : 0x00606060U);
            for (int row = 0; row < visible; row++) {
                int item = top + row;
                gui_rect_t item_rect = {rect.x + 2, rect.y + 2 + row * line_height,
                                        rect.w - 4 - arrow_width, line_height};
                if (item < 0 || item >= control->control_item_count) break;
                if (item == control->control_cur_sel)
                    gui_gfx_fill_rect(surface,item_rect,0x000060C0U);
                gui_font_draw_string_clipped(surface,item_rect.x+2,item_rect.y+3,
                    control->control_items[item],item==control->control_cur_sel?
                    0x00FFFFFFU:0x00101010U,item_rect);
            }
            if (control->kind == 7U) {
                gui_rect_t arrow = {rect.x + rect.w - 18, rect.y + 1, 17, rect.h - 2};
                gui_gfx_fill_rect(surface,arrow,0x00D8D8D8U);
                gui_gfx_draw_rect(surface,arrow,0x00606060U);
                gui_gfx_draw_line(surface,arrow.x+5,arrow.y+arrow.h/2-2,
                    arrow.x+8,arrow.y+arrow.h/2+1,0x00000000U);
                gui_gfx_draw_line(surface,arrow.x+8,arrow.y+arrow.h/2+1,
                    arrow.x+11,arrow.y+arrow.h/2-2,0x00000000U);
            }
        } else if (control->kind == 8U) {
            int range=control->scroll_max-control->scroll_min;
            int value=control->scroll_pos-control->scroll_min;
            int fill=range>0?(rect.w-4)*value/range:0;
            if(fill<0)fill=0;
            if(fill>rect.w-4)fill=rect.w-4;
            gui_gfx_fill_rect(surface,rect,0x00FFFFFFU);
            gui_gfx_draw_rect(surface,rect,0x00606060U);
            if(fill)gui_gfx_fill_rect(surface,
                (gui_rect_t){rect.x+2,rect.y+2,fill,rect.h-4},0x000080C0U);
        } else {
            if ((control->style & SS_TYPEMASK) == SS_ICON && control->large_icon)
                win32_draw_static_icon(surface, rect, control->large_icon);
            else if ((control->style & SS_TYPEMASK) == SS_BITMAP &&
                     control->large_icon)
                win32_draw_static_bitmap(surface, rect, control->large_icon);
            else
                win32_draw_static_text(surface, rect, control,
                    control->enabled ? 0x00101010U : 0x00808080U);
        }
    }
}
static gdi_command_t*gdi_slot(void*hwnd,uint8_t kind){if(!window_from_handle(hwnd))return NULL;for(uint32_t i=0;i<WIN32_MAX_GDI_COMMANDS;i++)if(!gdi_commands[i].used){kmemset(&gdi_commands[i],0,sizeof(gdi_commands[i]));gdi_commands[i].used=true;gdi_commands[i].hwnd=hwnd;gdi_commands[i].kind=kind;return &gdi_commands[i];}return NULL;}
void win32_gdi_begin(void*hwnd){for(uint32_t i=0;i<WIN32_MAX_GDI_COMMANDS;i++)if(gdi_commands[i].used&&gdi_commands[i].hwnd==hwnd&&gdi_commands[i].kind!=6U){if(gdi_commands[i].pixels)kfree(gdi_commands[i].pixels);gdi_commands[i].used=false;}}
static void win32_gdi_dirty(win_window_t *window) {
    if (!window || !window->native) return;
    window->native->dirty = true;
    if (!window->paint_active) gui_request_paint();
}
bool win32_gdi_text_ex(void*hwnd,int x,int y,const char*text,uint32_t color,int pixel_height,bool bold,bool italic,bool monospace){gdi_command_t*c=gdi_slot(hwnd,1);win_window_t*w=window_from_handle(hwnd);if(!c||!w)return false;c->x1=x;c->y1=y;c->color=color;c->font_height=(int16_t)pixel_height;c->font_flags=(bold?1U:0U)|(italic?2U:0U)|(monospace?4U:0U);kstrncpy(c->text,text?text:"",sizeof(c->text)-1U);win32_gdi_dirty(w);return true;}
bool win32_gdi_text(void*hwnd,int x,int y,const char*text,uint32_t color){return win32_gdi_text_ex(hwnd,x,y,text,color,8,false,false,false);}
bool win32_gdi_line(void*hwnd,int x1,int y1,int x2,int y2,uint32_t color){gdi_command_t*c=gdi_slot(hwnd,2);if(!c)return false;c->x1=x1;c->y1=y1;c->x2=x2;c->y2=y2;c->color=color;win32_gdi_dirty(window_from_handle(hwnd));return true;}
bool win32_gdi_rect(void*hwnd,int l,int t,int r,int b,uint32_t color){gdi_command_t*c=gdi_slot(hwnd,3);if(!c)return false;c->x1=l;c->y1=t;c->x2=r;c->y2=b;c->color=color;win32_gdi_dirty(window_from_handle(hwnd));return true;}
bool win32_gdi_fill_rect(void*hwnd,int l,int t,int r,int b,uint32_t color){gdi_command_t*c=gdi_slot(hwnd,4);if(!c)return false;c->x1=l;c->y1=t;c->x2=r;c->y2=b;c->color=color;win32_gdi_dirty(window_from_handle(hwnd));return true;}
bool win32_gdi_blit(void*hwnd,int dx,int dy,int w,int h,const uint32_t*pixels,int pitch,int sx,int sy){if(!pixels||w<=0||h<=0||pitch<=0)return false;gdi_command_t*c=gdi_slot(hwnd,5);if(!c)return false;uint32_t count=(uint32_t)pitch*(uint32_t)(sy+h);c->pixels=(uint32_t*)kmalloc(count*sizeof(uint32_t));if(!c->pixels){c->used=false;return false;}kmemcpy(c->pixels,pixels,count*sizeof(uint32_t));c->x1=dx;c->y1=dy;c->x2=dx+w;c->y2=dy+h;c->pitch=pitch;c->src_x=sx;c->src_y=sy;win32_gdi_dirty(window_from_handle(hwnd));return true;}
bool win32_directdraw_blit(void*hwnd,int w,int h,const uint32_t*pixels){gdi_command_t*c=NULL;uint32_t count;if(!window_from_handle(hwnd)||!pixels||w<=0||h<=0)return false;for(uint32_t i=0;i<WIN32_MAX_GDI_COMMANDS;i++)if(gdi_commands[i].used&&gdi_commands[i].kind==6U&&gdi_commands[i].hwnd==hwnd){c=&gdi_commands[i];break;}if(!c)c=gdi_slot(hwnd,6);if(!c)return false;count=(uint32_t)w*(uint32_t)h;if(c->pixel_capacity<count){uint32_t*replacement=(uint32_t*)kmalloc(count*sizeof(uint32_t));if(!replacement)return false;if(c->pixels)kfree(c->pixels);c->pixels=replacement;c->pixel_capacity=count;}kmemcpy(c->pixels,pixels,count*sizeof(uint32_t));c->x1=c->y1=c->src_x=c->src_y=0;c->x2=w;c->y2=h;c->pitch=w;win32_gdi_dirty(window_from_handle(hwnd));return true;}

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
        w->toolbar_bitmap_indices[i] =
            buttons ? (int16_t)buttons[i].iBitmap : -1;
        w->toolbar_commands[i] = buttons ? (uint16_t)buttons[i].idCommand : 0U;
        w->toolbar_states[i] = buttons ? buttons[i].fsState : 0x04U;
        w->toolbar_styles[i] = buttons ? buttons[i].fsStyle : 0U;
    }
    if (w->native) { w->native->dirty = true; gui_request_paint(); }
    return true;
}

static int toolbar_add_bitmap(win_window_t *w, uint32_t requested_count,
                              void *bitmap_handle) {
    const uint32_t *source;
    uint32_t *replacement;
    int source_width, source_height;
    int cell_width, cell_height;
    uint32_t old_count, count;

    if (!w || !bitmap_handle ||
        !win32_gdi_bitmap_query(bitmap_handle, &source_width,
                                &source_height, &source))
        return -1;

    cell_width = w->toolbar_bitmap_width > 0 ?
        w->toolbar_bitmap_width : 16;
    cell_height = w->toolbar_bitmap_height > 0 ?
        w->toolbar_bitmap_height : source_height;
    count = requested_count;
    if (!count && cell_width > 0)
        count = (uint32_t)(source_width / cell_width);
    if (!count || source_width < (int)count * cell_width ||
        source_height < cell_height)
        return -1;

    old_count = w->toolbar_bitmap_count;
    if (old_count + count > 128U) count = 128U - old_count;
    if (!count) return -1;
    replacement = (uint32_t *)kmalloc(
        (old_count + count) * (uint32_t)cell_width *
        (uint32_t)cell_height * sizeof(uint32_t));
    if (!replacement) return -1;
    if (w->toolbar_pixels && old_count)
        kmemcpy(replacement, w->toolbar_pixels,
            old_count * (uint32_t)cell_width * (uint32_t)cell_height *
            sizeof(uint32_t));
    for (uint32_t image = 0; image < count; image++)
        for (int y = 0; y < cell_height; y++)
            kmemcpy(replacement +
                    (old_count + image) * (uint32_t)cell_width *
                        (uint32_t)cell_height +
                    (uint32_t)y * (uint32_t)cell_width,
                    source + (uint32_t)y * (uint32_t)source_width +
                        image * (uint32_t)cell_width,
                    (uint32_t)cell_width * sizeof(uint32_t));
    if (w->toolbar_pixels) kfree(w->toolbar_pixels);
    w->toolbar_pixels = replacement;
    w->toolbar_bitmap_width = (int16_t)cell_width;
    w->toolbar_bitmap_height = (int16_t)cell_height;
    w->toolbar_bitmap_count = (uint16_t)(old_count + count);
    return (int)old_count;
}

static int toolbar_button_at(const win_window_t *w, int local_x,
                             int local_y) {
    int x = 3;
    int width;
    int height;
    if (!w) return -1;
    width = w->toolbar_button_width > 0 ? w->toolbar_button_width : 24;
    height = w->toolbar_button_height > 0 ?
        w->toolbar_button_height : w->bounds.h - 4;
    if (local_y < 2 || local_y >= 2 + height) return -1;
    for (uint32_t i = 0; i < w->toolbar_count && i < 32U; i++) {
        int item_width = (w->toolbar_styles[i] & 0x01U) ?
            width / 2 : width;
        if (!(w->toolbar_styles[i] & 0x01U) &&
            !(w->toolbar_states[i] & 0x08U) &&
            local_x >= x && local_x < x + item_width)
            return (int)i;
        x += item_width;
    }
    return -1;
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
            return rich_format_set(w->rich_char_format,
                                   sizeof(w->rich_char_format),
                                   (const void *)(uintptr_t)lp);
        case EM_SETPARAFORMAT:
            return rich_format_set(w->rich_para_format,
                                   sizeof(w->rich_para_format),
                                   (const void *)(uintptr_t)lp);
        case EM_GETCHARFORMAT:
            return rich_format_get(w->rich_char_format,
                                   sizeof(w->rich_char_format),
                                   (void *)(uintptr_t)lp);
        case EM_GETPARAFORMAT:
            return rich_format_get(w->rich_para_format,
                                   sizeof(w->rich_para_format),
                                   (void *)(uintptr_t)lp);
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
            return edit_undo(w) ? 1 : 0;
        case EM_CANREDO:
            return w->undo_buffer ? 1 : 0;
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
            uint8_t chunk[1024];
            char *input = NULL, *text = NULL;
            uint32_t length = 0U, capacity = 0U, text_length = 0U;
            int32_t got = 0;
            if (!stream || !stream->callback) return 0;
            stream->error = 0;
            for (;;) {
                got = 0;
                stream->error = stream->callback(stream->cookie, chunk,
                    (int32_t)sizeof(chunk), &got);
                if (stream->error || got <= 0) break;
                if (length + (uint32_t)got + 1U < length) { stream->error = 8U; break; }
                if (length + (uint32_t)got + 1U > capacity) {
                    uint32_t next = capacity ? capacity * 2U : 2048U;
                    char *grown;
                    while (next < length + (uint32_t)got + 1U) next *= 2U;
                    grown = input ? (char *)krealloc(input, next) : (char *)kmalloc(next);
                    if (!grown) { stream->error = 8U; break; }
                    input = grown; capacity = next;
                }
                kmemcpy(input + length, chunk, (uint32_t)got);
                length += (uint32_t)got;
            }
            if (!stream->error) {
                if (!input) { input = (char *)kmalloc(1U); if (input) input[0] = 0; }
                else input[length] = 0;
                if (wp & SF_RTF) text = rich_rtf_to_text(input, length, &text_length);
                else { text = input; input = NULL; text_length = length; }
                if (!text || !edit_set_text_internal(w, "", false) ||
                    !edit_replace(w, 0U, 0U, text, text_length, false, false))
                    stream->error = 8U;
            }
            if (input) kfree(input);
            if (text) kfree(text);
            return stream->error ? 0 : (int32_t)w->edit_length;
        }
        case EM_STREAMOUT: {
            win_edit_stream_t *stream = (win_edit_stream_t *)(uintptr_t)lp;
            char *output = NULL;
            const char *source = edit_text(w);
            uint32_t length = w->edit_length, position = 0U;
            if (!stream || !stream->callback) return 0;
            stream->error = 0;
            if (wp & SF_RTF) {
                output = rich_text_to_rtf(source, length, &length);
                if (!output) { stream->error = 8U; return 0; }
                source = output;
            }
            while (position < length) {
                int32_t ask = (int32_t)(length - position), sent = 0;
                if (ask > 1024) ask = 1024;
                stream->error = stream->callback(stream->cookie,
                    (uint8_t *)source + position, ask, &sent);
                if (stream->error || sent <= 0) break;
                position += (uint32_t)sent;
            }
            if (output) kfree(output);
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
                control->parent != hwnd || !control->enabled ||
                control->kind == 12U ||
                (control->kind == 3U &&
                 !(control->style & SS_NOTIFY))) continue;
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
                (void)win32_call_or_queue_wndproc(
                    control->proc, control_hwnd, WM_KILLFOCUS,
                    (uint32_t)(uintptr_t)hit_hwnd, 0, NULL, 0U, -1);
            control->focused = control == hit;
        }

        if (hit) {
            if (hit->proc)
                (void)win32_call_or_queue_wndproc(
                    hit->proc, hit_hwnd, WM_SETFOCUS, 0U, 0,
                    NULL, 0U, -1);

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
            } else if (hit->kind == 4U) {
                int button = toolbar_button_at(hit,
                    event->x - (client.x + hit->bounds.x),
                    event->y - (client.y + hit->bounds.y));
                if (button >= 0 &&
                    (hit->toolbar_states[button] & 0x04U)) {
                    hit->selection_start = (uint32_t)button;
                    hit->pressed = true;
                }
            } else if (hit->kind == 6U || hit->kind == 7U || hit->kind == 9U || hit->kind == 10U || hit->kind == 11U) {
                int selected;
                if (hit->kind == 7U) {
                    selected = hit->control_item_count ?
                        (hit->control_cur_sel + 1) % hit->control_item_count : -1;
                } else {
                    selected = hit->control_top_index +
                        (event->y - (client.y + hit->bounds.y) - 2) / 16;
                    if (selected >= hit->control_item_count) selected = -1;
                }
                if (selected >= 0) {
                    hit->control_cur_sel = (int16_t)selected;
                    queue_message(hwnd,WM_COMMAND,
                        hit->id | ((hit->kind==7U?CBN_SELCHANGE:LBN_SELCHANGE)<<16),
                        (int32_t)(uintptr_t)hit_hwnd);
                }
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
            if (control->kind == 4U && control->pressed) {
                int button = toolbar_button_at(control,
                    event->x - (client.x + control->bounds.x),
                    event->y - (client.y + control->bounds.y));
                uint32_t pressed = control->selection_start;
                control->pressed = false;
                if (button >= 0 && (uint32_t)button == pressed &&
                    pressed < control->toolbar_count &&
                    (control->toolbar_states[pressed] & 0x04U))
                    queue_message(hwnd, WM_COMMAND,
                        control->toolbar_commands[pressed],
                        (int32_t)(HWND_BASE + i));
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

        for (uint32_t i = 0; i < WIN32_MAX_WINDOWS; i++) {
            win_window_t *control=&win_windows[i];
            if(!control->used||!control->control||!control->focused||
               control->parent!=hwnd||(control->kind!=6U&&control->kind!=7U&&control->kind!=9U&&control->kind!=10U&&control->kind!=11U))continue;
            if((vk==VK_UP||vk==VK_DOWN)&&control->control_item_count){
                int selected=control->control_cur_sel;
                if(selected<0)selected=0;
                else if(vk==VK_UP&&selected>0)selected--;
                else if(vk==VK_DOWN&&selected+1<control->control_item_count)selected++;
                control->control_cur_sel=(int16_t)selected;
                if(control->kind==6U){
                    int visible=(control->bounds.h-4)/16;if(visible<1)visible=1;
                    if(selected<control->control_top_index)control->control_top_index=(int16_t)selected;
                    if(selected>=control->control_top_index+visible)
                        control->control_top_index=(int16_t)(selected-visible+1);
                }
                queue_message(hwnd,WM_COMMAND,control->id|
                    ((control->kind==7U?CBN_SELCHANGE:LBN_SELCHANGE)<<16),
                    (int32_t)(HWND_BASE+i));
                window->dirty=true;gui_request_paint();return true;
            }
        }

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
                    (void)win32_call_or_queue_wndproc(
                        target->proc, target_hwnd, WM_SETFOCUS, 0U, 0,
                        NULL, 0U, -1);
            }
        }

        if (target) {
            /* The native keyboard driver reports Enter as LF.  Win32 edit
             * controls receive VK_RETURN followed by a CR WM_CHAR. */
            uint8_t character = key == '\n' ? '\r' : key;
            bool produces_char = (character >= 32U && character < 127U) ||
                                 character == 8U || character == 9U ||
                                 character == 13U;

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
                    (void)win32_call_or_queue_wndproc(
                        active_proc, target_hwnd, WM_KEYDOWN, vk, 0,
                        NULL, 0U, -1);

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
                        (void)win32_call_or_queue_wndproc(
                            active_proc, target_hwnd, WM_CHAR, character, 0,
                            NULL, 0U, -1);

                    if (target->edit_length == before_length &&
                        target->edit_caret == before_caret &&
                        target->edit_anchor == before_anchor &&
                        target->edit_modified == before_modified &&
                        (target->default_proc || win32_is_edit_control(target)) &&
                        target->default_proc != active_proc) {
                        (target->default_proc ? target->default_proc : win32_EditWndProc)(
                            target_hwnd, WM_CHAR, character, 0);
                    }

                    if (target->edit_length == before_length &&
                        target->edit_caret == before_caret &&
                        target->edit_anchor == before_anchor &&
                        target->edit_modified == before_modified) {
                        (void)win32_edit_force_char(target, character);
                    }
                }
                return true;
            }

            /* Ctrl/Alt combinations must still pass through the application
             * message loop so accelerators and menu commands keep working. */
            queue_message(target_hwnd, WM_KEYDOWN, vk, 0);
            if (character != 0U)
                queue_message(target_hwnd, WM_CHAR, character, 0);
            return true;
        }

        queue_message(hwnd, WM_KEYDOWN, vk, 0);
        {
            uint8_t character = key == '\n' ? '\r' : key;
            if ((character >= 32U && character < 127U) ||
                character == 8U || character == 9U || character == 13U)
                queue_message(hwnd, WM_CHAR, character, 0);
        }
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
        /* WIN32_RING3_WM_MOVE */
        (void)win32_queue_wndproc_upcall(
            w->proc, hwnd, WM_MOVE, 0U,
            (int32_t)(((uint32_t)(uint16_t)y << 16) |
                      (uint16_t)x),
            NULL, 0U, -1);
    }

    if (sized) {
        client = win32_effective_client_rect(w);
        /* WIN32_RING3_WM_SIZE */
        (void)win32_queue_wndproc_upcall(
            w->proc, hwnd, WM_SIZE, 0U,
            (int32_t)(((uint32_t)(uint16_t)client.h << 16) |
                      (uint16_t)client.w),
            NULL, 0U, -1);
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

static win_class_t *win32_find_class(const char *name) {
    uint32_t process_id = task_current_process_id();
    if (!name) return NULL;
    for (uint32_t i = 0; i < WIN32_MAX_CLASSES; i++)
        if (registered_classes[i].used &&
            registered_classes[i].owner_process_id == process_id &&
            equal_ci(registered_classes[i].name, name)) return &registered_classes[i];
    return NULL;
}
static uint16_t WIN32_API win32_RegisterClassA(const wndclass_a_t *wc){
    win_class_t *entry;
    if(!wc||!wc->proc||!wc->name)return 0;
    entry=win32_find_class(wc->name);
    if(entry)return(uint16_t)((entry-registered_classes)+1U);
    for(uint32_t i=0;i<WIN32_MAX_CLASSES;i++)if(!registered_classes[i].used){
        entry=&registered_classes[i];kmemset(entry,0,sizeof(*entry));entry->used=true;
        entry->owner_process_id=task_current_process_id();
        entry->definition=*wc;kstrncpy(entry->name,wc->name,sizeof(entry->name)-1U);
        entry->definition.name=entry->name;
        registered_class=entry->definition;kstrncpy(registered_name,entry->name,sizeof(registered_name)-1U);
        registered_class.name=registered_name;
        registered_class_process_id=task_current_process_id();
        return(uint16_t)(i+1U);
    }
    return 0;
}
static uint16_t WIN32_API win32_RegisterClassExA(const void *raw){const uint8_t *p=(const uint8_t*)raw;return p?win32_RegisterClassA((const wndclass_a_t*)(p+4)):0;}
static int WIN32_API win32_UnregisterClassA(const char *name,
                                             void *instance UNUSED) {
    win_class_t *entry = win32_find_class(name);
    if (!entry) return 0;
    kmemset(entry, 0, sizeof(*entry));
    return 1;
}
static int WIN32_API win32_GetClassInfoA(void *instance,
                                          const char *name,
                                          wndclass_a_t *out) {
    win_class_t *entry = win32_find_class(name);

    if (!out || !name) return 0;

    if (entry) {
        *out = entry->definition;
        return 1;
    }

    /*
     * Wine registers ReBarWindow32 as a global common-control class.
     * WinZip asks GetClassInfoA for it before CreateWindowExA; returning
     * FALSE leaves its rebar HWND at NULL and triggers WzTBar.c:1526.
     */
    if (equal_ci(name, "ReBarWindow32")) {
        kmemset(out, 0, sizeof(*out));
        out->style = 0x00000003U;
        out->proc = win32_ReBarWndProc;
        out->win_extra = (int)sizeof(void *);
        out->instance = instance;
        out->name = name;
        kprintf("[USER32:REBAR] GetClassInfoA class=%s proc=%x extra=%d\n",
                name, (uint32_t)(uintptr_t)out->proc, out->win_extra);
        return 1;
    }

    return 0;
}
static int WIN32_API win32_GetClassInfoExA(void *instance,
                                            const char *name, void *raw) {
    uint8_t *bytes = (uint8_t *)raw;
    wndclass_a_t value;
    if (!bytes || !win32_GetClassInfoA(instance, name, &value)) return 0;
    kmemcpy(bytes + 4U, &value, sizeof(value));
    return 1;
}

static void win32_fit_classic_wizard_dialog(void *parent) {
    win_window_t *dialog = window_from_handle(parent);
    gui_desktop_t *desktop = gui_get_desktop();
    int page_right = 0, page_bottom = 0;
    int client_width, navigation_y;
    win_window_t *back = NULL, *next = NULL, *close = NULL;
    win_window_t *separator = NULL;

    if (!dialog || !dialog->dialog || !dialog->native) return;
    for (uint32_t n = 0U; n < WIN32_MAX_WINDOWS; n++) {
        win_window_t *child = &win_windows[n];
        if (!child->used || !child->control || child->parent != parent)
            continue;
        if (child->id == 8101U) back = child;
        else if (child->id == 8102U) next = child;
        else if (child->id == 8103U) close = child;
        else if (child->id == 999U) separator = child;
        if (!child->visible ||
            (child->id >= 8101U && child->id <= 8107U) ||
            child->id == 999U || child->id == 8104U ||
            child->id == 8106U)
            continue;
        if (child->bounds.x + child->bounds.w > page_right)
            page_right = child->bounds.x + child->bounds.w;
        if (child->bounds.y + child->bounds.h > page_bottom)
            page_bottom = child->bounds.y + child->bounds.h;
    }
    if (!back || !next || !close || page_right <= 0) return;

    client_width = dialog->native->bounds.w - 4;
    if (client_width < page_right + 14) client_width = page_right + 14;
    if (desktop && client_width > (int)desktop->surface.width - 8)
        client_width = (int)desktop->surface.width - 8;
    navigation_y = page_bottom + 25;
    if (navigation_y < 265) navigation_y = 265;

    close->bounds.x = client_width - close->bounds.w - 5;
    next->bounds.x = close->bounds.x - next->bounds.w - 10;
    back->bounds.x = next->bounds.x - back->bounds.w;
    back->bounds.y = next->bounds.y = close->bounds.y = navigation_y;
    for (uint32_t n = 0U; n < WIN32_MAX_WINDOWS; n++) {
        win_window_t *child = &win_windows[n];
        if (!child->used || !child->control || child->parent != parent)
            continue;
        if (child->id == 8105U || child->id == 8107U)
            child->bounds.y = navigation_y;
    }
    if (separator) {
        separator->bounds.y = navigation_y - 15;
        separator->bounds.w = client_width - separator->bounds.x - 3;
    }

    dialog->native->bounds.w = client_width + 4;
    dialog->native->bounds.h = navigation_y + 49;
    if (desktop) {
        dialog->native->bounds.x =
            ((int)desktop->surface.width - dialog->native->bounds.w) / 2;
        if (dialog->native->bounds.x < 0) dialog->native->bounds.x = 0;
        if (dialog->native->bounds.y + dialog->native->bounds.h >
            (int)desktop->surface.height - 28)
            dialog->native->bounds.y =
                (int)desktop->surface.height - 28 -
                dialog->native->bounds.h;
        if (dialog->native->bounds.y < 0) dialog->native->bounds.y = 0;
    }
    dialog->native->dirty = true;
}

static void *WIN32_API win32_CreateWindowExA(uint32_t exstyle,
                                               const char *class_name,
                                               const char *title,
                                               uint32_t style,
                                               int x, int y, int w, int h,
                                               void *parent, void *menu,
                                               void *instance, void *param) {
    gui_desktop_t *desktop = gui_get_desktop();
    win_window_t *pw = window_from_handle(parent);
    win_class_t *class_entry = win32_find_class(class_name);
    const wndclass_a_t *class_definition = class_entry ?
        &class_entry->definition : &registered_class;
    void *window_menu = menu;
    bool control = is_edit_class(class_name) ||
                   equal_ci(class_name, "BUTTON") ||
                   equal_ci(class_name, "STATIC") ||
                   equal_ci(class_name, "SCROLLBAR") ||
                   is_list_class(class_name) || is_combo_class(class_name) ||
                   equal_ci(class_name, "ToolbarWindow32") ||
                   equal_ci(class_name, "ReBarWindow32") ||
                   is_status_class(class_name) || is_progress_class(class_name) ||
                   is_listview_class(class_name) || is_treeview_class(class_name) ||
                   is_tab_class(class_name);
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

    if (!desktop || (!control && !class_entry &&
                     registered_class_process_id != task_current_process_id()) ||
        (!control && !class_entry &&
                     !equal_ci(class_name, registered_class.name)) ||
        (control && !pw)) return NULL;

    if (!control) {
        if ((uint32_t)x == 0x80000000U) x = 80;
        if ((uint32_t)y == 0x80000000U) y = 60;
        if ((uint32_t)w == 0x80000000U || w <= 0) w = 480;
        if ((uint32_t)h == 0x80000000U || h <= 0) h = 320;
        if (desktop && w > (int)desktop->surface.width - 4) {
            x = 2;
            w = (int)desktop->surface.width - 4;
        } else if (desktop && x + w > (int)desktop->surface.width - 2) {
            x = (int)desktop->surface.width - 2 - w;
            if (x < 2) x = 2;
        }
    }

    for (uint32_t i = 0; i < WIN32_MAX_WINDOWS; i++) {
        void *hwnd;
        win_window_t *ww;

        if (win_windows[i].used) continue;
        ww = &win_windows[i];
        hwnd = (void *)(uintptr_t)(HWND_BASE + i);
        kmemset(ww, 0, sizeof(*ww));
        ww->used = true;
        ww->owner_process_id = control && pw ? pw->owner_process_id :
                               task_current_process_id();
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

        /* RegisterClass.cbWndExtra reserves zero-filled bytes after
         * every custom window instance.  WinZip uses four bytes and
         * accesses them through Get/SetWindowWord at offsets 0 and 2. */
        if (!control && class_definition->win_extra > 0) {
            uint32_t extra = (uint32_t)class_definition->win_extra;
            if (extra > 4096U) {
                kmemset(ww, 0, sizeof(*ww));
                return NULL;
            }
            ww->window_extra = (uint8_t *)kzalloc(extra);
            if (!ww->window_extra) {
                kmemset(ww, 0, sizeof(*ww));
                return NULL;
            }
            ww->window_extra_size = extra;
            kprintf("[WIN32] cbWndExtra hwnd=%x bytes=%u clase=%s\n",
                    (uint32_t)(uintptr_t)hwnd, extra,
                    class_name ? class_name : "");
        }

        if (control) {
            ww->control = true;
            ww->parent = parent;
            ww->native = pw->native;
            ww->bounds = (gui_rect_t){x, y, w, h};
            ww->id = (uint32_t)(uintptr_t)menu;
            ww->kind = is_edit_class(class_name) ? 1U :
                       (equal_ci(class_name, "BUTTON") ? 2U :
                       (equal_ci(class_name, "ToolbarWindow32") ? 4U :
                       (equal_ci(class_name, "ReBarWindow32") ? 12U :
                       (is_status_class(class_name) ? 5U :
                       (is_list_class(class_name) ? 6U :
                       (is_combo_class(class_name) ? 7U :
                       (is_progress_class(class_name) ? 8U :
                       (is_listview_class(class_name) ? 9U :
                       (is_treeview_class(class_name) ? 10U :
                       (is_tab_class(class_name) ? 11U : 3U))))))))));
            ww->control_cur_sel = -1;
            if (ww->kind == 8U) {
                ww->scroll_min = 0; ww->scroll_max = 100;
                ww->scroll_pos = 0; ww->scroll_page = 10;
            }
            if (ww->kind == 12U) {
                gui_rect_t client = gui_window_content_rect(pw->native);

                if ((uint32_t)x == CW_USEDEFAULT) x = 0;
                if ((uint32_t)y == CW_USEDEFAULT) y = 0;
                if ((uint32_t)w == CW_USEDEFAULT || w <= 0) w = client.w;
                if ((uint32_t)h == CW_USEDEFAULT || h <= 0) h = 23;

                ww->bounds = (gui_rect_t){x, y, w, h};
                ww->rebar_background = 0x00D8D8D8U;
                ww->rebar_text_color = 0x00101010U;
                ww->default_proc = win32_ReBarWndProc;
                ww->proc = ww->default_proc;

                kprintf("[USER32:REBAR] CREATE hwnd=%x parent=%x "
                        "style=%x exstyle=%x bounds=%d,%d %dx%d proc=%x\n",
                        (uint32_t)(uintptr_t)hwnd,
                        (uint32_t)(uintptr_t)parent,
                        style, exstyle, x, y, w, h,
                        (uint32_t)(uintptr_t)ww->default_proc);
            }
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

            /* BLES_WINE_STATUSBAR_SUBCLASS_20260723
             *
             * Wine registers msctls_statusbar32 with a real StatusWindowProc.
             * WinZip subclasses that procedure and treats a NULL previous
             * WndProc as a fatal WzSBar error. Give the built-in control a
             * persistent default procedure and initialize its one-part state.
             */
            if (ww->kind == 5U) {
                gui_rect_t client = gui_window_content_rect(pw->native);
                int status_height = 20;
                bool at_top = (style & 0x00000003U) == CCS_TOP;

                if ((uint32_t)h != CW_USEDEFAULT && h > 0)
                    status_height = h;
                if ((uint32_t)x == CW_USEDEFAULT) x = 0;
                if ((uint32_t)w == CW_USEDEFAULT || w <= 0) w = client.w;
                if ((uint32_t)y == CW_USEDEFAULT)
                    y = at_top ? 0 : client.h - status_height;
                if (y < 0) y = 0;

                ww->bounds = (gui_rect_t){x, y, w, status_height};
                ww->status_part_count = 1U;
                ww->status_parts[0] = -1;
                ww->status_background = 0x00D8D8D8U;
                ww->default_proc = win32_StatusBarWndProc;
                ww->proc = ww->default_proc;
                kstrncpy(ww->status_text[0], title ? title : "",
                         sizeof(ww->status_text[0]) - 1U);
                ww->status_text[0][sizeof(ww->status_text[0]) - 1U] = '\0';

                kprintf("[USER32:STATUS] CREATE hwnd=%x parent=%x style=%x "
                        "bounds=%d,%d %dx%d proc=%x\n",
                        (uint32_t)(uintptr_t)hwnd,
                        (uint32_t)(uintptr_t)parent,
                        style, ww->bounds.x, ww->bounds.y,
                        ww->bounds.w, ww->bounds.h,
                        (uint32_t)(uintptr_t)ww->default_proc);
            }

            if (ww->kind == 1U) {
                ww->rich_edit = is_rich_edit_class(class_name);
                ww->rich_background = 0x00FFFFFFU;
                ww->rich_undo_limit = 100U;
                *(uint32_t *)ww->rich_char_format = sizeof(ww->rich_char_format);
                *(uint32_t *)ww->rich_para_format = sizeof(ww->rich_para_format);
                ww->edit_limit = WIN32_EDIT_DEFAULT_LIMIT;
                ww->edit_readonly = (style & ES_READONLY) != 0U;
                ww->default_proc = win32_EditWndProc;
                ww->proc = ww->default_proc;
                if (!edit_set_text_internal(ww, title ? title : "", false)) {
                    cleanup_control(ww);
                    return NULL;
                }
                /* A normal Win32 frame gives its first visible edit child
                 * keyboard focus.  Notepad creates the EDIT during WM_CREATE
                 * and does not issue an extra mouse click first. */
                if (ww->visible && ww->enabled) {
                    bool parent_has_focus = false;
                    for (uint32_t n = 0; n < WIN32_MAX_WINDOWS; n++) {
                        if (win_windows[n].used && win_windows[n].control &&
                            win_windows[n].parent == parent &&
                            win_windows[n].focused) {
                            parent_has_focus = true;
                            break;
                        }
                    }
                    if (!parent_has_focus) ww->focused = true;
                }
                if (ww->rich_edit) win32_fallback_layout_edit(ww);
            } else {
                kstrncpy(ww->text, title ? title : "", sizeof(ww->text) - 1U);
            }
            if (pw->dialog && ww->kind == 3U && ww->id == 8106U) {
                void *wizard_resource = win32_resource_find(
                    instance, (const void *)(uintptr_t)WIN32_RT_BITMAP,
                    "WIZ", 0U, false);
                void *wizard_bitmap =
                    win32_gdi_bitmap_from_resource(wizard_resource);
                if (wizard_bitmap) {
                    ww->large_icon = wizard_bitmap;
                    ww->style =
                        (ww->style & ~SS_TYPEMASK) | SS_BITMAP;
                }
            }
            /*
             * WinZip's setup swaps wizard pages by creating a new group whose
             * heading uses control id 8701.  The original Win95 dialog manager
             * keeps those page groups in separate child-dialog z-order
             * contexts.  Our flattened child controls otherwise leave every
             * older group visible.  Retire the preceding group when its
             * replacement heading is created, preserving the immediately
             * preceding page-background control.
             */
            if (pw->dialog && ww->id == 8701U && i > 0U) {
                int previous_heading = -1;
                for (uint32_t n = 0U; n < i; n++)
                    if (win_windows[n].used && win_windows[n].control &&
                        win_windows[n].parent == parent &&
                        win_windows[n].id == 8701U)
                        previous_heading = (int)n;
                if (previous_heading >= 0) {
                    uint32_t limit = i > 0U ? i - 1U : i;
                    for (uint32_t n = (uint32_t)previous_heading;
                         n < limit; n++)
                        if (win_windows[n].used && win_windows[n].control &&
                            win_windows[n].parent == parent)
                            win_windows[n].visible = false;
                }
            }
            if (pw->dialog) win32_fit_classic_wizard_dialog(parent);
            pw->native->dirty = true;
            return hwnd;
        }

        ww->native = gui_desktop_create_window(desktop, x, y, w, h,
                                                title ? title : "");
        if (!ww->native) {
            if (ww->window_extra) kfree(ww->window_extra);
            kmemset(ww, 0, sizeof(*ww));
            return NULL;
        }
        ww->proc = class_definition->proc;
        ww->native->owner_pid = task_current_pid();
        ww->native->visible = ww->visible;
        ww->large_icon = class_definition->icon;
        win32_update_native_icon(ww);
        gui_window_set_content(ww->native, normal_window_paint, ww);
        gui_window_set_event_handler(ww->native, normal_window_event, ww);
        /* normal_window_* pertenece al kernel y debe ejecutarse en CPL0. Si
         * necesita llamar un WndProc PE, win32_call_or_queue_wndproc lo
         * difiere explicitamente a la tarea propietaria de esta ventana. */

        /* hMenu may be supplied explicitly, or inherited from WNDCLASS.
         * Classic Notepad uses the latter with MAKEINTRESOURCE. */
        if (!window_menu && class_definition->menu) {
            window_menu = load_menu_resource(
                class_definition->instance ? class_definition->instance : instance,
                class_definition->menu, false);
        }
        if (window_menu) (void)win32_SetMenu(hwnd, window_menu);

        /*
         * WIN32_RING3_CREATE_CALLBACKS
         *
         * CreateWindowExA se ejecuta dentro del proxy de syscall. No se puede
         * llamar al WndProc PE directamente desde CPL0. El CREATESTRUCTA se
         * conserva como payload del upcall y lParam se reemplaza por su copia.
         *
         * Esta primera versión asume éxito de WM_NCCREATE/WM_CREATE. Más
         * adelante puede añadirse un upcall síncrono que capture EAX.
         */
        if (ww->proc) {
            create_struct_a_t create = {
                param, instance, window_menu, parent, h, w, y, x, (int32_t)style,
                title, class_name, exstyle
            };

            if (!win32_queue_wndproc_upcall(
                    ww->proc, hwnd, WM_NCCREATE, 0U, 0,
                    &create, (uint8_t)sizeof(create), 3)) {
                cleanup_window(hwnd);
                return NULL;
            }

            if (!win32_queue_wndproc_upcall(
                    ww->proc, hwnd, WM_CREATE, 0U, 0,
                    &create, (uint8_t)sizeof(create), 3)) {
                cleanup_window(hwnd);
                return NULL;
            }

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

    /* Keep the Win32 style state coherent even though BlesKernOS does not
     * yet draw a separate taskbar icon/minimized frame representation. */
    switch (command) {
        case SW_SHOWMINIMIZED:
        case SW_MINIMIZE:
        case SW_SHOWMINNOACTIVE:
        case SW_FORCEMINIMIZE:
            w->style = (w->style | WS_MINIMIZE) & ~WS_MAXIMIZE;
            break;
        case SW_SHOWMAXIMIZED:
            w->style = (w->style | WS_MAXIMIZE) & ~WS_MINIMIZE;
            break;
        case SW_SHOWNORMAL:
        case SW_SHOWNOACTIVATE:
        case SW_SHOW:
        case SW_SHOWNA:
        case SW_RESTORE:
        case SW_SHOWDEFAULT:
            w->style &= ~(WS_MINIMIZE | WS_MAXIMIZE);
            break;
        default:
            break;
    }
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
static int WIN32_API win32_UpdateWindow(void *hwnd){win_window_t*w=window_from_handle(hwnd);if(!w)return 0;queue_message(hwnd,WM_PAINT,0,0);w->native->dirty=true;gui_request_paint();return 1;}
static int WIN32_API win32_DestroyWindow(void *hwnd){win_window_t*w=window_from_handle(hwnd);if(!w)return 0;if(w->control){cleanup_control(w);return 1;}queue_message(hwnd,WM_DESTROY,0,0);gui_window_close(w->native);return 1;}
static void WIN32_API win32_PostQuitMessage(int code){queue_message(NULL,WM_QUIT,(uint32_t)code,0);}
static bool message_matches(const winmsg_t *message, void *hwnd,
                            uint32_t minimum, uint32_t maximum) {
    if (!message) return false;
    if (message->message == WM_QUIT) return true;
    if (hwnd == (void *)(intptr_t)-1) {
        if (message->hwnd != NULL) return false;
    } else if (hwnd && message->hwnd != hwnd) return false;
    if (minimum == 0U && maximum == 0U) return true;
    return message->message >= minimum && message->message <= maximum;
}
static bool message_queue_take(winmsg_t *out, void *hwnd, uint32_t minimum,
                               uint32_t maximum, bool remove) {
    uint8_t index = message_head;
    while (index != message_tail) {
        if (message_matches(&message_queue[index], hwnd, minimum, maximum)) {
            if (out) *out = message_queue[index];
            if (remove) {
                uint8_t next = (uint8_t)((index + 1U) % WIN32_MESSAGE_QUEUE);
                while (next != message_tail) {
                    message_queue[index] = message_queue[next];
                    index = next;
                    next = (uint8_t)((next + 1U) % WIN32_MESSAGE_QUEUE);
                }
                message_tail = (uint8_t)((message_tail + WIN32_MESSAGE_QUEUE - 1U) % WIN32_MESSAGE_QUEUE);
            }
            return true;
        }
        index = (uint8_t)((index + 1U) % WIN32_MESSAGE_QUEUE);
    }
    return false;
}
static void win32_pump_timers_and_destroy(void) {
    uint32_t now=pit_get_ticks(),hz=pit_get_frequency_hz();
    win32_winsock_poll();
    win32_winmm_poll();
    for(uint32_t i=0;i<8U;i++)if(win_timers[i].used&&(int32_t)(now-win_timers[i].next)>=0){queue_message(win_timers[i].hwnd,WM_TIMER,win_timers[i].id,(int32_t)(uintptr_t)win_timers[i].callback);win_timers[i].next=now+(win_timers[i].interval*(hz?hz:100U)+999U)/1000U;}
    for(uint32_t i=0;i<WIN32_MAX_WINDOWS;i++)if(win_windows[i].used&&!win_windows[i].control&&!win_windows[i].native->visible&&!win_windows[i].destroy_sent){win_windows[i].destroy_sent=true;queue_message((void*)(uintptr_t)(HWND_BASE+i),WM_DESTROY,0,0);}
}
static int WIN32_API win32_GetMessageA(winmsg_t *msg,void *hwnd,uint32_t min,uint32_t max){if(!msg||max<min)return -1;for(;;){win32_pump_timers_and_destroy();if(message_queue_take(msg,hwnd,min,max,true))return msg->message==WM_QUIT?0:1;task_sleep(1U);}}
static int WIN32_API win32_PeekMessageA(winmsg_t *msg,void *hwnd,uint32_t min,uint32_t max,uint32_t remove){if(!msg||max<min)return 0;win32_pump_timers_and_destroy();return message_queue_take(msg,hwnd,min,max,(remove&1U)!=0U)?1:0;}
static int32_t WIN32_API win32_DispatchMessageA(const winmsg_t *msg){win_window_t*w;int32_t result=0;bool queued=false;if(!msg)return 0;w=window_from_handle(msg->hwnd);if(w&&w->proc){queued=win32_wndproc_is_pe(w->proc);result=win32_call_or_queue_wndproc(w->proc,msg->hwnd,msg->message,msg->wparam,(int32_t)msg->lparam,NULL,0U,-1);}if(w&&msg->message==WM_DESTROY&&!queued)cleanup_window(msg->hwnd);return result;}
static int WIN32_API win32_TranslateMessage(const winmsg_t *msg UNUSED){return 1;}
static int WIN32_API win32_WaitMessage(void){while(message_head==message_tail)task_sleep(1U);return 1;}
static uint32_t WIN32_API win32_GetMessageTime(void){return win_last_message_time;}
static uint32_t WIN32_API win32_GetMessagePos(void){return(uint16_t)win_cursor_x|((uint32_t)(uint16_t)win_cursor_y<<16);}
static int32_t WIN32_API win32_DefWindowProcA(void *hwnd,uint32_t msg,
                                                 uint32_t wp,
                                                 int32_t lp) {
    /* Returning TRUE for WM_NCCREATE is required for CreateWindowEx to
     * continue when the application's WNDPROC delegates this message. */
    if (msg == WM_NCCREATE) return 1;
    if (msg == WM_CLOSE) {
        win32_DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_SETICON) {
        win_window_t *window = window_from_handle(hwnd);
        void *old;
        if (!window || window->control) return 0;
        old = wp ? window->large_icon : window->small_icon;
        if (wp) window->large_icon = (void *)(uintptr_t)lp;
        else window->small_icon = (void *)(uintptr_t)lp;
        win32_update_native_icon(window);
        return (int32_t)(uintptr_t)old;
    }
    if (msg == WM_GETICON) {
        win_window_t *window = window_from_handle(hwnd);
        return window ? (int32_t)(uintptr_t)(wp ? window->large_icon :
                                                  window->small_icon) : 0;
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
static int WIN32_API win32_GetClientRect(
    void *hwnd, int32_t *rect) {
    win_window_t *window = window_from_handle(hwnd);
    gui_rect_t value;
    gui_desktop_t *desktop;

    if (!rect) return 0;

    if (hwnd == DESKTOP_HWND) {
        desktop = gui_get_desktop();
        rect[0] = rect[1] = 0;
        rect[2] = desktop ? desktop->surface.width : 800;
        rect[3] = desktop ? desktop->surface.height : 600;
        return 1;
    }

    if (!window) {
        kprintf("[USER32:CLIENT] FAIL hwnd=%x\n",
                (uint32_t)(uintptr_t)hwnd);
        return 0;
    }

    if (window->control)
        value = window->bounds;
    else
        value = gui_window_content_rect(window->native);

    rect[0] = 0;
    rect[1] = 0;
    rect[2] = value.w > 0 ? value.w : 1;
    rect[3] = value.h > 0 ? value.h : 1;

    if (window->control &&
        (window->kind == 5U || window->kind == 12U)) {
        kprintf("[USER32:CLIENT] hwnd=%x class=%s kind=%u "
                "rect=0,0,%d,%d bounds=%d,%d %dx%d\n",
                (uint32_t)(uintptr_t)hwnd,
                window->class_name, window->kind,
                rect[2], rect[3],
                window->bounds.x, window->bounds.y,
                window->bounds.w, window->bounds.h);
    }

    return 1;
}

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

    win_window_t *window = window_from_handle(hwnd);
    if (!window) return NULL;
    win32_gdi_begin(hwnd);
    window->paint_active = true;

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
    window->paint_active = false;
    window->native->dirty = true;
    gui_request_paint();
    return 1;
}
static int32_t control_list_message(win_window_t *w,uint32_t msg,
                                    uint32_t wp,int32_t lp) {
    bool combo=w->kind==7U;
    int index;
    if(msg==(combo?CB_ADDSTRING:LB_ADDSTRING))
        return control_item_insert(w,-1,(const char*)(uintptr_t)lp);
    if(msg==(combo?CB_INSERTSTRING:LB_INSERTSTRING))
        return control_item_insert(w,(int)wp,(const char*)(uintptr_t)lp);
    if(msg==(combo?CB_DELETESTRING:LB_DELETESTRING))
        return control_item_delete(w,(int)wp);
    if(msg==(combo?CB_RESETCONTENT:LB_RESETCONTENT)){
        control_items_reset(w);if(w->native){w->native->dirty=true;gui_request_paint();}return 0;
    }
    if(msg==(combo?CB_GETCOUNT:LB_GETCOUNT))return(int32_t)w->control_item_count;
    if(msg==(combo?CB_GETCURSEL:LB_GETCURSEL))return w->control_cur_sel;
    if(msg==(combo?CB_SETCURSEL:LB_SETCURSEL)){
        index=(int)wp;if(index<-1||index>=(int)w->control_item_count)return CB_ERR;
        w->control_cur_sel=(int16_t)index;if(w->native){w->native->dirty=true;gui_request_paint();}return index;
    }
    if(msg==(combo?CB_GETLBTEXTLEN:LB_GETTEXTLEN)){
        index=(int)wp;return index>=0&&index<(int)w->control_item_count?
            (int32_t)kstrlen(w->control_items[index]):CB_ERR;
    }
    if(msg==(combo?CB_GETLBTEXT:LB_GETTEXT)){
        index=(int)wp;if(index<0||index>=(int)w->control_item_count||!lp)return CB_ERR;
        kstrcpy((char*)(uintptr_t)lp,w->control_items[index]);
        return(int32_t)kstrlen(w->control_items[index]);
    }
    if(msg==(combo?CB_FINDSTRING:LB_FINDSTRING))
        return control_item_find(w,(int)wp,(const char*)(uintptr_t)lp);
    if(msg==(combo?CB_SETITEMDATA:LB_SETITEMDATA)){
        index=(int)wp;if(index<0||index>=(int)w->control_item_count)return CB_ERR;
        w->control_item_data[index]=lp;return 0;
    }
    if(msg==(combo?CB_GETITEMDATA:LB_GETITEMDATA)){
        index=(int)wp;return index>=0&&index<(int)w->control_item_count?
            w->control_item_data[index]:CB_ERR;
    }
    if(!combo&&msg==LB_GETTOPINDEX)return w->control_top_index;
    if(!combo&&msg==LB_SETTOPINDEX){
        index=(int)wp;if(index<0||index>=(int)w->control_item_count)return CB_ERR;
        w->control_top_index=(int16_t)index;return 0;
    }
    return 0;
}
typedef struct {uint32_t mask;int32_t item,subitem;uint32_t state,state_mask;char*text;int32_t text_max,image;int32_t param;} lvitem_a_t;
typedef struct {uint32_t mask;void*item;uint32_t state,state_mask;char*text;int32_t text_max,image,selected_image,children;int32_t param;} tvitem_a_t;
typedef struct {void*parent;void*insert_after;tvitem_a_t item;} tvinsert_a_t;
typedef struct {uint32_t mask,state,state_mask;char*text;int32_t text_max,image;int32_t param;} tcitem_a_t;
typedef struct {void*hwnd_from;uint32_t id_from;int32_t code;} nmhdr_t;
static void control_notify(win_window_t*w,void*hwnd,int32_t code){win_window_t*p=window_from_handle(w?w->parent:NULL);nmhdr_t hdr={hwnd,w?w->id:0U,code};if(p&&p->proc)(void)win32_call_or_queue_wndproc(p->proc,w->parent,WM_NOTIFY,w->id,(int32_t)(uintptr_t)&hdr,&hdr,(uint8_t)sizeof(hdr),3);}
static int32_t common_control_message(win_window_t*w,void*hwnd,uint32_t msg,uint32_t wp,int32_t lp){
    int index;
    if(w->kind==9U){
        if(msg==LVM_GETIMAGELIST){
            uint32_t slot=wp;
            void *handle;

            if(slot>=WIN32_LISTVIEW_IMAGE_LISTS){
                kprintf("[USER32:LISTVIEW] GETIMAGELIST FAIL hwnd=%x "
                        "slot=%u\n",
                        (uint32_t)(uintptr_t)hwnd,slot);
                return 0;
            }

            handle=w->listview_image_lists[slot];
            kprintf("[USER32:LISTVIEW] GETIMAGELIST hwnd=%x "
                    "slot=%u handle=%x\n",
                    (uint32_t)(uintptr_t)hwnd,slot,
                    (uint32_t)(uintptr_t)handle);
            return(int32_t)(uintptr_t)handle;
        }
        if(msg==LVM_SETIMAGELIST){
            uint32_t slot=wp;
            void *old_handle;
            void *new_handle=(void*)(uintptr_t)lp;

            if(slot>=WIN32_LISTVIEW_IMAGE_LISTS){
                kprintf("[USER32:LISTVIEW] SETIMAGELIST FAIL hwnd=%x "
                        "slot=%u new=%x\n",
                        (uint32_t)(uintptr_t)hwnd,slot,
                        (uint32_t)(uintptr_t)new_handle);
                return 0;
            }

            old_handle=w->listview_image_lists[slot];
            w->listview_image_lists[slot]=new_handle;

            kprintf("[USER32:LISTVIEW] SETIMAGELIST hwnd=%x "
                    "slot=%u old=%x new=%x\n",
                    (uint32_t)(uintptr_t)hwnd,slot,
                    (uint32_t)(uintptr_t)old_handle,
                    (uint32_t)(uintptr_t)new_handle);

            if(w->native){
                w->native->dirty=true;
                gui_request_paint();
            }

            return(int32_t)(uintptr_t)old_handle;
        }
        if(msg==LVM_GETITEMCOUNT)return w->control_item_count;
        if(msg==LVM_INSERTITEMA){lvitem_a_t*i=(lvitem_a_t*)(uintptr_t)lp;if(!i)return -1;index=control_item_insert(w,i->item,(i->mask&LVIF_TEXT)&&i->text?i->text:"");if(index>=0&&(i->mask&LVIF_PARAM))w->control_item_data[index]=i->param;return index;}
        if(msg==LVM_DELETEITEM)return control_item_delete(w,(int)wp)>=0;
        if(msg==LVM_DELETEALLITEMS){control_items_reset(w);return 1;}
        if(msg==LVM_GETITEMA||msg==LVM_SETITEMA){lvitem_a_t*i=(lvitem_a_t*)(uintptr_t)lp;if(!i||i->item<0||i->item>=w->control_item_count)return 0;if(msg==LVM_GETITEMA){if((i->mask&LVIF_TEXT)&&i->text&&i->text_max>0)kstrncpy(i->text,w->control_items[i->item],(uint32_t)i->text_max-1U);if(i->mask&LVIF_PARAM)i->param=w->control_item_data[i->item];i->state=i->item==w->control_cur_sel?LVIS_SELECTED:0U;}else{if((i->mask&LVIF_TEXT)&&i->text){char*copy=(char*)kmalloc(kstrlen(i->text)+1U);if(!copy)return 0;kstrcpy(copy,i->text);kfree(w->control_items[i->item]);w->control_items[i->item]=copy;}if(i->mask&LVIF_PARAM)w->control_item_data[i->item]=i->param;if(i->state_mask&LVIS_SELECTED){w->control_cur_sel=(i->state&LVIS_SELECTED)?(int16_t)i->item:-1;control_notify(w,hwnd,-101);}}return 1;}
        if(msg==LVM_GETITEMTEXTA||msg==LVM_SETITEMTEXTA){lvitem_a_t*i=(lvitem_a_t*)(uintptr_t)lp;index=(int)wp;if(!i||index<0||index>=w->control_item_count)return 0;if(msg==LVM_GETITEMTEXTA){if(i->text&&i->text_max>0)kstrncpy(i->text,w->control_items[index],(uint32_t)i->text_max-1U);return kstrlen(w->control_items[index]);}i->item=index;i->mask=LVIF_TEXT;return common_control_message(w,hwnd,LVM_SETITEMA,0,(int32_t)(uintptr_t)i);}
        if(msg==LVM_GETNEXTITEM){index=(int)wp+1;if((lp&LVNI_SELECTED)&&w->control_cur_sel>=index)return w->control_cur_sel;return -1;}
        if(msg==LVM_GETSTRINGWIDTHA){
            const char *text=(const char*)(uintptr_t)lp;
            return text?(int32_t)(kstrlen(text)*8U):0;
        }
        if(msg==LVM_ENSUREVISIBLE)return 1;
        if(msg==LVM_GETCOLUMNA){
            /*
             * WinZip queries column zero while constructing an empty view.
             * Its MFC wrapper treats a failed query as an uninitialised
             * control.  Preserve the caller's requested fields and report a
             * conventional client-width column until full header storage is
             * needed.
             */
            uint32_t *column=(uint32_t*)(uintptr_t)lp;
            if(!column || wp!=0U)return 0;
            if(column[0]&0x0002U)column[2]=(uint32_t)w->bounds.w;
            return 1;
        }
        if(msg==LVM_UPDATE){
            if(w->native){w->native->dirty=true;gui_request_paint();}
            return wp < w->control_item_count;
        }
        if(msg==LVM_GETITEMSTATE){
            uint32_t state=(int32_t)wp==w->control_cur_sel?LVIS_SELECTED:0U;
            return(int32_t)(state&(uint32_t)lp);
        }
        if(msg==LVM_SORTITEMS){
            /*
             * The Win32 contract reports whether the sort request was
             * accepted.  WinZip invokes it while its list is still empty;
             * returning FALSE is treated as a fatal ListView.c error.
             * Preserve insertion order until Ring-3 callback comparators are
             * supported, but accept the operation like comctl32 does.
             */
            kprintf("[USER32:LISTVIEW] SORTITEMS hwnd=%x items=%u "
                    "context=%x compare=%x\n",
                    (uint32_t)(uintptr_t)hwnd,w->control_item_count,
                    wp,(uint32_t)lp);
            return 1;
        }
        if(msg==LVM_GETSUBITEMRECT){
            int32_t *rect=(int32_t*)(uintptr_t)lp;
            if(!rect)return 0;
            rect[0]=0;
            rect[1]=(int32_t)wp*18;
            rect[2]=w->bounds.w;
            rect[3]=rect[1]+18;
            return 1;
        }
        if(msg==LVM_GETSELECTEDCOUNT)return w->control_cur_sel>=0?1:0;
        if(msg==LVM_INSERTCOLUMNA)return (int32_t)wp;
        if(msg==LVM_SETCOLUMNWIDTH)return 1;
        if(msg==LVM_SETEXTENDEDLISTVIEWSTYLE){int32_t old=w->user_data;w->user_data=lp;return old;}
        if(msg>=LVM_FIRST&&msg<LVM_FIRST+0x100U){
            kprintf("[USER32:LISTVIEW] UNKNOWN hwnd=%x msg=%x "
                    "wp=%x lp=%x items=%u selected=%d\n",
                    (uint32_t)(uintptr_t)hwnd,msg,wp,
                    (uint32_t)lp,w->control_item_count,
                    (int)w->control_cur_sel);
        }
    }
    if(w->kind==10U){
        if(msg==TVM_GETCOUNT)return w->control_item_count;
        if(msg==TVM_INSERTITEMA){tvinsert_a_t*i=(tvinsert_a_t*)(uintptr_t)lp;if(!i)return 0;index=control_item_insert(w,-1,(i->item.mask&TVIF_TEXT)&&i->item.text?i->item.text:"");if(index<0)return 0;if(i->item.mask&TVIF_PARAM)w->control_item_data[index]=i->item.param;return index+1;}
        if(msg==TVM_DELETEITEM){index=(int)(uintptr_t)lp-1;if(lp==(int32_t)(uintptr_t)0xFFFF0000U){control_items_reset(w);return 1;}return control_item_delete(w,index)>=0;}
        if(msg==TVM_GETITEMA||msg==TVM_SETITEMA){tvitem_a_t*i=(tvitem_a_t*)(uintptr_t)lp;if(!i)return 0;index=(int)(uintptr_t)i->item-1;if(index<0||index>=w->control_item_count)return 0;if(msg==TVM_GETITEMA){if((i->mask&TVIF_TEXT)&&i->text&&i->text_max>0)kstrncpy(i->text,w->control_items[index],(uint32_t)i->text_max-1U);if(i->mask&TVIF_PARAM)i->param=w->control_item_data[index];}else{if((i->mask&TVIF_TEXT)&&i->text){char*copy=(char*)kmalloc(kstrlen(i->text)+1U);if(!copy)return 0;kstrcpy(copy,i->text);kfree(w->control_items[index]);w->control_items[index]=copy;}if(i->mask&TVIF_PARAM)w->control_item_data[index]=i->param;}return 1;}
        if(msg==TVM_SELECTITEM&&wp==TVGN_CARET){index=(int)(uintptr_t)lp-1;if(index<-1||index>=w->control_item_count)return 0;w->control_cur_sel=(int16_t)index;control_notify(w,hwnd,-402);return 1;}
        if(msg==TVM_GETNEXTITEM){if(wp==TVGN_CARET)return w->control_cur_sel>=0?w->control_cur_sel+1:0;if(wp==TVGN_ROOT||wp==TVGN_CHILD)return w->control_item_count?1:0;index=(int)(uintptr_t)lp-1;if(wp==TVGN_NEXT&&index+1<w->control_item_count)return index+2;if(wp==TVGN_PREVIOUS&&index>0)return index;return 0;}
        if(msg==TVM_EXPAND)return 1;
    }
    if(w->kind==11U){
        if(msg==TCM_GETITEMCOUNT)return w->control_item_count;
        if(msg==TCM_INSERTITEMA){tcitem_a_t*i=(tcitem_a_t*)(uintptr_t)lp;if(!i)return -1;index=control_item_insert(w,(int)wp,(i->mask&TCIF_TEXT)&&i->text?i->text:"");if(index>=0)w->control_item_data[index]=i->param;return index;}
        if(msg==TCM_DELETEITEM)return control_item_delete(w,(int)wp)>=0;
        if(msg==TCM_DELETEALLITEMS){control_items_reset(w);return 1;}
        if(msg==TCM_GETCURSEL)return w->control_cur_sel;
        if(msg==TCM_SETCURSEL){int old=w->control_cur_sel;index=(int)wp;if(index<0||index>=w->control_item_count)return -1;w->control_cur_sel=(int16_t)index;control_notify(w,hwnd,-551);return old;}
        if(msg==TCM_GETITEMA||msg==TCM_SETITEMA){tcitem_a_t*i=(tcitem_a_t*)(uintptr_t)lp;index=(int)wp;if(!i||index<0||index>=w->control_item_count)return 0;if(msg==TCM_GETITEMA){if((i->mask&TCIF_TEXT)&&i->text&&i->text_max>0)kstrncpy(i->text,w->control_items[index],(uint32_t)i->text_max-1U);i->param=w->control_item_data[index];}else{if((i->mask&TCIF_TEXT)&&i->text){char*copy=(char*)kmalloc(kstrlen(i->text)+1U);if(!copy)return 0;kstrcpy(copy,i->text);kfree(w->control_items[index]);w->control_items[index]=copy;}w->control_item_data[index]=i->param;}return 1;}
        if(msg==TCM_ADJUSTRECT&&lp){int32_t*r=(int32_t*)(uintptr_t)lp;if(wp){r[0]-=2;r[1]-=22;r[2]+=2;r[3]+=2;}else{r[0]+=2;r[1]+=22;r[2]-=2;r[3]-=2;}return 0;}
    }
    return 0;
}
/* BLES_WINE_STATUSBAR_SUBCLASS_20260723
 *
 * Kernel-side equivalent of Wine's StatusWindowProc. The important semantic
 * detail is not only the SB_* implementation: the control must have a real
 * WndProc so SetWindowLong(GWL_WNDPROC) can return it during subclassing.
 */
static uint32_t win32_status_part_index(uint32_t wparam) {
    uint32_t part = wparam & 0xFFU;
    return part == 0xFFU ? 0U : part;
}

static uint32_t win32_status_result(uint32_t length, uint16_t style) {
    return (length & 0xFFFFU) | ((uint32_t)style << 16);
}

static void win32_status_mark_dirty(win_window_t *window) {
    if (!window || !window->native) return;
    window->native->dirty = true;
    gui_request_paint();
}

static void win32_status_resize(win_window_t *window) {
    win_window_t *parent;
    gui_rect_t client;
    bool at_top;

    if (!window) return;
    parent = window_from_handle(window->parent);
    if (!parent || !parent->native) return;

    client = gui_window_content_rect(parent->native);
    at_top = (window->style & 0x00000003U) == CCS_TOP;

    window->bounds.x = 0;
    window->bounds.w = client.w;
    window->bounds.y = at_top ? 0 : client.h - window->bounds.h;
    if (window->bounds.y < 0) window->bounds.y = 0;

    kprintf("[USER32:STATUS] SIZE hwnd=%x parent=%x bounds=%d,%d %dx%d\n",
            (uint32_t)(uintptr_t)(
                HWND_BASE + (uint32_t)(window - win_windows)),
            (uint32_t)(uintptr_t)window->parent,
            window->bounds.x, window->bounds.y,
            window->bounds.w, window->bounds.h);
    win32_status_mark_dirty(window);
}

static int32_t WIN32_API win32_StatusBarWndProc(
    void *hwnd, uint32_t message, uint32_t wparam, int32_t lparam) {
    win_window_t *window = window_from_handle(hwnd);
    uint32_t part;
    uint16_t style;
    uint32_t length;

    if (!window || !window->control || window->kind != 5U)
        return win32_DefWindowProcA(hwnd, message, wparam, lparam);

    if (message == WM_CREATE || message == WM_DESTROY ||
        message == WM_SIZE || message == WM_SETTEXT ||
        message == WM_GETTEXT || message == WM_GETTEXTLENGTH ||
        message == SB_SETPARTS || message == SB_GETPARTS ||
        message == SB_SETTEXTA || message == SB_SETTEXTW ||
        message == SB_GETTEXTA || message == SB_GETTEXTW ||
        message == SB_GETTEXTLENGTHA || message == SB_GETTEXTLENGTHW ||
        message == SB_GETRECT || message == SB_SIMPLE ||
        message == SB_SETMINHEIGHT || message == CCM_SETBKCOLOR) {
        kprintf("[USER32:STATUS] DEFAULT hwnd=%x msg=%x wp=%x lp=%x "
                "parts=%u simple=%u\n",
                (uint32_t)(uintptr_t)hwnd, message, wparam,
                (uint32_t)lparam, window->status_part_count,
                window->status_simple ? 1U : 0U);
    }

    switch (message) {
    case WM_CREATE:
    case WM_DESTROY:
        return 0;

    case WM_SIZE:
        win32_status_resize(window);
        return 0;

    case WM_SETFONT:
        window->font = (void *)(uintptr_t)wparam;
        if (lparam) win32_status_mark_dirty(window);
        return 0;

    case WM_GETFONT:
        return (int32_t)(uintptr_t)window->font;

    case WM_SETTEXT:
        wparam = 0U;
        message = SB_SETTEXTA;
        break;

    case WM_GETTEXTLENGTH:
        return (int32_t)kstrlen(window->status_text[0]);

    case WM_GETTEXT:
        if (!lparam || !wparam) return 0;
        length = (uint32_t)kstrlen(window->status_text[0]);
        if (length >= wparam) length = wparam - 1U;
        kmemcpy((void *)(uintptr_t)lparam, window->status_text[0], length);
        ((char *)(uintptr_t)lparam)[length] = '\0';
        return (int32_t)length;

    default:
        break;
    }

    if (message == CCM_SETBKCOLOR) {
        uint32_t old = window->status_background;
        window->status_background = (uint32_t)lparam;
        win32_status_mark_dirty(window);
        return (int32_t)old;
    }

    if (message == SB_SETPARTS) {
        uint32_t count = wparam;
        const int32_t *parts = (const int32_t *)(uintptr_t)lparam;

        if (!count || !parts || count > 8U) {
            kprintf("[USER32:STATUS] SB_SETPARTS FAIL hwnd=%x "
                    "count=%u parts=%x current=%u\n",
                    (uint32_t)(uintptr_t)hwnd, count,
                    (uint32_t)(uintptr_t)parts,
                    window->status_part_count);
            return 0;
        }

        window->status_part_count = (uint8_t)count;
        for (uint32_t index = 0U; index < count; index++)
            window->status_parts[index] = parts[index];

        kprintf("[USER32:STATUS] SB_SETPARTS hwnd=%x count=%u",
                (uint32_t)(uintptr_t)hwnd, count);
        for (uint32_t index = 0U; index < count; index++)
            kprintf(" p%u=%d", index, window->status_parts[index]);
        kprintf("\n");

        win32_status_mark_dirty(window);
        return 1;
    }

    if (message == SB_GETBORDERS) {
        int32_t *borders = (int32_t *)(uintptr_t)lparam;
        if (!borders) return 0;
        borders[0] = 0;
        borders[1] = 2;
        borders[2] = 2;
        return 1;
    }

    if (message == SB_GETPARTS) {
        int32_t *parts = (int32_t *)(uintptr_t)lparam;
        uint32_t count = wparam < window->status_part_count
            ? wparam : window->status_part_count;

        if (parts) {
            for (uint32_t index = 0U; index < count; index++)
                parts[index] = window->status_parts[index];
        }

        kprintf("[USER32:STATUS] SB_GETPARTS hwnd=%x requested=%u "
                "buffer=%x copied=%u result=%u\n",
                (uint32_t)(uintptr_t)hwnd, wparam,
                (uint32_t)(uintptr_t)parts,
                count, window->status_part_count);
        return window->status_part_count;
    }

    if (message == SB_SETTEXTA || message == SB_SETTEXTW) {
        part = win32_status_part_index(wparam);
        style = (uint16_t)(wparam & 0xFF00U);

        if (part >= 8U) return 0;
        window->status_styles[part] = style;

        if (style & SBT_OWNERDRAW) {
            window->status_item_data[part] = (uint32_t)lparam;
            window->status_text[part][0] = '\0';
        } else if (message == SB_SETTEXTW) {
            const uint16_t *source = (const uint16_t *)(uintptr_t)lparam;
            uint32_t index = 0U;

            if (source) {
                while (source[index] &&
                       index + 1U < sizeof(window->status_text[part])) {
                    window->status_text[part][index] =
                        source[index] <= 0xFFU ? (char)source[index] : '?';
                    index++;
                }
            }
            window->status_text[part][index] = '\0';
        } else {
            const char *source = (const char *)(uintptr_t)lparam;
            kstrncpy(window->status_text[part], source ? source : "",
                     sizeof(window->status_text[part]) - 1U);
            window->status_text[part][sizeof(window->status_text[part]) - 1U] =
                '\0';
        }

        if (part >= window->status_part_count)
            window->status_part_count = (uint8_t)(part + 1U);

        kprintf("[USER32:STATUS] SB_SETTEXT hwnd=%x part=%u style=%x "
                "text=%s item=%x\n",
                (uint32_t)(uintptr_t)hwnd, part, style,
                window->status_text[part],
                window->status_item_data[part]);

        win32_status_mark_dirty(window);
        return 1;
    }

    if (message == SB_GETTEXTLENGTHA ||
        message == SB_GETTEXTLENGTHW) {
        part = win32_status_part_index(wparam);
        if (part >= 8U) return 0;
        if (window->status_styles[part] & SBT_OWNERDRAW)
            return (int32_t)win32_status_result(
                0U, window->status_styles[part]);
        return (int32_t)win32_status_result(
            (uint32_t)kstrlen(window->status_text[part]),
            window->status_styles[part]);
    }

    if (message == SB_GETTEXTA || message == SB_GETTEXTW) {
        part = win32_status_part_index(wparam);
        if (part >= 8U) return 0;

        if (window->status_styles[part] & SBT_OWNERDRAW)
            return (int32_t)window->status_item_data[part];

        length = (uint32_t)kstrlen(window->status_text[part]);
        if (lparam) {
            if (message == SB_GETTEXTW) {
                uint16_t *destination = (uint16_t *)(uintptr_t)lparam;
                for (uint32_t index = 0U; index <= length; index++)
                    destination[index] =
                        (uint8_t)window->status_text[part][index];
            } else {
                kstrcpy((char *)(uintptr_t)lparam,
                        window->status_text[part]);
            }
        }
        return (int32_t)win32_status_result(
            length, window->status_styles[part]);
    }

    if (message == SB_SETMINHEIGHT) {
        int minimum = (int)wparam;
        int old_height = window->bounds.h;

        if (minimum < 4) minimum = 4;
        if (window->bounds.h < minimum)
            window->bounds.h = minimum;

        kprintf("[USER32:STATUS] SB_SETMINHEIGHT hwnd=%x requested=%u "
                "old=%d new=%d\n",
                (uint32_t)(uintptr_t)hwnd, wparam,
                old_height, window->bounds.h);

        win32_status_resize(window);
        return 1;
    }

    if (message == SB_SIMPLE) {
        window->status_simple = wparam != 0U;
        win32_status_mark_dirty(window);
        return 1;
    }

    if (message == SB_ISSIMPLE)
        return window->status_simple ? 1 : 0;

    if (message == SB_GETRECT) {
        int32_t *out = (int32_t *)(uintptr_t)lparam;
        int left = 0;
        int right;

        part = win32_status_part_index(wparam);
        if (!out || part >= window->status_part_count) return 0;

        if (window->status_simple) {
            if (part != 0U) return 0;
            right = window->bounds.w;
        } else {
            if (part > 0U) {
                left = window->status_parts[part - 1U];
                if (left < 0 || left > window->bounds.w)
                    left = window->bounds.w;
                else
                    left += 2;
            }
            right = window->status_parts[part];
            if (right < 0 || right > window->bounds.w)
                right = window->bounds.w;
        }

        out[0] = left;
        out[1] = 0;
        out[2] = right;
        out[3] = window->bounds.h;

        kprintf("[USER32:STATUS] SB_GETRECT hwnd=%x part=%u "
                "rect=%d,%d,%d,%d ok=%u\n",
                (uint32_t)(uintptr_t)hwnd, part,
                out[0], out[1], out[2], out[3],
                right > left ? 1U : 0U);
        return right > left;
    }

    if (message == SB_SETICON) {
        part = win32_status_part_index(wparam);
        if (part >= 8U) return 0;
        window->status_icons[part] = (void *)(uintptr_t)lparam;
        win32_status_mark_dirty(window);
        return 1;
    }

    if (message == SB_GETICON) {
        part = win32_status_part_index(wparam);
        return part < 8U
            ? (int32_t)(uintptr_t)window->status_icons[part] : 0;
    }

    if (message == SB_SETTIPTEXTA || message == SB_SETTIPTEXTW) {
        part = win32_status_part_index(wparam);
        if (part >= 8U) return 0;

        if (message == SB_SETTIPTEXTW) {
            const uint16_t *source = (const uint16_t *)(uintptr_t)lparam;
            uint32_t index = 0U;
            if (source) {
                while (source[index] &&
                       index + 1U < sizeof(window->status_tips[part])) {
                    window->status_tips[part][index] =
                        source[index] <= 0xFFU ? (char)source[index] : '?';
                    index++;
                }
            }
            window->status_tips[part][index] = '\0';
        } else {
            const char *source = (const char *)(uintptr_t)lparam;
            kstrncpy(window->status_tips[part], source ? source : "",
                     sizeof(window->status_tips[part]) - 1U);
            window->status_tips[part]
                [sizeof(window->status_tips[part]) - 1U] = '\0';
        }
        return 1;
    }

    if (message == SB_GETTIPTEXTA || message == SB_GETTIPTEXTW) {
        uint32_t size = wparam >> 16;
        part = win32_status_part_index(wparam);
        if (part >= 8U || !lparam || !size) return 0;

        if (message == SB_GETTIPTEXTW) {
            uint16_t *destination = (uint16_t *)(uintptr_t)lparam;
            uint32_t index = 0U;
            while (window->status_tips[part][index] &&
                   index + 1U < size) {
                destination[index] =
                    (uint8_t)window->status_tips[part][index];
                index++;
            }
            destination[index] = 0U;
        } else {
            kstrncpy((char *)(uintptr_t)lparam,
                     window->status_tips[part], size - 1U);
            ((char *)(uintptr_t)lparam)[size - 1U] = '\0';
        }
        return 0;
    }

    if (message >= 0x0400U && message < 0x8000U) {
        kprintf("[USER32:STATUS] UNKNOWN hwnd=%x msg=%x wp=%x lp=%x\n",
                (uint32_t)(uintptr_t)hwnd, message, wparam,
                (uint32_t)lparam);
    }

    return win32_DefWindowProcA(hwnd, message, wparam, lparam);
}

static uint32_t win32_rebar_band_height(
    const win_window_t *rebar, uint32_t index) {
    int height;

    if (!rebar || index >= rebar->rebar_band_count) return 0U;

    height = rebar->rebar_cy_min[index];
    if (height <= 0) {
        win_window_t *child =
            window_from_handle(rebar->rebar_children[index]);
        height = child && child->bounds.h > 0 ? child->bounds.h : 23;
    }

    if (height < 4) height = 4;
    return (uint32_t)height;
}

static void win32_rebar_layout(void *hwnd, win_window_t *rebar) {
    int cursor_x = 0;
    int maximum_height = 0;

    if (!rebar) return;

    for (uint32_t index = 0U;
         index < rebar->rebar_band_count &&
         index < WIN32_REBAR_MAX_BANDS;
         index++) {
        int width = rebar->rebar_cx[index];
        int height = (int)win32_rebar_band_height(rebar, index);
        bool hidden = (rebar->rebar_styles[index] & RBBS_HIDDEN) != 0U;
        win_window_t *child =
            window_from_handle(rebar->rebar_children[index]);

        if (width <= 0) width = rebar->bounds.w - cursor_x;
        if (width < rebar->rebar_cx_min[index])
            width = rebar->rebar_cx_min[index];
        if (cursor_x + width > rebar->bounds.w)
            width = rebar->bounds.w - cursor_x;
        if (width < 0) width = 0;

        rebar->rebar_rects[index] =
            (gui_rect_t){cursor_x, 0, width, height};

        if (child) {
            if (child->parent == hwnd)
                child->parent = rebar->parent;

            child->bounds.x = rebar->bounds.x + cursor_x + 2;
            child->bounds.y = rebar->bounds.y + 1;
            child->bounds.w = width > 4 ? width - 4 : width;
            child->bounds.h = height > 2 ? height - 2 : height;
            child->visible = !hidden;

            kprintf("[USER32:REBAR] LAYOUT band=%u child=%x "
                    "rect=%d,%d %dx%d hidden=%u\n",
                    index,
                    (uint32_t)(uintptr_t)rebar->rebar_children[index],
                    child->bounds.x, child->bounds.y,
                    child->bounds.w, child->bounds.h,
                    hidden ? 1U : 0U);
        }

        cursor_x += width;
        if (!hidden && height > maximum_height)
            maximum_height = height;
    }

    if (maximum_height > 0)
        rebar->bounds.h = maximum_height;

    if (rebar->native) {
        rebar->native->dirty = true;
        gui_request_paint();
    }
}

static bool win32_rebar_copy_band(
    win_window_t *rebar, uint32_t index,
    const win32_rebar_band_info_t *info, bool unicode) {
    if (!rebar || !info || index >= WIN32_REBAR_MAX_BANDS ||
        info->cb_size < 32U)
        return false;

    rebar->rebar_masks[index] = info->mask;

    if (info->mask & RBBIM_STYLE)
        rebar->rebar_styles[index] = info->style;
    if (info->mask & RBBIM_CHILD)
        rebar->rebar_children[index] = info->child;
    if (info->mask & RBBIM_CHILDSIZE) {
        rebar->rebar_cx_min[index] = (int32_t)info->cx_min_child;
        rebar->rebar_cy_min[index] = (int32_t)info->cy_min_child;
    }
    if (info->mask & RBBIM_SIZE)
        rebar->rebar_cx[index] = (int32_t)info->cx;
    if (info->mask & RBBIM_ID)
        rebar->rebar_ids[index] = info->id;

    if ((info->mask & RBBIM_TEXT) && info->text) {
        if (unicode) {
            const uint16_t *source = (const uint16_t *)info->text;
            uint32_t out = 0U;
            while (source[out] &&
                   out + 1U < sizeof(rebar->rebar_text[index])) {
                rebar->rebar_text[index][out] =
                    source[out] <= 0xFFU ? (char)source[out] : '?';
                out++;
            }
            rebar->rebar_text[index][out] = '\0';
        } else {
            kstrncpy(rebar->rebar_text[index],
                     (const char *)info->text,
                     sizeof(rebar->rebar_text[index]) - 1U);
            rebar->rebar_text[index]
                [sizeof(rebar->rebar_text[index]) - 1U] = '\0';
        }
    }

    return true;
}

/*
 * RB_GETBANDINFO is not merely a capability probe.  WzTBar queries the
 * band it just inserted and uses its returned child and dimensions to finish
 * toolbar setup.  Returning TRUE without writing the requested fields made
 * WinZip consume uninitialised stack data and abort in WzTBar.c.
 */
static bool win32_rebar_get_band(
    const win_window_t *rebar, uint32_t index,
    win32_rebar_band_info_t *info, bool unicode) {
    uint32_t capacity;
    uint32_t copied = 0U;
    uint32_t bytes;

    if (!rebar || !info || index >= rebar->rebar_band_count ||
        info->cb_size < 32U)
        return false;
    bytes = info->cb_size;

    if (info->mask & RBBIM_STYLE) info->style = rebar->rebar_styles[index];
    if (info->mask & RBBIM_COLORS) {
        info->clr_fore = rebar->rebar_text_color;
        info->clr_back = rebar->rebar_background;
    }
    if ((info->mask & RBBIM_IMAGE) && bytes >= 32U) info->image = -1;
    if ((info->mask & RBBIM_CHILD) && bytes >= 36U)
        info->child = rebar->rebar_children[index];
    if ((info->mask & RBBIM_CHILDSIZE) && bytes >= 44U) {
        info->cx_min_child = (uint32_t)rebar->rebar_cx_min[index];
        info->cy_min_child = (uint32_t)rebar->rebar_cy_min[index];
    }
    if ((info->mask & RBBIM_SIZE) && bytes >= 48U)
        info->cx = (uint32_t)rebar->rebar_cx[index];
    if ((info->mask & RBBIM_BACKGROUND) && bytes >= 52U)
        info->background = NULL;
    if ((info->mask & RBBIM_ID) && bytes >= 56U)
        info->id = rebar->rebar_ids[index];
    if ((info->mask & RBBIM_IDEALSIZE) && bytes >= 72U) info->cx_ideal =
        (uint32_t)(rebar->rebar_cx[index] > 0 ? rebar->rebar_cx[index] : 0);
    if ((info->mask & RBBIM_LPARAM) && bytes >= 76U) info->lparam = 0;
    if ((info->mask & RBBIM_HEADERSIZE) && bytes >= 80U) info->cx_header = 0U;

    if (!(info->mask & RBBIM_TEXT) || bytes < 28U ||
        !info->text || !info->text_length)
        return true;

    capacity = info->text_length;
    if (unicode) {
        uint16_t *destination = (uint16_t *)info->text;
        while (rebar->rebar_text[index][copied] && copied + 1U < capacity) {
            destination[copied] = (uint8_t)rebar->rebar_text[index][copied];
            copied++;
        }
        destination[copied] = 0U;
    } else {
        char *destination = (char *)info->text;
        while (rebar->rebar_text[index][copied] && copied + 1U < capacity) {
            destination[copied] = rebar->rebar_text[index][copied];
            copied++;
        }
        destination[copied] = '\0';
    }
    info->text_length = copied;
    return true;
}

static int32_t WIN32_API win32_ReBarWndProc(
    void *hwnd, uint32_t message, uint32_t wparam, int32_t lparam) {
    win_window_t *rebar = window_from_handle(hwnd);
    uint32_t index;

    if (!rebar || !rebar->control || rebar->kind != 12U)
        return win32_DefWindowProcA(hwnd, message, wparam, lparam);

    if (message == WM_CREATE || message == WM_DESTROY)
        return 0;

    if (message == WM_SIZE) {
        int width = (int)((uint32_t)lparam & 0xFFFFU);
        int height = (int)(((uint32_t)lparam >> 16) & 0xFFFFU);
        if (width > 0) rebar->bounds.w = width;
        if (height > 0) rebar->bounds.h = height;
        win32_rebar_layout(hwnd, rebar);
        return 0;
    }

    if (message == WM_SETFONT) {
        rebar->font = (void *)(uintptr_t)wparam;
        return 0;
    }
    if (message == WM_GETFONT)
        return (int32_t)(uintptr_t)rebar->font;

    if (message == RB_INSERTBANDA || message == RB_INSERTBANDW) {
        const win32_rebar_band_info_t *info =
            (const win32_rebar_band_info_t *)(uintptr_t)lparam;

        if (!info ||
            rebar->rebar_band_count >= WIN32_REBAR_MAX_BANDS) {
            kprintf("[USER32:REBAR] INSERT FAIL hwnd=%x index=%u "
                    "info=%x count=%u\n",
                    (uint32_t)(uintptr_t)hwnd, wparam,
                    (uint32_t)lparam, rebar->rebar_band_count);
            return 0;
        }

        index = wparam;
        if (index == 0xFFFFFFFFU ||
            index > rebar->rebar_band_count)
            index = rebar->rebar_band_count;

        for (uint32_t move = rebar->rebar_band_count;
             move > index; move--) {
            rebar->rebar_masks[move] = rebar->rebar_masks[move - 1U];
            rebar->rebar_styles[move] = rebar->rebar_styles[move - 1U];
            rebar->rebar_ids[move] = rebar->rebar_ids[move - 1U];
            rebar->rebar_cx_min[move] = rebar->rebar_cx_min[move - 1U];
            rebar->rebar_cy_min[move] = rebar->rebar_cy_min[move - 1U];
            rebar->rebar_cx[move] = rebar->rebar_cx[move - 1U];
            rebar->rebar_children[move] =
                rebar->rebar_children[move - 1U];
            rebar->rebar_rects[move] =
                rebar->rebar_rects[move - 1U];
            kstrcpy(rebar->rebar_text[move],
                    rebar->rebar_text[move - 1U]);
        }

        rebar->rebar_masks[index] = 0U;
        rebar->rebar_styles[index] = 0U;
        rebar->rebar_ids[index] = 0U;
        rebar->rebar_cx_min[index] = 0;
        rebar->rebar_cy_min[index] = 0;
        rebar->rebar_cx[index] = 0;
        rebar->rebar_children[index] = NULL;
        rebar->rebar_text[index][0] = '\0';

        if (!win32_rebar_copy_band(
                rebar, index, info,
                message == RB_INSERTBANDW)) {
            kprintf("[USER32:REBAR] INSERT BADINFO hwnd=%x "
                    "cb=%u mask=%x\n",
                    (uint32_t)(uintptr_t)hwnd,
                    info->cb_size, info->mask);
            return 0;
        }

        rebar->rebar_band_count++;

        kprintf("[USER32:REBAR] INSERT hwnd=%x index=%u cb=%u "
                "mask=%x style=%x child=%x min=%dx%d cx=%d id=%u "
                "text=%s count=%u\n",
                (uint32_t)(uintptr_t)hwnd, index,
                info->cb_size, info->mask,
                rebar->rebar_styles[index],
                (uint32_t)(uintptr_t)rebar->rebar_children[index],
                rebar->rebar_cx_min[index],
                rebar->rebar_cy_min[index],
                rebar->rebar_cx[index],
                rebar->rebar_ids[index],
                rebar->rebar_text[index],
                rebar->rebar_band_count);

        win32_rebar_layout(hwnd, rebar);
        return 1;
    }

    if (message == RB_SETBANDINFOA ||
        message == RB_SETBANDINFOW) {
        const win32_rebar_band_info_t *info =
            (const win32_rebar_band_info_t *)(uintptr_t)lparam;
        index = wparam;

        if (index >= rebar->rebar_band_count ||
            !win32_rebar_copy_band(
                rebar, index, info,
                message == RB_SETBANDINFOW))
            return 0;

        kprintf("[USER32:REBAR] SETBAND hwnd=%x index=%u mask=%x "
                "child=%x cx=%d min=%dx%d\n",
                (uint32_t)(uintptr_t)hwnd, index,
                info ? info->mask : 0U,
                (uint32_t)(uintptr_t)rebar->rebar_children[index],
                rebar->rebar_cx[index],
                rebar->rebar_cx_min[index],
                rebar->rebar_cy_min[index]);

        win32_rebar_layout(hwnd, rebar);
        return 1;
    }

    if (message == RB_GETBANDINFOA || message == RB_GETBANDINFOW) {
        win32_rebar_band_info_t *info =
            (win32_rebar_band_info_t *)(uintptr_t)lparam;
        index = wparam;

        if (!win32_rebar_get_band(rebar, index, info,
                                  message == RB_GETBANDINFOW)) {
            kprintf("[USER32:REBAR] GETBAND FAIL hwnd=%x index=%u "
                    "info=%x count=%u\n",
                    (uint32_t)(uintptr_t)hwnd, index,
                    (uint32_t)lparam, rebar->rebar_band_count);
            return 0;
        }

        kprintf("[USER32:REBAR] GETBAND hwnd=%x index=%u mask=%x "
                "child=%x cx=%d min=%dx%d id=%u\n",
                (uint32_t)(uintptr_t)hwnd, index, info->mask,
                (uint32_t)(uintptr_t)info->child, (int32_t)info->cx,
                (int32_t)info->cx_min_child, (int32_t)info->cy_min_child,
                info->id);
        return 1;
    }

    if (message == RB_GETBANDCOUNT)
        return rebar->rebar_band_count;
    if (message == RB_GETROWCOUNT)
        return rebar->rebar_band_count ? 1 : 0;
    if (message == RB_GETROWHEIGHT) {
        index = wparam;
        if (index >= rebar->rebar_band_count) return 0;
        return (int32_t)win32_rebar_band_height(rebar, index);
    }
    if (message == RB_GETBARHEIGHT)
        return rebar->bounds.h;

    if (message == RB_GETRECT) {
        int32_t *rect = (int32_t *)(uintptr_t)lparam;
        index = wparam;

        if (!rect || index >= rebar->rebar_band_count)
            return 0;

        rect[0] = rebar->rebar_rects[index].x;
        rect[1] = rebar->rebar_rects[index].y;
        rect[2] = rect[0] + rebar->rebar_rects[index].w;
        rect[3] = rect[1] + rebar->rebar_rects[index].h;

        kprintf("[USER32:REBAR] GETRECT hwnd=%x band=%u "
                "rect=%d,%d,%d,%d\n",
                (uint32_t)(uintptr_t)hwnd, index,
                rect[0], rect[1], rect[2], rect[3]);
        return 1;
    }

    if (message == RB_IDTOINDEX) {
        for (index = 0U; index < rebar->rebar_band_count; index++)
            if (rebar->rebar_ids[index] == wparam)
                return (int32_t)index;
        return -1;
    }

    if (message == RB_SHOWBAND) {
        index = wparam;
        if (index >= rebar->rebar_band_count) return 0;

        if (lparam)
            rebar->rebar_styles[index] &= ~RBBS_HIDDEN;
        else
            rebar->rebar_styles[index] |= RBBS_HIDDEN;

        win32_rebar_layout(hwnd, rebar);
        return 1;
    }

    if (message == RB_DELETEBAND) {
        index = wparam;
        if (index >= rebar->rebar_band_count) return 0;

        for (uint32_t move = index;
             move + 1U < rebar->rebar_band_count; move++) {
            rebar->rebar_masks[move] = rebar->rebar_masks[move + 1U];
            rebar->rebar_styles[move] = rebar->rebar_styles[move + 1U];
            rebar->rebar_ids[move] = rebar->rebar_ids[move + 1U];
            rebar->rebar_cx_min[move] = rebar->rebar_cx_min[move + 1U];
            rebar->rebar_cy_min[move] = rebar->rebar_cy_min[move + 1U];
            rebar->rebar_cx[move] = rebar->rebar_cx[move + 1U];
            rebar->rebar_children[move] =
                rebar->rebar_children[move + 1U];
            rebar->rebar_rects[move] =
                rebar->rebar_rects[move + 1U];
            kstrcpy(rebar->rebar_text[move],
                    rebar->rebar_text[move + 1U]);
        }

        rebar->rebar_band_count--;
        win32_rebar_layout(hwnd, rebar);
        return 1;
    }

    if (message == RB_SETBKCOLOR) {
        uint32_t old = rebar->rebar_background;
        rebar->rebar_background = (uint32_t)lparam;
        return (int32_t)old;
    }
    if (message == RB_GETBKCOLOR)
        return (int32_t)rebar->rebar_background;

    if (message == RB_SETTEXTCOLOR) {
        uint32_t old = rebar->rebar_text_color;
        rebar->rebar_text_color = (uint32_t)lparam;
        return (int32_t)old;
    }
    if (message == RB_GETTEXTCOLOR)
        return (int32_t)rebar->rebar_text_color;

    if (message == RB_SETPARENT) {
        void *old = rebar->parent;
        rebar->parent = (void *)(uintptr_t)wparam;
        return (int32_t)(uintptr_t)old;
    }

    if (message == RB_SIZETORECT && lparam) {
        const int32_t *rect = (const int32_t *)(uintptr_t)lparam;
        int width = rect[2] - rect[0];
        if (width > 0) rebar->bounds.w = width;
        win32_rebar_layout(hwnd, rebar);
        return 1;
    }

    if (message == RB_GETBARINFO || message == RB_SETBARINFO ||
        message == RB_HITTEST) {
        kprintf("[USER32:REBAR] COMPAT hwnd=%x msg=%x wp=%x lp=%x\n",
                (uint32_t)(uintptr_t)hwnd, message,
                wparam, (uint32_t)lparam);
        return message == RB_HITTEST ? -1 : 1;
    }

    if (message >= 0x0400U && message < 0x8000U) {
        kprintf("[USER32:REBAR] UNKNOWN hwnd=%x msg=%x wp=%x lp=%x\n",
                (uint32_t)(uintptr_t)hwnd, message,
                wparam, (uint32_t)lparam);
    }

    return win32_DefWindowProcA(hwnd, message, wparam, lparam);
}

static int32_t WIN32_API win32_SendMessageA(void *hwnd, uint32_t msg,
                                               uint32_t wp, int32_t lp) {
    win_window_t *w = window_from_handle(hwnd);
    if (!w) return 0;

    if (w->control && w->kind == 12U) {
        wndproc_t active = w->proc ? w->proc : w->default_proc;
        if (!active) active = win32_ReBarWndProc;

        kprintf("[USER32:REBAR] ROUTE hwnd=%x msg=%x wp=%x lp=%x "
                "active=%x default=%x subclass=%u\n",
                (uint32_t)(uintptr_t)hwnd, msg, wp, (uint32_t)lp,
                (uint32_t)(uintptr_t)active,
                (uint32_t)(uintptr_t)w->default_proc,
                active != w->default_proc ? 1U : 0U);

        return win32_call_or_queue_wndproc(
            active, hwnd, msg, wp, lp, NULL, 0U, -1);
    }

    /* Status bars, like Wine's StatusWindowProc class, participate in normal
     * subclass chaining. Send to the current proc first; CallWindowProc with
     * the value returned by SetWindowLong continues into the kernel default. */
    if (w->control && w->kind == 5U) {
        wndproc_t active = w->proc ? w->proc : w->default_proc;
        if (!active) active = win32_StatusBarWndProc;

        if (active != w->default_proc || msg == WM_SIZE ||
            msg == SB_SETPARTS || msg == SB_SETTEXTA ||
            msg == SB_SETTEXTW || msg == SB_GETRECT) {
            kprintf("[USER32:STATUS] ROUTE hwnd=%x msg=%x active=%x "
                    "default=%x subclass=%u\n",
                    (uint32_t)(uintptr_t)hwnd, msg,
                    (uint32_t)(uintptr_t)active,
                    (uint32_t)(uintptr_t)w->default_proc,
                    active != w->default_proc ? 1U : 0U);
        }

        {
            int32_t result = win32_call_or_queue_wndproc(
                active, hwnd, msg, wp, lp, NULL, 0U, -1);

            if (msg == WM_SIZE || msg == SB_SETPARTS ||
                msg == SB_GETPARTS || msg == SB_GETBORDERS ||
                msg == SB_SETMINHEIGHT || msg == SB_GETRECT ||
                msg == SB_SETTEXTA || msg == SB_GETTEXTA ||
                msg == SB_GETTEXTLENGTHA) {
                kprintf("[USER32:STATUS] RESULT hwnd=%x msg=%x "
                        "wp=%x lp=%x result=%x\n",
                        (uint32_t)(uintptr_t)hwnd, msg, wp,
                        (uint32_t)lp, (uint32_t)result);
            }

            return result;
        }
    }

    /* EDIT controls have a real default window procedure.  When an
     * application subclasses the control with SetWindowLong(GWL_WNDPROC),
     * messages reach that procedure first and CallWindowProc(old, ...)
    * continues into win32_EditWndProc. */
    if (w->control && w->kind == 1U)
        return w->proc ? win32_call_or_queue_wndproc(
                            w->proc, hwnd, msg, wp, lp, NULL, 0U, -1)
                       : win32_EditWndProc(hwnd, msg, wp, lp);
    if (msg == WM_SETTEXT)
        return win32_SetWindowTextA(hwnd, (const char *)(uintptr_t)lp);
    if (msg == WM_GETTEXT)
        return win32_GetWindowTextA(hwnd, (char *)(uintptr_t)lp, (int)wp);
    if (msg == WM_GETTEXTLENGTH) return win32_GetWindowTextLengthA(hwnd);
    if (msg == WM_SETFONT) {
        int font_height = w->font_pixel_height ? w->font_pixel_height : 8;
        bool font_bold = w->font_bold, font_italic = w->font_italic;
        bool font_monospace = w->font_monospace;
        w->font = (void *)(uintptr_t)wp;
        (void)win32_gdi_font_query(w->font, &font_height,
            &font_bold, &font_italic, &font_monospace);
        w->font_pixel_height = (int16_t)font_height;
        w->font_bold = font_bold; w->font_italic = font_italic;
        w->font_monospace = font_monospace;
        if (lp && w->native) { w->native->dirty = true; gui_request_paint(); }
        return 0;
    }
    if (msg == WM_GETFONT) return (int32_t)(uintptr_t)w->font;
    if (w->control && w->kind == 3U) {
        if (msg == STM_SETICON) {
            void *old = w->large_icon;
            w->large_icon = (void *)(uintptr_t)wp;
            if (w->native) { w->native->dirty = true; gui_request_paint(); }
            return (int32_t)(uintptr_t)old;
        }
        if (msg == STM_GETICON) return (int32_t)(uintptr_t)w->large_icon;
        if (msg == STM_SETIMAGE) {
            void *old = w->large_icon;
            if (wp == IMAGE_BITMAP || wp == IMAGE_ICON)
                w->large_icon = (void *)(uintptr_t)lp;
            if (w->native) { w->native->dirty = true; gui_request_paint(); }
            return (int32_t)(uintptr_t)old;
        }
        if (msg == STM_GETIMAGE &&
            (wp == IMAGE_BITMAP || wp == IMAGE_ICON))
            return (int32_t)(uintptr_t)w->large_icon;
    }
    if (w->control && w->kind == 2U) {
        if (msg == BM_GETCHECK) return (int32_t)w->check_state;
        if (msg == BM_SETCHECK) {
            w->check_state = wp;
            w->native->dirty = true;
            gui_request_paint();
            return 0;
        }
    }
    if(w->control&&(w->kind==6U||w->kind==7U))
        return control_list_message(w,msg,wp,lp);
    if(w->control&&(w->kind==9U||w->kind==10U||w->kind==11U))
        return common_control_message(w,hwnd,msg,wp,lp);
    if(w->control&&w->kind==8U){
        int old=w->scroll_pos;
        if(msg==PBM_SETRANGE){w->scroll_min=(int16_t)(lp&0xFFFF);w->scroll_max=(int16_t)((uint32_t)lp>>16);}
        else if(msg==PBM_SETPOS)w->scroll_pos=(int)wp;
        else if(msg==PBM_DELTAPOS)w->scroll_pos+=(int)wp;
        else if(msg==PBM_SETSTEP){old=w->scroll_page;w->scroll_page=(int)wp;}
        else if(msg==PBM_STEPIT)w->scroll_pos+=w->scroll_page;
        else return 0;
        if(w->scroll_pos<w->scroll_min)w->scroll_pos=w->scroll_min;
        if(w->scroll_pos>w->scroll_max)w->scroll_pos=w->scroll_max;
        if(w->native){w->native->dirty=true;gui_request_paint();}return old;
    }
    if (w->control && w->kind == 4U) {
        if (msg == TB_BUTTONSTRUCTSIZE) return 0;
        if (msg == TB_ADDBITMAP && lp) {
            const win_toolbar_add_bitmap_t *add =
                (const win_toolbar_add_bitmap_t *)(uintptr_t)lp;
            void *bitmap = NULL;
            if (add->instance) {
                void *resource = win32_resource_find(add->instance,
                    (const void *)(uintptr_t)WIN32_RT_BITMAP,
                    (const void *)(uintptr_t)add->bitmap_id, 0U, false);
                bitmap = win32_gdi_bitmap_from_resource(resource);
            } else {
                bitmap = (void *)(uintptr_t)add->bitmap_id;
            }
            {
                int first = toolbar_add_bitmap(w, wp, bitmap);
                kprintf("[USER32:TOOLBAR] ADDBITMAP hwnd=%x count=%u "
                        "bitmap=%x first=%d\n",
                        (uint32_t)(uintptr_t)hwnd, wp,
                        (uint32_t)(uintptr_t)bitmap, first);
                if (first >= 0 && w->native) {
                    w->native->dirty = true;
                    gui_request_paint();
                }
                return first;
            }
        }
        if (msg == TB_ADDBUTTONSA && lp) {
            const win_toolbar_button_t *buttons =
                (const win_toolbar_button_t *)(uintptr_t)lp;
            uint32_t available = 32U - w->toolbar_count;
            uint32_t count = wp < available ? wp : available;
            uint32_t first = w->toolbar_count;
            for (uint32_t i = 0; i < count; i++) {
                uint32_t slot = first + i;
                w->toolbar_bitmap_indices[slot] =
                    (int16_t)buttons[i].iBitmap;
                w->toolbar_commands[slot] =
                    (uint16_t)buttons[i].idCommand;
                w->toolbar_states[slot] = buttons[i].fsState;
                w->toolbar_styles[slot] = buttons[i].fsStyle;
            }
            w->toolbar_count = (uint8_t)(first + count);
            if (w->native) {
                w->native->dirty = true;
                gui_request_paint();
            }
            kprintf("[USER32:TOOLBAR] ADDBUTTONS hwnd=%x added=%u "
                    "total=%u\n", (uint32_t)(uintptr_t)hwnd, count,
                    w->toolbar_count);
            return count == wp;
        }
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
        if (msg == TB_SETBITMAPSIZE) {
            w->toolbar_bitmap_width = (int16_t)(lp & 0xFFFF);
            w->toolbar_bitmap_height =
                (int16_t)(((uint32_t)lp >> 16) & 0xFFFFU);
            return 1;
        }
        if (msg == TB_SETIMAGELIST) {
            void *old = w->listview_image_lists[0];
            w->listview_image_lists[0] = (void *)(uintptr_t)lp;
            if (w->native) { w->native->dirty = true; gui_request_paint(); }
            return (int32_t)(uintptr_t)old;
        }
        if (msg == TB_GETIMAGELIST)
            return (int32_t)(uintptr_t)w->listview_image_lists[0];
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
    return w->proc ? win32_call_or_queue_wndproc(
        w->proc, hwnd, msg, wp, lp, NULL, 0U, -1) : 0;
}
static int WIN32_API win32_PostMessageA(void*hwnd,uint32_t msg,uint32_t wp,int32_t lp){if(hwnd&&!window_from_handle(hwnd))return 0;queue_message(hwnd,msg,wp,lp);return 1;}
bool win32_user_post_message(void *hwnd, uint32_t message,
                             uint32_t wparam, int32_t lparam) {
    if (hwnd && !window_from_handle(hwnd)) return false;
    queue_message(hwnd, message, wparam, lparam);
    return true;
}
static void *WIN32_API win32_SetFocus(void *hwnd) {
    win_window_t *w = window_from_handle(hwnd);
    void *old = NULL;
    if (!w) return NULL;
    for (uint32_t i = 0; i < WIN32_MAX_WINDOWS; i++) {
        if (!win_windows[i].used || !win_windows[i].focused) continue;
        old = (void *)(uintptr_t)(HWND_BASE + i);
        if (old != hwnd && win_windows[i].proc)
            (void)win32_call_or_queue_wndproc(
                win_windows[i].proc, old, WM_KILLFOCUS,
                (uint32_t)(uintptr_t)hwnd, 0, NULL, 0U, -1);
        win_windows[i].focused = false;
    }
    w->focused = true;
    if (w->proc) (void)win32_call_or_queue_wndproc(
        w->proc, hwnd, WM_SETFOCUS, (uint32_t)(uintptr_t)old, 0,
        NULL, 0U, -1);
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

/* BLES_WINE_LOADSTRING_RESOURCE_DEBUG_20260723 */
static int WIN32_API win32_LoadStringW(void *instance, uint32_t id,
                                       uint16_t *buffer, int max_chars) {
    uint32_t block = id / 16U + 1U;
    uint32_t index = id % 16U;
    void *resource = win32_resource_find_w(instance,
        (const void *)(uintptr_t)WIN32_RT_STRING,
        (const void *)(uintptr_t)block, 0U, false);
    const uint16_t *data = (const uint16_t *)win32_resource_lock(resource);
    uint32_t bytes = win32_resource_size(instance, resource);

    if (!data || bytes < sizeof(uint16_t) ||
        max_chars <= 0 || !buffer) {
        kprintf("[USER32:LOADSTRING] FAIL instance=%x id=%u "
                "block=%u index=%u resource=%x bytes=%u max=%d error=%u\n",
                (uint32_t)(uintptr_t)instance, id, block, index,
                (uint32_t)(uintptr_t)resource, bytes, max_chars,
                pe_win32_get_last_error());
        return 0;
    }
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

    if (id >= 0x0290U && id <= 0x02A5U) {
        kprintf("[USER32:LOADSTRING] OK instance=%x id=%u "
                "block=%u index=%u chars=%u resource=%x\n",
                (uint32_t)(uintptr_t)instance, id, block, index,
                copied, (uint32_t)(uintptr_t)resource);
    }

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
    return win32_icon_load(instance, name, false, 32, 32);
}
static void *WIN32_API win32_LoadIconW(void *instance, const uint16_t *name) {
    return win32_icon_load(instance, name, true, 32, 32);
}
static void *WIN32_API win32_LoadBitmapA(void *instance, const char *name) {
    void *resource =
        load_resource_object_a(instance, name, WIN32_RT_BITMAP, 0U);
    void *bitmap = win32_gdi_bitmap_from_resource(resource);
    kprintf("[USER32:BITMAP] LoadBitmapA module=%x name=%s resource=%x "
            "bitmap=%x\n",
            (uint32_t)(uintptr_t)instance,
            (uint32_t)(uintptr_t)name > 0xFFFFU ? name : "#ID",
            (uint32_t)(uintptr_t)resource,
            (uint32_t)(uintptr_t)bitmap);
    return bitmap;
}
static void *WIN32_API win32_LoadBitmapW(void *instance, const uint16_t *name) {
    void *resource =
        load_resource_object_w(instance, name, WIN32_RT_BITMAP, 0U);
    return win32_gdi_bitmap_from_resource(resource);
}
static bool win32_queue_icon(void *dc, int x, int y, void *icon,
                             int requested_width, int requested_height) {
    const uint32_t *pixels;
    int width, height;
    gdi_command_t *command;
    if (!win32_icon_get(icon, &pixels, &width, &height) || !pixels) return false;
    if (requested_width <= 0) requested_width = width;
    if (requested_height <= 0) requested_height = height;
    command = gdi_slot(dc, 7U);
    if (!command) return false;
    command->pixels = (uint32_t *)kmalloc((size_t)width * (size_t)height *
                                          sizeof(uint32_t));
    if (!command->pixels) { command->used = false; return false; }
    kmemcpy(command->pixels, pixels, (size_t)width * (size_t)height *
            sizeof(uint32_t));
    command->x1 = x; command->y1 = y;
    command->x2 = x + requested_width; command->y2 = y + requested_height;
    command->pitch = width;
    command->color = (uint32_t)height;
    window_from_handle(dc)->native->dirty = true;
    gui_request_paint();
    return true;
}
static int WIN32_API win32_DrawIcon(void *dc, int x, int y, void *icon) {
    return win32_queue_icon(dc, x, y, icon, 0, 0) ? 1 : 0;
}
static int WIN32_API win32_DrawIconEx(void *dc, int x, int y, void *icon,
                                      int width, int height,
                                      uint32_t step UNUSED,
                                      void *brush UNUSED,
                                      uint32_t flags UNUSED) {
    return win32_queue_icon(dc, x, y, icon, width, height) ? 1 : 0;
}
static int WIN32_API win32_DestroyIcon(void *icon) {
    return win32_icon_destroy(icon) ? 1 : 0;
}
static void *WIN32_API win32_CopyIcon(void *icon) {
    return win32_icon_copy(icon);
}
static void *WIN32_API win32_CreateIconFromResourceEx(uint8_t *data,
    uint32_t size, int icon UNUSED, uint32_t version UNUSED,
    int width, int height, uint32_t flags UNUSED) {
    return win32_icon_create_from_resource(data, size, width, height);
}
static void *WIN32_API win32_CreateIconFromResource(uint8_t *data,
    uint32_t size, int icon, uint32_t version) {
    return win32_CreateIconFromResourceEx(data, size, icon, version,
                                          0, 0, 0U);
}
static void *WIN32_API win32_LoadImageA(void *instance, const char *name,
    uint32_t type, int width, int height, uint32_t flags UNUSED) {
    if (type == 1U) return win32_icon_load(instance, name, false, width, height);
    if (type == 2U) return win32_LoadCursorA(instance, name);
    if (type == 0U) return win32_LoadBitmapA(instance, name);
    return NULL;
}
static void *WIN32_API win32_LoadImageW(void *instance, const uint16_t *name,
    uint32_t type, int width, int height, uint32_t flags UNUSED) {
    if (type == 1U) return win32_icon_load(instance, name, true, width, height);
    if (type == 2U) return win32_LoadCursorW(instance, name);
    if (type == 0U) return win32_LoadBitmapW(instance, name);
    return NULL;
}
static int WIN32_API win32_InvalidateRect(void*hwnd,const int32_t*rect UNUSED,int erase UNUSED){win_window_t*w=window_from_handle(hwnd);if(!w)return 0;w->native->dirty=true;queue_message(hwnd,WM_PAINT,0,0);gui_request_paint();return 1;}
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
static int WIN32_API win32_GetWindowRect(void*hwnd,int32_t*rect){win_window_t*w=window_from_handle(hwnd);gui_rect_t r;if(hwnd==DESKTOP_HWND)return win32_GetClientRect(hwnd,rect);if(!w||!rect)return 0;r=window_screen_rect(w);rect[0]=r.x;rect[1]=r.y;rect[2]=r.x+r.w;rect[3]=r.y+r.h;return 1;}
static int WIN32_API win32_SetWindowPos(void *hwnd, void *after,
                                         int x, int y, int width, int height,
                                         uint32_t flags) {
    win_window_t *w = window_from_handle(hwnd);
    gui_desktop_t *desktop = gui_get_desktop();
    bool moved = (flags & SWP_NOMOVE) == 0U;
    bool sized = (flags & SWP_NOSIZE) == 0U;

    if (!w) return 0;
    struct { void *hwnd; void *insert_after; int x,y,cx,cy; uint32_t flags; } position =
        {hwnd, after, x, y, width, height, flags};
    if (w->proc) (void)win32_call_or_queue_wndproc(
        w->proc, hwnd, WM_WINDOWPOSCHANGING, 0U,
        (int32_t)(uintptr_t)&position, &position,
        (uint8_t)sizeof(position), 3);
    after=position.insert_after;x=position.x;y=position.y;width=position.cx;height=position.cy;flags=position.flags;
    moved = (flags & SWP_NOMOVE) == 0U; sized = (flags & SWP_NOSIZE) == 0U;

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
            /* WIN32_RING3_WM_KILLFOCUS */
            if (w->proc)
                (void)win32_queue_wndproc_upcall(
                    w->proc, hwnd, WM_KILLFOCUS, 0U, 0,
                    NULL, 0U, -1);
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

    if (w->proc) { position.insert_after=after;position.x=x;position.y=y;position.cx=width;position.cy=height;position.flags=flags;(void)win32_call_or_queue_wndproc(w->proc,hwnd,WM_WINDOWPOSCHANGED,0U,(int32_t)(uintptr_t)&position,&position,(uint8_t)sizeof(position),3); }
    if (w->native) w->native->dirty = true;
    gui_request_paint();
    return 1;
}

/* WIN32_USER32_SCROLLWINDOWEX */
static void win32_rect_intersect(int32_t *out,
                                 const int32_t *left,
                                 const int32_t *right) {
    out[0] = left[0] > right[0] ? left[0] : right[0];
    out[1] = left[1] > right[1] ? left[1] : right[1];
    out[2] = left[2] < right[2] ? left[2] : right[2];
    out[3] = left[3] < right[3] ? left[3] : right[3];

    if (out[2] < out[0]) out[2] = out[0];
    if (out[3] < out[1]) out[3] = out[1];
}

static int WIN32_API win32_ScrollWindowEx(
        void *hwnd,
        int dx,
        int dy,
        const int32_t *scroll_rect,
        const int32_t *clip_rect,
        void *update_region UNUSED,
        int32_t *update_rect,
        uint32_t flags) {
    win_window_t *window = window_from_handle(hwnd);
    int32_t client[4];
    int32_t affected[4];

    if (!window || !win32_GetClientRect(hwnd, client)) return 0;

    if (scroll_rect) {
        affected[0] = scroll_rect[0];
        affected[1] = scroll_rect[1];
        affected[2] = scroll_rect[2];
        affected[3] = scroll_rect[3];
    } else {
        affected[0] = client[0];
        affected[1] = client[1];
        affected[2] = client[2];
        affected[3] = client[3];
    }

    if (clip_rect) {
        int32_t clipped[4];
        win32_rect_intersect(clipped, affected, clip_rect);
        affected[0] = clipped[0];
        affected[1] = clipped[1];
        affected[2] = clipped[2];
        affected[3] = clipped[3];
    }

    if (update_rect) {
        update_rect[0] = affected[0];
        update_rect[1] = affected[1];
        update_rect[2] = affected[2];
        update_rect[3] = affected[3];
    }

    if ((flags & SW_SCROLLCHILDREN) && (dx || dy)) {
        for (uint32_t i = 0; i < WIN32_MAX_WINDOWS; i++) {
            win_window_t *child = &win_windows[i];

            if (!child->used || !child->control || child->parent != hwnd)
                continue;

            child->bounds.x += dx;
            child->bounds.y += dy;

            if (win32_is_edit_control(child)) {
                edit_update_scroll_info(child);
                edit_scroll_caret(child);
            }
        }
    }

    /*
     * BlesKernOS does not retain a backing bitmap for arbitrary Win32
     * windows yet. Wine ultimately invalidates the exposed region; doing the
     * same here lets the application repaint it through WM_PAINT.
     */
    if ((flags & (SW_INVALIDATE | SW_ERASE)) || dx || dy)
        (void)win32_InvalidateRect(hwnd, affected,
                                  (flags & SW_ERASE) != 0U);

    if ((flags & SW_SMOOTHSCROLL) != 0U)
        task_yield();

    return (dx || dy) ? SIMPLEREGION : NULLREGION;
}

static int WIN32_API win32_ScrollWindow(
        void *hwnd,
        int dx,
        int dy,
        const int32_t *scroll_rect,
        const int32_t *clip_rect) {
    return win32_ScrollWindowEx(
        hwnd, dx, dy, scroll_rect, clip_rect,
        NULL, NULL, SW_INVALIDATE | SW_ERASE) != 0;
}

static int WIN32_API win32_EnableWindow(void*hwnd,int enable){win_window_t*w=window_from_handle(hwnd);int was_disabled;if(!w)return 0;was_disabled=!w->enabled;w->enabled=enable!=0;if(!w->enabled)w->focused=false;if(!w->control&&w->native)w->native->input_enabled=w->enabled;w->native->dirty=true;gui_request_paint();return was_disabled;}
static int WIN32_API win32_IsWindowEnabled(void*hwnd){win_window_t*w=window_from_handle(hwnd);return w&&w->enabled;}
static int WIN32_API win32_IsWindowVisible(void*hwnd){win_window_t*w=window_from_handle(hwnd);return w&&w->visible;}
static int WIN32_API win32_IsIconic(void *hwnd) {
    win_window_t *w = window_from_handle(hwnd);
    return w && (w->style & WS_MINIMIZE) != 0U;
}
static int WIN32_API win32_IsZoomed(void *hwnd) {
    win_window_t *w = window_from_handle(hwnd);
    return w && (w->style & WS_MAXIMIZE) != 0U;
}
static void*WIN32_API win32_GetParent(void*hwnd){win_window_t*w=window_from_handle(hwnd);return w?w->parent:NULL;}
static bool property_name_equal(const typeof(((win_window_t*)0)->properties[0]) *property,
                                const char *name) {
    uint32_t value=(uint32_t)(uintptr_t)name;
    if (!property || !property->used || !name) return false;
    if (value <= 0xFFFFU) return property->atom == (uint16_t)value;
    return property->atom == 0U && equal(property->name,name);
}
static int WIN32_API win32_SetPropA(void*hwnd,const char*name,void*data){win_window_t*w=window_from_handle(hwnd);if(!w||!name)return 0;for(uint32_t i=0;i<WIN32_WINDOW_PROPERTIES;i++)if(property_name_equal(&w->properties[i],name)){w->properties[i].value=data;return 1;}for(uint32_t i=0;i<WIN32_WINDOW_PROPERTIES;i++)if(!w->properties[i].used){uint32_t value=(uint32_t)(uintptr_t)name;w->properties[i].used=true;w->properties[i].value=data;if(value<=0xFFFFU)w->properties[i].atom=(uint16_t)value;else kstrncpy(w->properties[i].name,name,sizeof(w->properties[i].name)-1U);return 1;}return 0;}
static void*WIN32_API win32_GetPropA(void*hwnd,const char*name){win_window_t*w=window_from_handle(hwnd);if(!w||!name)return NULL;for(uint32_t i=0;i<WIN32_WINDOW_PROPERTIES;i++)if(property_name_equal(&w->properties[i],name))return w->properties[i].value;return NULL;}
static void*WIN32_API win32_RemovePropA(void*hwnd,const char*name){win_window_t*w=window_from_handle(hwnd);if(!w||!name)return NULL;for(uint32_t i=0;i<WIN32_WINDOW_PROPERTIES;i++)if(property_name_equal(&w->properties[i],name)){void*old=w->properties[i].value;kmemset(&w->properties[i],0,sizeof(w->properties[i]));return old;}return NULL;}
static int WIN32_API win32_SetPropW(void*hwnd,const uint16_t*name,void*data){char a[32];if((uint32_t)(uintptr_t)name<=0xFFFFU)return win32_SetPropA(hwnd,(const char*)name,data);if(!wide_to_ansi(name,a,sizeof(a)))return 0;return win32_SetPropA(hwnd,a,data);}
static void*WIN32_API win32_GetPropW(void*hwnd,const uint16_t*name){char a[32];if((uint32_t)(uintptr_t)name<=0xFFFFU)return win32_GetPropA(hwnd,(const char*)name);if(!wide_to_ansi(name,a,sizeof(a)))return NULL;return win32_GetPropA(hwnd,a);}
static void*WIN32_API win32_RemovePropW(void*hwnd,const uint16_t*name){char a[32];if((uint32_t)(uintptr_t)name<=0xFFFFU)return win32_RemovePropA(hwnd,(const char*)name);if(!wide_to_ansi(name,a,sizeof(a)))return NULL;return win32_RemovePropA(hwnd,a);}
static void* handle_for_index(uint32_t index){return index<WIN32_MAX_WINDOWS&&win_windows[index].used?(void*)(uintptr_t)(HWND_BASE+index):NULL;}
static void*WIN32_API win32_GetWindow(void*hwnd,uint32_t command){win_window_t*w=window_from_handle(hwnd);int index=w?(int)(w-win_windows):-1;if(!w)return NULL;if(command==GW_OWNER)return w->parent;if(command==GW_CHILD){for(uint32_t i=0;i<WIN32_MAX_WINDOWS;i++)if(win_windows[i].used&&win_windows[i].parent==hwnd)return handle_for_index(i);return NULL;}if(command==GW_HWNDFIRST||command==GW_HWNDLAST){void*found=NULL;for(uint32_t i=0;i<WIN32_MAX_WINDOWS;i++)if(win_windows[i].used&&win_windows[i].parent==w->parent){found=handle_for_index(i);if(command==GW_HWNDFIRST)return found;}return found;}if(command==GW_HWNDNEXT){for(uint32_t i=(uint32_t)index+1U;i<WIN32_MAX_WINDOWS;i++)if(win_windows[i].used&&win_windows[i].parent==w->parent)return handle_for_index(i);}else if(command==GW_HWNDPREV){for(int i=index-1;i>=0;i--)if(win_windows[i].used&&win_windows[i].parent==w->parent)return handle_for_index((uint32_t)i);}return NULL;}
static void*WIN32_API win32_GetTopWindow(void*parent){if(!parent)parent=DESKTOP_HWND;for(uint32_t i=0;i<WIN32_MAX_WINDOWS;i++)if(win_windows[i].used&&((parent==DESKTOP_HWND&&!win_windows[i].parent)||win_windows[i].parent==parent))return handle_for_index(i);return NULL;}
static void*WIN32_API win32_SetParent(void*hwnd,void*parent){win_window_t*w=window_from_handle(hwnd);void*old;if(!w)return NULL;old=w->parent;w->parent=parent;if(w->control)w->style|=WS_CHILD;return old;}
static void*WIN32_API win32_GetAncestor(void*hwnd,uint32_t flags){win_window_t*w=window_from_handle(hwnd);void*current=hwnd;if(!w)return NULL;if(flags==GA_PARENT)return w->parent;while(w&&w->parent&&w->parent!=DESKTOP_HWND){current=w->parent;w=window_from_handle(current);}return (flags==GA_ROOT||flags==GA_ROOTOWNER)?current:NULL;}
static void*WIN32_API win32_GetForegroundWindow(void){gui_desktop_t*d=gui_get_desktop();return d?handle_from_native(d->focused_window):NULL;}
static void*WIN32_API win32_GetDesktopWindow(void){return DESKTOP_HWND;}
static void*WIN32_API win32_GetActiveWindow(void){return win32_GetForegroundWindow();}
static void*WIN32_API win32_GetLastActivePopup(void*hwnd){
    win_window_t*root=window_from_handle(hwnd);
    void*active;
    win_window_t*popup;
    if(!root)return NULL;
    active=win32_GetForegroundWindow();
    popup=window_from_handle(active);
    while(popup&&popup->parent&&popup->parent!=DESKTOP_HWND){
        if(popup->parent==hwnd&&!popup->control)return active;
        popup=window_from_handle(popup->parent);
    }
    return hwnd;
}
static void*WIN32_API win32_SetActiveWindow(void*hwnd){void*old=win32_GetForegroundWindow();if(hwnd)win32_SetForegroundWindow(hwnd);return old;}
static void *win_capture_window;
static void*WIN32_API win32_SetCapture(void*hwnd){void*old=win_capture_window;if(window_from_handle(hwnd))win_capture_window=hwnd;return old;}
static void*WIN32_API win32_GetCapture(void){return win_capture_window;}
static int WIN32_API win32_ReleaseCapture(void){win_capture_window=NULL;return 1;}
static int WIN32_API win32_SetForegroundWindow(void*hwnd){win_window_t*w=window_from_handle(hwnd);gui_desktop_t*d=gui_get_desktop();if(!w||w->control||!d)return 0;gui_desktop_focus_window(d,w->native);gui_desktop_raise_window(d,w->native);gui_request_paint();return 1;}
static int WIN32_API win32_BringWindowToTop(void*hwnd){win_window_t*w=window_from_handle(hwnd);gui_desktop_t*d=gui_get_desktop();if(!w||w->control||!d)return 0;gui_desktop_raise_window(d,w->native);w->native->dirty=true;gui_request_paint();return 1;}
static int WIN32_API win32_ClientToScreen(void*hwnd,int32_t*point){win_window_t*w=window_from_handle(hwnd);gui_rect_t r;if(!w||!point)return 0;if(w->control)r=window_screen_rect(w);else r=gui_window_content_rect(w->native);point[0]+=r.x;point[1]+=r.y;return 1;}
static int WIN32_API win32_ScreenToClient(void*hwnd,int32_t*point){win_window_t*w=window_from_handle(hwnd);gui_rect_t r;if(!w||!point)return 0;if(w->control)r=window_screen_rect(w);else r=gui_window_content_rect(w->native);point[0]-=r.x;point[1]-=r.y;return 1;}
static int WIN32_API win32_MapWindowPoints(void*from,void*to,int32_t*points,uint32_t count){int32_t delta[2]={0,0};if(!points)return 0;if(from&&from!=DESKTOP_HWND){if(!win32_ClientToScreen(from,delta))return 0;}if(to&&to!=DESKTOP_HWND){int32_t origin[2]={0,0};if(!win32_ClientToScreen(to,origin))return 0;delta[0]-=origin[0];delta[1]-=origin[1];}for(uint32_t i=0;i<count;i++){points[i*2]+=delta[0];points[i*2+1]+=delta[1];}return(int)((uint16_t)delta[0]|((uint32_t)(uint16_t)delta[1]<<16));}
static int WIN32_API win32_IsChild(void*parent,void*child){win_window_t*w=window_from_handle(child);while(w&&w->parent){if(w->parent==parent)return 1;w=window_from_handle(w->parent);}return 0;}

/* WIN32_USER32_CHILD_FROM_POINT */
typedef struct {
    int32_t x;
    int32_t y;
} win32_point_t;

static bool win32_point_in_rect(int32_t x, int32_t y,
                                int32_t left, int32_t top,
                                int32_t right, int32_t bottom) {
    return x >= left && y >= top && x < right && y < bottom;
}

static void *win32_child_from_point(void *parent,
                                    win32_point_t point,
                                    uint32_t flags) {
    if (!parent) parent = DESKTOP_HWND;
    if (parent != DESKTOP_HWND && !window_from_handle(parent)) return NULL;

    for (uint32_t n = WIN32_MAX_WINDOWS; n > 0U; n--) {
        uint32_t index = n - 1U;
        win_window_t *child = &win_windows[index];
        bool direct_child;

        if (!child->used) continue;
        direct_child = parent == DESKTOP_HWND
            ? child->parent == NULL || child->parent == DESKTOP_HWND
            : child->parent == parent;
        if (!direct_child) continue;
        if ((flags & CWP_SKIPINVISIBLE) && !child->visible) continue;
        if ((flags & CWP_SKIPDISABLED) && !child->enabled) continue;
        if ((flags & CWP_SKIPTRANSPARENT) &&
            (child->exstyle & WS_EX_TRANSPARENT)) continue;

        if (child->control) {
            if (win32_point_in_rect(point.x, point.y,
                    child->bounds.x, child->bounds.y,
                    child->bounds.x + child->bounds.w,
                    child->bounds.y + child->bounds.h))
                return handle_for_index(index);
        } else {
            gui_rect_t bounds = child->native
                ? child->native->bounds : child->bounds;
            if (parent == DESKTOP_HWND &&
                win32_point_in_rect(point.x, point.y,
                    bounds.x, bounds.y,
                    bounds.x + bounds.w, bounds.y + bounds.h))
                return handle_for_index(index);
        }
    }

    return parent;
}

static void *WIN32_API win32_ChildWindowFromPoint(
        void *parent, win32_point_t point) {
    return win32_child_from_point(parent, point, 0U);
}

static void *WIN32_API win32_ChildWindowFromPointEx(
        void *parent, win32_point_t point, uint32_t flags) {
    return win32_child_from_point(parent, point, flags);
}

static void *WIN32_API win32_RealChildWindowFromPoint(
        void *parent, win32_point_t point) {
    return win32_child_from_point(parent, point, CWP_SKIPINVISIBLE);
}

static void *WIN32_API win32_WindowFromPoint(win32_point_t point) {
    for (uint32_t n = WIN32_MAX_WINDOWS; n > 0U; n--) {
        uint32_t index = n - 1U;
        win_window_t *window = &win_windows[index];
        gui_rect_t bounds;

        if (!window->used || !window->visible) continue;
        bounds = window_screen_rect(window);
        if (win32_point_in_rect(point.x, point.y,
                bounds.x, bounds.y,
                bounds.x + bounds.w, bounds.y + bounds.h))
            return handle_for_index(index);
    }
    return DESKTOP_HWND;
}

static int WIN32_API win32_AdjustWindowRectEx(int32_t*rect,uint32_t style,int menu,uint32_t exstyle UNUSED){int border=(style&WS_BORDER)?GUI_BORDER_SIZE:0;int top=border+(menu?18:0);if(!rect)return 0;rect[0]-=border;rect[2]+=border;rect[1]-=top;rect[3]+=border;return 1;}
static int WIN32_API win32_AdjustWindowRect(int32_t*rect,uint32_t style,int menu){return win32_AdjustWindowRectEx(rect,style,menu,0);}

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
static int16_t WIN32_API win32_GetAsyncKeyState(int key){return win32_GetKeyState(key);}
static uint32_t WIN32_API win32_MapVirtualKeyA(uint32_t code,uint32_t type UNUSED){return code;}
static int16_t WIN32_API win32_VkKeyScanA(uint8_t character){if(character>='a'&&character<='z')return(int16_t)(character-'a'+'A');if(character>='A'&&character<='Z')return(int16_t)((character)|(1U<<8));return(int16_t)character;}
static int WIN32_API win32_GetKeyboardType(int type){return type==0?4:(type==1?0:(type==2?12:0));}

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

/*
 * WinHelp is an out-of-process service on Win95/98.  BlesKernOS does not
 * provide WINHLP32 yet, so report the operation as unavailable without
 * failing PE import binding. Applications such as WinZip use the return
 * value to keep running when contextual help cannot be opened.
 */
static int WIN32_API win32_WinHelpA(void *owner UNUSED,
                                    const char *help_file UNUSED,
                                    uint32_t command UNUSED,
                                    uint32_t data UNUSED) {
    pe_win32_set_last_error(2U); /* ERROR_FILE_NOT_FOUND */
    return 0;
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
            /* SetClipboardData transfers ownership to the system. */
            win32_global_transfer_handle(handle, 0U);
            return handle;
        }
        if (!free_entry && win_clipboard[i].format == 0U)
            free_entry = &win_clipboard[i];
    }
    if (!free_entry) return NULL;
    free_entry->format = format;
    free_entry->handle = handle;
    win32_global_transfer_handle(handle, 0U);
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

/* Wine ultimately routes SendMessageTimeout through the same synchronous
 * message machinery and reports the window-procedure result separately.  All
 * BlesKernOS Win32 windows currently live in one address space, so there is no
 * hung remote GUI thread to wait for: dispatch synchronously and complete the
 * call immediately. */
static int32_t WIN32_API win32_SendMessageTimeoutA(
    void *hwnd, uint32_t message, uint32_t wparam, int32_t lparam,
    uint32_t flags UNUSED, uint32_t timeout UNUSED, uint32_t *result) {
    int32_t value = win32_SendMessageA(hwnd, message, wparam, lparam);
    if (result) *result = (uint32_t)value;
    return 1;
}

static int32_t WIN32_API win32_SendMessageTimeoutW(
    void *hwnd, uint32_t message, uint32_t wparam, int32_t lparam,
    uint32_t flags, uint32_t timeout, uint32_t *result) {
    return win32_SendMessageTimeoutA(hwnd, message, wparam, lparam,
                                     flags, timeout, result);
}

/* BLES_WINE_STATUSBAR_LAYOUT_METRICS_20260723
 *
 * WinZip's WzSBar layout asks for SM_CXHSCROLL (21) before calculating
 * its SB_SETPARTS edges. Returning zero here is not a harmless placeholder:
 * old common-control applications use these values as real geometry.
 */
static int WIN32_API win32_GetSystemMetrics(int index) {
    gui_desktop_t *desktop = gui_get_desktop();
    int screen_width = desktop ? desktop->surface.width : 800;
    int screen_height = desktop ? desktop->surface.height : 600;
    int result;

    switch (index) {
    case 0:  result = screen_width; break;
    case 1:  result = screen_height; break;
    case 2:  result = 16; break;
    case 3:  result = 16; break;
    case 4:  result = GUI_TITLEBAR_HEIGHT; break;
    case 5:
    case 6:  result = GUI_BORDER_SIZE; break;
    case 7:
    case 8:  result = GUI_BORDER_SIZE * 2; break;
    case 9:
    case 10: result = 16; break;
    case 11:
    case 12: result = 32; break;
    case 13:
    case 14: result = 32; break;
    case 15: result = 18; break;
    case 16: result = screen_width; break;
    case 17: result = screen_height - GUI_TITLEBAR_HEIGHT; break;
    case 18: result = 0; break;
    case 19: result = 1; break;
    case 20: result = 16; break;
    case 21: result = 16; break;
    case 22:
    case 23: result = 0; break;
    case 28: result = 112; break;
    case 29: result = GUI_TITLEBAR_HEIGHT + 8; break;
    case 30:
    case 31: result = 16; break;
    case 32:
    case 33: result = GUI_BORDER_SIZE * 2; break;
    case 34: result = 112; break;
    case 35: result = GUI_TITLEBAR_HEIGHT + 8; break;
    case 36:
    case 37: result = 4; break;
    case 38:
    case 39: result = 75; break;
    case 43: result = 2; break;
    case 45:
    case 46: result = 2; break;
    default: result = 0; break;
    }

    if (index == 15 || index == 20 || index == 21 ||
        index == 30 || index == 31) {
        kprintf("[USER32:METRIC] index=%d result=%d\n", index, result);
    }

    return result;
}
static int WIN32_API win32_GetClassNameA(void*hwnd,char*out,int size){win_window_t*w=window_from_handle(hwnd);if(!w||!out||size<=0)return 0;kstrncpy(out,w->class_name,(size_t)size-1U);out[size-1]='\0';return(int)kstrlen(out);}
static void*WIN32_API win32_FindWindowA(const char*class_name,const char*title){for(uint32_t i=0;i<WIN32_MAX_WINDOWS;i++){win_window_t*w=&win_windows[i];if(!w->used||w->control)continue;if(class_name&& !equal_ci(class_name,w->class_name))continue;if(title&& !equal(title,w->native->title))continue;return(void*)(uintptr_t)(HWND_BASE+i);}return NULL;}
static int32_t WIN32_API win32_GetWindowLongA(void *hwnd, int index) {
    win_window_t *w = window_from_handle(hwnd);
    int32_t value = 0;
    if (!w) return 0;
    if (index >= 0) {
        uint32_t offset = (uint32_t)index;
        if (!w->window_extra || offset > w->window_extra_size ||
            4U > w->window_extra_size - offset) return 0;
        kmemcpy(&value, w->window_extra + offset, sizeof(value));
        return value;
    }
    switch(index) {
        case GWL_WNDPROC:return(int32_t)(uintptr_t)w->proc;
        case GWL_HINSTANCE:return(int32_t)(uintptr_t)w->instance;
        case GWL_HWNDPARENT:return(int32_t)(uintptr_t)w->parent;
        case GWL_ID:return(int32_t)w->id;
        case GWL_STYLE:return(int32_t)w->style;
        case GWL_EXSTYLE:return(int32_t)w->exstyle;
        case GWL_USERDATA:return w->user_data;
        default:return 0;
    }
}
static int32_t WIN32_API win32_SetWindowLongA(void *hwnd, int index,
                                               int32_t value) {
    win_window_t *w = window_from_handle(hwnd);
    int32_t old;
    if (!w) return 0;
    if (index >= 0) {
        uint32_t offset = (uint32_t)index;
        if (!w->window_extra || offset > w->window_extra_size ||
            4U > w->window_extra_size - offset) return 0;
        kmemcpy(&old, w->window_extra + offset, sizeof(old));
        kmemcpy(w->window_extra + offset, &value, sizeof(value));
        return old;
    }
    old=win32_GetWindowLongA(hwnd,index);
    if (index == GWL_WNDPROC) {
        kprintf("[USER32:SUBCLASS] hwnd=%x class=%s kind=%u old=%x "
                "new=%x default=%x\n",
                (uint32_t)(uintptr_t)hwnd, w->class_name, w->kind,
                (uint32_t)old, (uint32_t)value,
                (uint32_t)(uintptr_t)w->default_proc);
    }
    switch(index){case GWL_WNDPROC:w->proc=(wndproc_t)(uintptr_t)value;break;case GWL_HINSTANCE:w->instance=(void*)(uintptr_t)value;break;case GWL_HWNDPARENT:w->parent=(void*)(uintptr_t)value;break;case GWL_ID:w->id=(uint32_t)value;break;case GWL_STYLE:w->style=(uint32_t)value;if(w->control&&w->kind==1U)w->edit_readonly=(w->style&ES_READONLY)!=0U;break;case GWL_EXSTYLE:w->exstyle=(uint32_t)value;break;case GWL_USERDATA:w->user_data=value;break;default:return 0;}
    if (index == GWL_WNDPROC && w->control) {
        queue_message(hwnd, WM_PAINT, 0U, 0);
        if (w->native) w->native->dirty = true;
        gui_request_paint();
    }
    return old;
}
static int32_t WIN32_API win32_GetWindowLongW(void*hwnd,int index){return win32_GetWindowLongA(hwnd,index);}
static int32_t WIN32_API win32_SetWindowLongW(void*hwnd,int index,int32_t value){return win32_SetWindowLongA(hwnd,index,value);}
static int32_t WIN32_API win32_CallWindowProcA(
    void *proc, void *hwnd, uint32_t msg, uint32_t wp, int32_t lp) {
    win_window_t *window = window_from_handle(hwnd);
    if (window && window->control && window->kind == 5U) {
        kprintf("[USER32:SUBCLASS] CallWindowProc status hwnd=%x proc=%x "
                "msg=%x wp=%x lp=%x\n",
                (uint32_t)(uintptr_t)hwnd,
                (uint32_t)(uintptr_t)proc,
                msg, wp, (uint32_t)lp);
    }
    return proc ? win32_call_or_queue_wndproc(
        (wndproc_t)proc, hwnd, msg, wp, lp, NULL, 0U, -1) : 0;
}
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
static int WIN32_API win32_GetDlgCtrlID(void*hwnd){win_window_t*w=window_from_handle(hwnd);return w?(int)w->id:0;}
static int WIN32_API win32_SetDlgItemInt(void*dialog,int id,uint32_t value,int signed_value){char text[16];char reversed[16];uint32_t used=0,n=value;bool negative=signed_value&&(int32_t)value<0;if(negative)n=(uint32_t)(-(int32_t)value);do{reversed[used++]=(char)('0'+n%10U);n/=10U;}while(n&&used<14U);uint32_t out=0;if(negative)text[out++]='-';while(used)text[out++]=reversed[--used];text[out]=0;return win32_SetDlgItemTextA(dialog,id,text);}
static uint32_t WIN32_API win32_GetDlgItemInt(void*dialog,int id,int*translated,int signed_value){char text[32];uint32_t value=0,i=0;bool negative=false;if(translated)*translated=0;if(!win32_GetDlgItemTextA(dialog,id,text,sizeof(text)))return 0;if(text[0]=='-'&&signed_value){negative=true;i++;}if(!text[i])return 0;for(;text[i];i++){if(text[i]<'0'||text[i]>'9')return 0;value=value*10U+(uint32_t)(text[i]-'0');}if(translated)*translated=1;return negative?(uint32_t)(-(int32_t)value):value;}
static int WIN32_API win32_IsWindow(void*hwnd){return hwnd==DESKTOP_HWND||window_from_handle(hwnd)!=NULL;}
static uint32_t WIN32_API win32_GetWindowThreadProcessId(void*hwnd,uint32_t*process_id){win_window_t*w=window_from_handle(hwnd);uint32_t tid;if(!w)return 0;while(w->control&&w->parent){win_window_t*p=window_from_handle(w->parent);if(!p)break;w=p;}tid=w->native?w->native->owner_pid:task_current_pid();if(process_id){*process_id=tid;for(uint32_t i=0;i<task_count();i++){const task_t*t=task_get(i);if(t&&t->pid==tid){*process_id=t->process_id;break;}}}return tid;}
typedef int (WIN32_API *enum_window_proc_t)(void*,int32_t);
static int WIN32_API win32_EnumWindows(enum_window_proc_t proc,int32_t param){if(!proc)return 0;for(uint32_t i=0;i<WIN32_MAX_WINDOWS;i++)if(win_windows[i].used&&!win_windows[i].control)if(!proc((void*)(uintptr_t)(HWND_BASE+i),param))return 0;return 1;}
static int WIN32_API win32_EnumChildWindows(void*parent,enum_window_proc_t proc,int32_t param){if(!proc)return 0;for(uint32_t i=0;i<WIN32_MAX_WINDOWS;i++)if(win_windows[i].used&&win_windows[i].parent==parent)if(!proc((void*)(uintptr_t)(HWND_BASE+i),param))return 0;return 1;}
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

/* WIN32_USER32_GETSYSTEMMENU */
static void win32_system_menu_item(win_menu_t *menu, uint32_t id,
                                   uint32_t flags, const char *text) {
    win_menu_item_t *item;

    if (!menu || menu->count >= WIN32_MAX_MENU_ITEMS) return;

    item = &menu->items[menu->count++];
    kmemset(item, 0, sizeof(*item));
    item->id = id;
    item->flags = flags;
    kstrncpy(item->text, text ? text : "",
             sizeof(item->text) - 1U);
    item->text[sizeof(item->text) - 1U] = '\0';
}

static void win32_release_system_menu(uint32_t window_index) {
    uint32_t handle_value;
    uint32_t menu_index;

    if (window_index >= WIN32_MAX_WINDOWS) return;

    handle_value =
        (uint32_t)(uintptr_t)win_system_menus[window_index];

    if (handle_value >= MENU_BASE &&
        handle_value < MENU_BASE + WIN32_MAX_MENUS) {
        menu_index = handle_value - MENU_BASE;
        kmemset(&win_menus[menu_index], 0, sizeof(win_menus[menu_index]));
    }

    win_system_menus[window_index] = NULL;
}

static void *WIN32_API win32_GetSystemMenu(void *hwnd, int revert) {
    win_window_t *window = window_from_handle(hwnd);
    uint32_t window_index;
    win_menu_t *menu;

    if (!window || window->control) return NULL;

    window_index = (uint32_t)(window - win_windows);

    if (revert) {
        win32_release_system_menu(window_index);
        return NULL;
    }

    menu = menu_from_handle(win_system_menus[window_index]);
    if (menu) return win_system_menus[window_index];

    for (uint32_t i = 0; i < WIN32_MAX_MENUS; i++) {
        if (win_menus[i].used) continue;

        menu = &win_menus[i];
        kmemset(menu, 0, sizeof(*menu));
        menu->used = true;

        win32_system_menu_item(menu, SC_RESTORE, MF_STRING, "Restore");
        win32_system_menu_item(menu, SC_MOVE, MF_STRING, "Move");
        win32_system_menu_item(menu, SC_SIZE, MF_STRING, "Size");
        win32_system_menu_item(menu, SC_MINIMIZE, MF_STRING, "Minimize");
        win32_system_menu_item(menu, SC_MAXIMIZE, MF_STRING, "Maximize");
        win32_system_menu_item(menu, 0U, MF_SEPARATOR, "-");
        win32_system_menu_item(menu, SC_CLOSE, MF_STRING, "Close");

        win_system_menus[window_index] =
            (void *)(uintptr_t)(MENU_BASE + i);
        return win_system_menus[window_index];
    }

    return NULL;
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
static uint32_t WIN32_API win32_GetMenuItemID(void*handle,int position){win_menu_t*m=menu_from_handle(handle);if(!m||position<0||(uint32_t)position>=m->count)return 0xFFFFFFFFU;return m->items[position].submenu?0xFFFFFFFFU:m->items[position].id;}
static uint32_t WIN32_API win32_GetMenuState(void*handle,uint32_t item,uint32_t flags){win_menu_t*m=menu_from_handle(handle);win_menu_item_t*e=menu_find_item(m,item,flags);win_menu_t*sub;if(!e)return 0xFFFFFFFFU;sub=menu_from_handle(e->submenu);return e->flags|(sub?((uint32_t)sub->count<<8):0U);}
static int WIN32_API win32_GetMenuStringA(void*handle,uint32_t item,char*out,int size,uint32_t flags){win_menu_item_t*e=menu_find_item(menu_from_handle(handle),item,flags);int length;if(!e)return 0;length=(int)kstrlen(e->text);if(out&&size>0){kstrncpy(out,e->text,(size_t)size-1U);out[size-1]=0;return(int)kstrlen(out);}return length;}
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
        case 0x0084U: return "SCROLLBAR";
        case 0x0085U: return "COMBOBOX";
        default: return "STATIC";
    }
}

static int dialog_muldiv(int value, int numerator, int denominator) {
    int64_t product = (int64_t)value * (int64_t)numerator;
    if (!denominator) return 0;
    if (product >= 0) product += denominator / 2;
    else product -= denominator / 2;
    return (int)(product / denominator);
}
static bool dialog_contains_ci(const char *text, const char *needle) {
    if (!text || !needle) return false;
    while (*text) {
        const char *a = text, *b = needle;
        while (*a && *b && upper_ascii((uint8_t)*a) == upper_ascii((uint8_t)*b)) { a++; b++; }
        if (!*b) return true;
        text++;
    }
    return false;
}
static void dialog_font_units(uint16_t point_size, const char *face,
                              uint16_t weight, bool italic,
                              int *base_x, int *base_y, int *pixel_height,
                              bool *bold, bool *font_italic,
                              bool *monospace) {
    int h = point_size ? dialog_muldiv((int)point_size, 96, 72) : 8;
    bool m = face && (dialog_contains_ci(face, "COURIER") ||
                      dialog_contains_ci(face, "TERMINAL") ||
                      dialog_contains_ci(face, "FIXEDSYS"));
    char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    int b = weight >= 600U;
    int x;
    if (h < 8) h = 8;
    if (h > 32) h = 32;
    x = (int)gui_font_text_width_px(alphabet, 52U, h, m, b) / 52;
    if (x < 4) x = 4;
    *base_x = x;
    /* tmHeight used for drawing excludes the external leading that Windows
       includes in vertical dialog units. */
    *base_y = h + (h <= 16 ? 2 : 1);
    *pixel_height = h; *bold = b != 0;
    *font_italic = italic; *monospace = m;
}
static void *create_dialog_from_template(void *instance, const uint8_t *data,
                                         uint32_t size, void *parent,
                                         wndproc_t dialog_proc,
                                         int32_t init_param) {
    dialog_reader_t reader = { data, data + size };
    uint32_t style, exstyle, help_id = 0U;
    uint16_t item_count, point_size = 0U, weight = 400U;
    uint16_t dlgver = 0U, signature = 0U, italic_charset = 0U;
    int16_t x, y, cx, cy;
    bool extended = false, italic = false;
    dialog_field_t menu_field, class_field, title_field, font_name;
    wndclass_a_t saved_class = registered_class;
    char saved_name[sizeof(registered_name)];
    uint32_t saved_class_process_id = registered_class_process_id;
    void *hwnd = NULL;
    int base_x = 8, base_y = 16, font_height = 8;
    bool font_bold = false, font_italic = false, font_monospace = false;

    if (!data || size < 18U || !dialog_proc) return NULL;
    kstrncpy(saved_name, registered_name, sizeof(saved_name) - 1U);
    saved_name[sizeof(saved_name) - 1U] = '\0';
    kmemset(&font_name, 0, sizeof(font_name));

    if (size >= 4U) {
        dlgver = (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
        signature = (uint16_t)(data[2] | ((uint16_t)data[3] << 8));
        extended = dlgver == 1U && signature == 0xFFFFU;
    }
    if (extended) {
        if (!dialog_read_u16(&reader, &dlgver) ||
            !dialog_read_u16(&reader, &signature) ||
            !dialog_read_u32(&reader, &help_id) ||
            !dialog_read_u32(&reader, &exstyle) ||
            !dialog_read_u32(&reader, &style) ||
            !dialog_read_u16(&reader, &item_count) ||
            !dialog_read_s16(&reader, &x) || !dialog_read_s16(&reader, &y) ||
            !dialog_read_s16(&reader, &cx) || !dialog_read_s16(&reader, &cy))
            return NULL;
    } else {
        if (!dialog_read_u32(&reader, &style) ||
            !dialog_read_u32(&reader, &exstyle) ||
            !dialog_read_u16(&reader, &item_count) ||
            !dialog_read_s16(&reader, &x) || !dialog_read_s16(&reader, &y) ||
            !dialog_read_s16(&reader, &cx) || !dialog_read_s16(&reader, &cy))
            return NULL;
    }
    if (!dialog_read_field(&reader, &menu_field) ||
        !dialog_read_field(&reader, &class_field) ||
        !dialog_read_field(&reader, &title_field)) return NULL;
    if (style & DS_SETFONT) {
        if (!dialog_read_u16(&reader, &point_size)) return NULL;
        if (extended) {
            if (!dialog_read_u16(&reader, &weight) ||
                !dialog_read_u16(&reader, &italic_charset)) return NULL;
            italic = (italic_charset & 0x00FFU) != 0U;
        }
        if (!dialog_read_field(&reader, &font_name)) return NULL;
        dialog_font_units(point_size,
            font_name.kind == 2U ? font_name.text : "System",
            weight, italic, &base_x, &base_y, &font_height,
            &font_bold, &font_italic, &font_monospace);
    }

    kmemset(&registered_class, 0, sizeof(registered_class));
    registered_class.proc = dialog_proc;
    registered_class_process_id = task_current_process_id();
    kstrncpy(registered_name, "#32770", sizeof(registered_name) - 1U);
    registered_class.name = registered_name;
    int px = (uint16_t)x == 0x8000U ? 80 : dialog_muldiv(x, base_x, 4);
    int py = (uint16_t)y == 0x8000U ? 60 : dialog_muldiv(y, base_y, 8);
    int width = dialog_muldiv(cx, base_x, 4);
    int height = dialog_muldiv(cy, base_y, 8);
    if (style & DS_CENTER) {
        gui_desktop_t *desktop = gui_get_desktop();
        int dw = desktop ? desktop->surface.width : 800;
        int dh = desktop ? desktop->surface.height : 600;
        px = (dw - width) / 2; py = (dh - height) / 2;
    }
    hwnd = win32_CreateWindowExA(exstyle, "#32770", title_field.text,
                                 style & ~WS_VISIBLE, px, py, width, height,
                                 parent, NULL, instance,
                                 (void *)(uintptr_t)init_param);
    registered_class = saved_class;
    registered_class_process_id = saved_class_process_id;
    kstrncpy(registered_name, saved_name, sizeof(registered_name) - 1U);
    registered_class.name = registered_name;
    if (!hwnd) return NULL;

    win_window_t *dialog = window_from_handle(hwnd);
    dialog->dialog = true; dialog->proc = dialog_proc; dialog->parent = parent;
    dialog->dialog_base_x = (int16_t)base_x;
    dialog->dialog_base_y = (int16_t)base_y;
    dialog->font_pixel_height = (int16_t)font_height;
    dialog->font_bold = font_bold; dialog->font_italic = font_italic;
    dialog->font_monospace = font_monospace;
    if (style & DS_SETFONT)
        dialog->font = win32_gdi_create_font_internal(font_height, weight,
            font_italic, font_monospace,
            font_name.kind == 2U ? font_name.text : "System");
    (void)help_id;
    if (menu_field.kind) {
        void *menu = menu_field.kind == 1U
            ? win32_LoadMenuA(instance, (const char *)(uintptr_t)menu_field.ordinal)
            : win32_LoadMenuA(instance, menu_field.text);
        if (menu) win32_SetMenu(hwnd, menu);
    }

    void *first_control = NULL, *first_any_control = NULL;
    for (uint16_t item = 0; item < item_count; item++) {
        uint32_t item_style, item_exstyle, item_help = 0U, id32;
        int16_t ix, iy, icx, icy; uint16_t id16, extra;
        dialog_field_t item_class, item_title;
        if (!dialog_align4(&reader)) { cleanup_window(hwnd); return NULL; }
        if (extended) {
            if (!dialog_read_u32(&reader, &item_help) ||
                !dialog_read_u32(&reader, &item_exstyle) ||
                !dialog_read_u32(&reader, &item_style) ||
                !dialog_read_s16(&reader, &ix) || !dialog_read_s16(&reader, &iy) ||
                !dialog_read_s16(&reader, &icx) || !dialog_read_s16(&reader, &icy) ||
                !dialog_read_u32(&reader, &id32)) { cleanup_window(hwnd); return NULL; }
        } else {
            if (!dialog_read_u32(&reader, &item_style) ||
                !dialog_read_u32(&reader, &item_exstyle) ||
                !dialog_read_s16(&reader, &ix) || !dialog_read_s16(&reader, &iy) ||
                !dialog_read_s16(&reader, &icx) || !dialog_read_s16(&reader, &icy) ||
                !dialog_read_u16(&reader, &id16)) { cleanup_window(hwnd); return NULL; }
            id32 = id16;
        }
        if (!dialog_read_field(&reader, &item_class) ||
            !dialog_read_field(&reader, &item_title) ||
            !dialog_read_u16(&reader, &extra)) { cleanup_window(hwnd); return NULL; }
        if ((uint32_t)(reader.end - reader.cursor) < extra) { cleanup_window(hwnd); return NULL; }
        reader.cursor += extra; (void)item_help;
        const char *control_class = dialog_class_name(&item_class);
        if (!equal(control_class, "BUTTON") && !equal(control_class, "EDIT") &&
            !equal(control_class, "STATIC") && !equal(control_class, "LISTBOX") &&
            !equal(control_class, "SCROLLBAR") && !equal(control_class, "COMBOBOX"))
            control_class = "STATIC";
        item_style = (item_style & ~WS_POPUP) | WS_CHILD;
        void *control = win32_CreateWindowExA(item_exstyle, control_class,
            item_title.kind == 2U ? item_title.text : "", item_style,
            dialog_muldiv(ix, base_x, 4), dialog_muldiv(iy, base_y, 8),
            dialog_muldiv(icx, base_x, 4), dialog_muldiv(icy, base_y, 8),
            hwnd, (void *)(uintptr_t)id32, instance, NULL);
        if (control) {
            win_window_t *cw = window_from_handle(control);
            cw->font = dialog->font;
            cw->font_pixel_height = (int16_t)font_height;
            cw->font_bold = font_bold; cw->font_italic = font_italic;
            cw->font_monospace = font_monospace;
            if (item_title.kind == 1U && equal(control_class, "STATIC") &&
                (item_style & SS_TYPEMASK) == SS_ICON)
                cw->large_icon = win32_icon_load(instance,
                    (const void *)(uintptr_t)item_title.ordinal, false,
                    cw->bounds.w, cw->bounds.h);
            if (equal(control_class, "STATIC") &&
                (item_style & SS_TYPEMASK) == SS_BITMAP) {
                const void *bitmap_name = item_title.kind == 1U
                    ? (const void *)(uintptr_t)item_title.ordinal
                    : (const void *)item_title.text;
                void *bitmap_resource = win32_resource_find(instance,
                    (const void *)(uintptr_t)WIN32_RT_BITMAP,
                    bitmap_name, 0U, false);
                cw->large_icon =
                    win32_gdi_bitmap_from_resource(bitmap_resource);
            }
            if (!first_any_control) first_any_control = control;
            if (!first_control && (item_style & WS_TABSTOP)) first_control = control;
        }
    }
    if (!first_control) first_control = first_any_control;
    if (first_control) win32_SetFocus(first_control);
    (void)win32_call_or_queue_wndproc(dialog_proc, hwnd, WM_INITDIALOG,
        (uint32_t)(uintptr_t)first_control, init_param, NULL, 0U, -1);
    if (style & WS_VISIBLE) { win32_ShowWindow(hwnd, SW_SHOWNORMAL); win32_UpdateWindow(hwnd); }
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

static void *WIN32_API win32_CreateDialogIndirectParamA(
    void *instance, const void *template_data, void *parent, void *proc,
    int32_t init_param);
static void *WIN32_API win32_CreateDialogIndirectParamW(
    void *instance, const void *template_data, void *parent, void *proc,
    int32_t init_param);

/* BLES_WINE_DIALOG_RING3_FIX_20260723
 *
 * DialogBox* cannot own its modal GetMessage/DispatchMessage loop inside a
 * SYS_API_CALL.  DispatchMessage queues PE WndProcs as Ring-3 upcalls, and an
 * upcall can only be delivered when the syscall returns to a real CPL3 frame.
 * The small user-mode thunk built by elf_loader.c calls these one-step helpers
 * repeatedly, returning through int 0x80 after every dispatched message. */
static void win32_dialog_prepare_modal(void *hwnd, void *owner) {
    win_window_t *dialog = window_from_handle(hwnd);
    win_window_t *owner_window = window_from_handle(owner);
    if (!dialog || !dialog->dialog) return;
    dialog->dialog_modal = true;
    dialog->parent = owner;
    dialog->dialog_owner_was_enabled = owner_window && owner_window->enabled;
    if (dialog->dialog_owner_was_enabled) (void)win32_EnableWindow(owner, 0);
    if (dialog->native) {
        gui_desktop_t *desktop = gui_get_desktop();
        dialog->native->input_enabled = true;
        if (desktop) {
            dialog->native->bounds.x =
                ((int)desktop->surface.width -
                 dialog->native->bounds.w) / 2;
            dialog->native->bounds.y =
                ((int)desktop->surface.height - 28 -
                 dialog->native->bounds.h) / 2;
            if (dialog->native->bounds.x < 0)
                dialog->native->bounds.x = 0;
            if (dialog->native->bounds.y < 0)
                dialog->native->bounds.y = 0;
        }
        gui_desktop_raise_window(desktop, dialog->native);
        gui_desktop_focus_window(desktop, dialog->native);
    }
}

void *WIN32_API win32_user_dialog_begin_param_a(
    void *instance, const char *template_name, void *parent, void *proc,
    int32_t init_param) {
    void *hwnd = win32_CreateDialogParamA(instance, template_name, parent,
                                           proc, init_param);
    if (hwnd) win32_dialog_prepare_modal(hwnd, parent);
    return hwnd;
}

void *WIN32_API win32_user_dialog_begin_param_w(
    void *instance, const uint16_t *template_name, void *parent, void *proc,
    int32_t init_param) {
    void *hwnd = win32_CreateDialogParamW(instance, template_name, parent,
                                           proc, init_param);
    if (hwnd) win32_dialog_prepare_modal(hwnd, parent);
    return hwnd;
}

void *WIN32_API win32_user_dialog_begin_indirect_a(
    void *instance, const void *template_data, void *parent, void *proc,
    int32_t init_param) {
    void *hwnd = win32_CreateDialogIndirectParamA(instance, template_data,
                                                   parent, proc, init_param);
    if (hwnd) win32_dialog_prepare_modal(hwnd, parent);
    return hwnd;
}

void *WIN32_API win32_user_dialog_begin_indirect_w(
    void *instance, const void *template_data, void *parent, void *proc,
    int32_t init_param) {
    void *hwnd = win32_CreateDialogIndirectParamW(instance, template_data,
                                                   parent, proc, init_param);
    if (hwnd) win32_dialog_prepare_modal(hwnd, parent);
    return hwnd;
}

int WIN32_API win32_user_dialog_modal_step(
    void *hwnd, win32_user_dialog_plan_t *plan) {
    win_window_t *dialog = window_from_handle(hwnd);
    winmsg_t message;
    int message_result;

    if (plan) {
        plan->invoke = 0U;
        plan->proc = 0U;
        plan->hwnd = 0U;
        plan->message = 0U;
        plan->wparam = 0U;
        plan->lparam = 0;
    }
    if (!dialog || !dialog->dialog) {
        if (plan) plan->result = WIN32_IDCANCEL;
        return 1;
    }

    /* DestroyWindow may close a modal dialog without calling EndDialog.  Once
     * its queued WM_DESTROY callback has returned, finish the modal contract. */
    if (!dialog->dialog_done && dialog->destroy_sent &&
        (!dialog->native || !dialog->native->visible)) {
        dialog->dialog_result = WIN32_IDCANCEL;
        dialog->dialog_done = true;
    }
    if (task_exit_requested() && !dialog->dialog_done) {
        dialog->dialog_result = WIN32_IDCANCEL;
        dialog->dialog_done = true;
    }

    if (dialog->dialog_done) {
        int32_t result = dialog->dialog_result;
        void *owner = dialog->parent;
        bool reenable_owner = dialog->dialog_owner_was_enabled;

        /* EndDialog does not destroy synchronously. Queue WM_DESTROY, return
         * to Ring 3 so the dialog procedure can run, then clean up next step. */
        if (!dialog->destroy_sent) {
            bool deferred = dialog->proc && win32_wndproc_is_pe(dialog->proc);
            int32_t dispatched;
            dialog->destroy_sent = true;
            dispatched = dialog->proc ? win32_call_or_queue_wndproc(
                dialog->proc, hwnd, WM_DESTROY, 0U, 0, NULL, 0U, -1) : 0;
            if (deferred && dispatched == 0) return 0;
        }

        cleanup_window(hwnd);
        if (reenable_owner && window_from_handle(owner)) {
            (void)win32_EnableWindow(owner, 1);
            (void)win32_SetForegroundWindow(owner);
        }
        if (plan) plan->result = result;
        return 1;
    }

    message_result = win32_GetMessageA(&message, NULL, 0U, 0U);
    if (message_result < 0) {
        (void)win32_EndDialog(hwnd, WIN32_IDCANCEL);
        return 0;
    }
    if (message_result == 0) {
        /* Like Wine's modal loop, preserve WM_QUIT for the outer loop. */
        win32_PostQuitMessage((int)message.wparam);
        (void)win32_EndDialog(hwnd, WIN32_IDCANCEL);
        return 0;
    }
    if (!win32_IsDialogMessageA(hwnd, &message)) {
        (void)win32_TranslateMessage(&message);
        {
            win_window_t *target = window_from_handle(message.hwnd);
            if (plan && target && target->proc &&
                win32_wndproc_is_pe(target->proc)) {
                /*
                 * A nested DialogBox can be opened from an existing Ring-3
                 * WndProc. Queuing another upcall to the same task deadlocks:
                 * the outer callback cannot return until this modal loop
                 * finishes. Let the Ring-3 dialog thunk call the target
                 * procedure directly before its next modal-step syscall.
                 */
                plan->invoke = 1U;
                plan->proc = (uint32_t)(uintptr_t)target->proc;
                plan->hwnd = (uint32_t)(uintptr_t)message.hwnd;
                plan->message = message.message;
                plan->wparam = message.wparam;
                plan->lparam = (int32_t)message.lparam;
                return 2;
            }
            (void)win32_DispatchMessageA(&message);
        }
    }
    return 0;
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
        if (dialog->proc) (void)win32_call_or_queue_wndproc(
            dialog->proc, hwnd, WM_DESTROY, 0U, 0, NULL, 0U, -1);
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

/*
 * DialogBoxIndirectParam receives an already locked DLGTEMPLATE rather than a
 * resource name.  Old self extractors (notably WinZip/Winamp) build or lock
 * this template themselves and use this entry point for their main window.
 * DLGTEMPLATE does not carry its total byte size, so give the bounded parser a
 * conservative maximum; it stops after the declared number of controls.
 */
#define WIN32_MAX_DIALOG_TEMPLATE_SIZE 65536U

static int32_t WIN32_API win32_DialogBoxIndirectParamA(
    void *instance, const void *template_data, void *parent, void *proc,
    int32_t init_param) {
    void *hwnd = create_dialog_from_template(
        instance, (const uint8_t *)template_data,
        WIN32_MAX_DIALOG_TEMPLATE_SIZE, parent, (wndproc_t)proc, init_param);
    return hwnd ? dialog_box_common(hwnd) : -1;
}

static int32_t WIN32_API win32_DialogBoxIndirectParamW(
    void *instance, const void *template_data, void *parent, void *proc,
    int32_t init_param) {
    return win32_DialogBoxIndirectParamA(instance, template_data, parent, proc,
                                         init_param);
}

static void *WIN32_API win32_CreateDialogIndirectParamA(
    void *instance, const void *template_data, void *parent, void *proc,
    int32_t init_param) {
    return create_dialog_from_template(
        instance, (const uint8_t *)template_data,
        WIN32_MAX_DIALOG_TEMPLATE_SIZE, parent, (wndproc_t)proc, init_param);
}

static void *WIN32_API win32_CreateDialogIndirectParamW(
    void *instance, const void *template_data, void *parent, void *proc,
    int32_t init_param) {
    return win32_CreateDialogIndirectParamA(instance, template_data, parent,
                                             proc, init_param);
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
static void box_button(gui_window_t *window, uint32_t id) {
    win32_message_box_t *box = window ? (win32_message_box_t *)window->content_context : NULL;
    if (box) box->result = (int)id;
    if (window) gui_window_close(window);
}
static int WIN32_API win32_MessageBoxA(void *owner UNUSED, const char *text,
                                       const char *caption, uint32_t type) {
    typedef struct { const char *text; uint32_t result; } box_button_t;
    static const box_button_t ok[] = {{"OK", WIN32_IDOK}};
    static const box_button_t ok_cancel[] = {
        {"OK", WIN32_IDOK}, {"Cancel", WIN32_IDCANCEL}};
    static const box_button_t abort_retry_ignore[] = {
        {"Abort", WIN32_IDABORT}, {"Retry", WIN32_IDRETRY},
        {"Ignore", WIN32_IDIGNORE}};
    static const box_button_t yes_no_cancel[] = {
        {"Yes", WIN32_IDYES}, {"No", WIN32_IDNO},
        {"Cancel", WIN32_IDCANCEL}};
    static const box_button_t yes_no[] = {
        {"Yes", WIN32_IDYES}, {"No", WIN32_IDNO}};
    static const box_button_t retry_cancel[] = {
        {"Retry", WIN32_IDRETRY}, {"Cancel", WIN32_IDCANCEL}};
    static const box_button_t cancel_try_continue[] = {
        {"Cancel", WIN32_IDCANCEL}, {"Try Again", WIN32_IDTRYAGAIN},
        {"Continue", WIN32_IDCONTINUE}};
    const box_button_t *buttons = ok;
    uint32_t button_count = 1U;
    win32_message_box_t box;
    gui_desktop_t *desktop = gui_get_desktop();
    int width = 360, height = 145, x, y;
    switch (type & 0x0FU) {
        case 1U: buttons = ok_cancel; button_count = 2U; break;
        case 2U: buttons = abort_retry_ignore; button_count = 3U; break;
        case 3U: buttons = yes_no_cancel; button_count = 3U; break;
        case 4U: buttons = yes_no; button_count = 2U; break;
        case 5U: buttons = retry_cancel; button_count = 2U; break;
        case 6U: buttons = cancel_try_continue; button_count = 3U; break;
        default: break;
    }
    if (!desktop) return 0;
    x = ((int)desktop->surface.width - width) / 2;
    y = ((int)desktop->surface.height - height) / 2;
    box.window = gui_desktop_create_window(desktop, x < 0 ? 0 : x, y < 0 ? 0 : y,
                                            width, height, caption ? caption : "Message");
    if (!box.window) return 0;
    box.text = text ? text : ""; box.result = 0;
    box.window->owner_pid = task_current_pid(); box.window->listed = false;
    gui_window_set_content(box.window, box_paint, &box);
    int buttons_width = (int)button_count * 76 + ((int)button_count - 1) * 8;
    int button_x = (width - buttons_width) / 2;
    for (uint32_t i = 0U; i < button_count; i++) {
        gui_widget_t *button = gui_widget_create_button(desktop, box.window,
            (gui_rect_t){button_x + (int)i * 84, 78, 76, 26},
            buttons[i].text, box_button);
        if (!button) {
            gui_desktop_remove_window(desktop, box.window);
            gui_window_destroy(box.window);
            return 0;
        }
        button->id = buttons[i].result;
    }
    gui_desktop_focus_window(desktop, box.window); gui_request_paint();
    while (box.window->visible && !task_exit_requested()) task_sleep(1U);
    if (!box.result) box.result = button_count == 1U
        ? (int)buttons[0].result : WIN32_IDCANCEL;
    gui_desktop_remove_window(desktop, box.window); gui_window_destroy(box.window); gui_request_paint();
    return box.result;
}

int WIN32_API win32_user_message_box_a(void *owner, const char *text,
                                        const char *caption, uint32_t type) {
    return win32_MessageBoxA(owner, text, caption, type);
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
        /* A PE dialog procedure must run through the Ring 3 upcall bridge.
         * Creation remains optimistic until synchronous callback results are
         * implemented, matching CreateWindowExA above. */
        if (win32_call_or_queue_wndproc(proc, hwnd, WM_NCCREATE, 0U, 0,
                                        NULL, 0U, -1) < 0 ||
            win32_call_or_queue_wndproc(proc, hwnd, WM_CREATE, 0U, 0,
                                        NULL, 0U, -1) < 0) {
            cleanup_window(hwnd);
            return NULL;
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

typedef struct {
    volatile bool done;
    bool accepted;
    char *buffer;
    uint32_t capacity;
} win_file_dialog_wait_t;

static void win32_file_dialog_complete(const char *path, void *context) {
    win_file_dialog_wait_t *wait = (win_file_dialog_wait_t *)context;
    if (!wait) return;
    wait->accepted = path && path[0];
    if (wait->accepted && wait->buffer && wait->capacity) {
        kstrncpy(wait->buffer, path, wait->capacity - 1U);
        wait->buffer[wait->capacity - 1U] = '\0';
    }
    wait->done = true;
}

bool win32_user_file_dialog(const char *title, const char *initial_dir,
                            const char *extension, char *buffer,
                            uint32_t capacity) {
    win_file_dialog_wait_t wait;
    if (!buffer || capacity < 2U || !gui_get_desktop()) return false;
    kmemset(&wait, 0, sizeof(wait));
    wait.buffer = buffer;
    wait.capacity = capacity;
    if (!bk_file_dialog_open_kernel(gui_get_desktop(), title, initial_dir,
                                    extension, 0U,
                                    win32_file_dialog_complete, &wait))
        return false;
    while (!wait.done && !task_exit_requested()) task_sleep(1U);
    return wait.accepted;
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
/* Wine converts these APIs through Unicode, between the active ANSI and
 * OEM code pages. BlesKernOS currently reports Windows-1252 and CP437, while
 * its generic MultiByteToWideChar path is still byte-preserving. Keep the
 * correct conversion local until the NLS layer grows full code-page tables. */
static const uint16_t win32_cp437_high[128] = {
    0x00C7U, 0x00FCU, 0x00E9U, 0x00E2U, 0x00E4U, 0x00E0U, 0x00E5U, 0x00E7U,
    0x00EAU, 0x00EBU, 0x00E8U, 0x00EFU, 0x00EEU, 0x00ECU, 0x00C4U, 0x00C5U,
    0x00C9U, 0x00E6U, 0x00C6U, 0x00F4U, 0x00F6U, 0x00F2U, 0x00FBU, 0x00F9U,
    0x00FFU, 0x00D6U, 0x00DCU, 0x00A2U, 0x00A3U, 0x00A5U, 0x20A7U, 0x0192U,
    0x00E1U, 0x00EDU, 0x00F3U, 0x00FAU, 0x00F1U, 0x00D1U, 0x00AAU, 0x00BAU,
    0x00BFU, 0x2310U, 0x00ACU, 0x00BDU, 0x00BCU, 0x00A1U, 0x00ABU, 0x00BBU,
    0x2591U, 0x2592U, 0x2593U, 0x2502U, 0x2524U, 0x2561U, 0x2562U, 0x2556U,
    0x2555U, 0x2563U, 0x2551U, 0x2557U, 0x255DU, 0x255CU, 0x255BU, 0x2510U,
    0x2514U, 0x2534U, 0x252CU, 0x251CU, 0x2500U, 0x253CU, 0x255EU, 0x255FU,
    0x255AU, 0x2554U, 0x2569U, 0x2566U, 0x2560U, 0x2550U, 0x256CU, 0x2567U,
    0x2568U, 0x2564U, 0x2565U, 0x2559U, 0x2558U, 0x2552U, 0x2553U, 0x256BU,
    0x256AU, 0x2518U, 0x250CU, 0x2588U, 0x2584U, 0x258CU, 0x2590U, 0x2580U,
    0x03B1U, 0x00DFU, 0x0393U, 0x03C0U, 0x03A3U, 0x03C3U, 0x00B5U, 0x03C4U,
    0x03A6U, 0x0398U, 0x03A9U, 0x03B4U, 0x221EU, 0x03C6U, 0x03B5U, 0x2229U,
    0x2261U, 0x00B1U, 0x2265U, 0x2264U, 0x2320U, 0x2321U, 0x00F7U, 0x2248U,
    0x00B0U, 0x2219U, 0x00B7U, 0x221AU, 0x207FU, 0x00B2U, 0x25A0U, 0x00A0U
};

static const uint16_t win32_cp1252_special[32] = {
    0x20ACU, 0x0081U, 0x201AU, 0x0192U, 0x201EU, 0x2026U, 0x2020U, 0x2021U,
    0x02C6U, 0x2030U, 0x0160U, 0x2039U, 0x0152U, 0x008DU, 0x017DU, 0x008FU,
    0x0090U, 0x2018U, 0x2019U, 0x201CU, 0x201DU, 0x2022U, 0x2013U, 0x2014U,
    0x02DCU, 0x2122U, 0x0161U, 0x203AU, 0x0153U, 0x009DU, 0x017EU, 0x0178U
};

static uint16_t win32_cp437_to_unicode(uint8_t value) {
    return value < 0x80U ? value : win32_cp437_high[value - 0x80U];
}
static uint16_t win32_cp1252_to_unicode(uint8_t value) {
    if (value < 0x80U || value >= 0xA0U) return value;
    return win32_cp1252_special[value - 0x80U];
}
static uint8_t win32_unicode_to_cp437(uint16_t value) {
    if (value < 0x80U) return (uint8_t)value;
    for (uint32_t i = 0; i < 128U; i++)
        if (win32_cp437_high[i] == value) return (uint8_t)(i + 0x80U);
    return (uint8_t)'?';
}
static uint8_t win32_unicode_to_cp1252(uint16_t value) {
    if (value < 0x80U || (value >= 0xA0U && value <= 0x00FFU))
        return (uint8_t)value;
    for (uint32_t i = 0; i < 32U; i++)
        if (win32_cp1252_special[i] == value) return (uint8_t)(i + 0x80U);
    return (uint8_t)'?';
}

static int WIN32_API win32_CharToOemBuffA(const char *src, char *dst,
                                           uint32_t length) {
    if (!src || !dst) return 0;
    for (uint32_t i = 0; i < length; i++)
        dst[i] = (char)win32_unicode_to_cp437(
            win32_cp1252_to_unicode((uint8_t)src[i]));
    return 1;
}
static int WIN32_API win32_OemToCharBuffA(const char *src, char *dst,
                                           uint32_t length) {
    if (!src || !dst) return 0;
    for (uint32_t i = 0; i < length; i++)
        dst[i] = (char)win32_unicode_to_cp1252(
            win32_cp437_to_unicode((uint8_t)src[i]));
    return 1;
}
static int WIN32_API win32_CharToOemA(const char *src, char *dst) {
    if (!src || !dst) return 0;
    return win32_CharToOemBuffA(src, dst, (uint32_t)kstrlen(src) + 1U);
}
static int WIN32_API win32_OemToCharA(const char *src, char *dst) {
    if (!src || !dst) return 0;
    return win32_OemToCharBuffA(src, dst, (uint32_t)kstrlen(src) + 1U);
}
static int WIN32_API win32_CharToOemBuffW(const uint16_t *src, char *dst,
                                           uint32_t length) {
    if (!src || !dst) return 0;
    for (uint32_t i = 0; i < length; i++)
        dst[i] = (char)win32_unicode_to_cp437(src[i]);
    return 1;
}
static int WIN32_API win32_OemToCharBuffW(const char *src, uint16_t *dst,
                                           uint32_t length) {
    if (!src || !dst) return 0;
    for (uint32_t i = 0; i < length; i++)
        dst[i] = win32_cp437_to_unicode((uint8_t)src[i]);
    return 1;
}
static int WIN32_API win32_CharToOemW(const uint16_t *src, char *dst) {
    uint32_t length = 0U;
    if (!src || !dst) return 0;
    while (src[length]) length++;
    return win32_CharToOemBuffW(src, dst, length + 1U);
}
static int WIN32_API win32_OemToCharW(const char *src, uint16_t *dst) {
    if (!src || !dst) return 0;
    return win32_OemToCharBuffW(src, dst, (uint32_t)kstrlen(src) + 1U);
}
static int WIN32_API win32_wsprintfA(char *out,const char *format,...) {
    va_list args; int result;
    if(!out||!format)return 0;
    va_start(args,format); result=vsnprintf(out,1024U,format,args); va_end(args);
    return result;
}

/* WIN32_USER32_WVSPRINTFA */
static int WIN32_API win32_wvsprintfA(char *out,
                                      const char *format,
                                      va_list arguments) {
    if (!out || !format) return 0;
    return vsnprintf(out, 1024U, format, arguments);
}

/* WIN32_USER32_TABBEDTEXTOUTA */
typedef int (WIN32_API *win32_text_out_a_t)(
    void *dc, int x, int y, const char *text, int length);

static int win32_tab_segment_width(const char *text, int length) {
    int total = 0;
    int offset = 0;

    while (offset < length) {
        char copy[128];
        int chunk = length - offset;

        if (chunk > (int)sizeof(copy) - 1)
            chunk = (int)sizeof(copy) - 1;

        kmemcpy(copy, text + offset, (size_t)chunk);
        copy[chunk] = '\0';
        total += (int)gui_font_text_width(copy);
        offset += chunk;
    }

    return total;
}

static int win32_next_tab_x(int current_x,
                            int tab_origin,
                            int tab_count,
                            const int32_t *tabs) {
    int relative = current_x - tab_origin;
    int default_interval = 64;

    if (relative < 0) relative = 0;

    if (tabs && tab_count == 1 && tabs[0] > 0) {
        int interval = tabs[0];
        return tab_origin + ((relative / interval) + 1) * interval;
    }

    if (tabs && tab_count > 1) {
        for (int i = 0; i < tab_count; i++) {
            if (tabs[i] > relative)
                return tab_origin + tabs[i];
        }
    }

    return tab_origin +
        ((relative / default_interval) + 1) * default_interval;
}

static uint32_t win32_tabbed_text_common(
        void *dc,
        int x,
        int y,
        const char *text,
        int length,
        int tab_count,
        const int32_t *tabs,
        int tab_origin,
        bool draw) {
    win32_text_out_a_t text_out = NULL;
    int current_x = x;
    int segment = 0;

    if (!text) return 0U;
    if (length < 0) length = (int)kstrlen(text);

    if (draw) {
        text_out = (win32_text_out_a_t)(uintptr_t)
            pe_win32_resolve_export("GDI32.DLL", "TextOutA");
        if (!text_out) return 0U;
    }

    for (int i = 0; i <= length; i++) {
        bool at_end = i == length;
        bool at_tab = !at_end && text[i] == '\t';

        if (!at_end && !at_tab) continue;

        if (i > segment) {
            int part_length = i - segment;
            int offset = 0;

            if (draw) {
                while (offset < part_length) {
                    int chunk = part_length - offset;
                    if (chunk > 94) chunk = 94;

                    if (!text_out(dc, current_x, y,
                                  text + segment + offset, chunk))
                        return 0U;

                    current_x += win32_tab_segment_width(
                        text + segment + offset, chunk);
                    offset += chunk;
                }
            } else {
                current_x += win32_tab_segment_width(
                    text + segment, part_length);
            }
        }

        if (at_tab) {
            current_x = win32_next_tab_x(
                current_x, tab_origin, tab_count, tabs);
            segment = i + 1;
        }
    }

    {
        uint32_t width = current_x > x
            ? (uint32_t)(current_x - x) : 0U;
        uint32_t height = 12U;
        if (width > 0xFFFFU) width = 0xFFFFU;
        return (height << 16) | width;
    }
}

static uint32_t WIN32_API win32_TabbedTextOutA(
        void *dc,
        int x,
        int y,
        const char *text,
        int length,
        int tab_count,
        const int32_t *tabs,
        int tab_origin) {
    return win32_tabbed_text_common(
        dc, x, y, text, length, tab_count,
        tabs, tab_origin, true);
}

static uint32_t WIN32_API win32_GetTabbedTextExtentA(
        void *dc,
        const char *text,
        int length,
        int tab_count,
        const int32_t *tabs) {
    return win32_tabbed_text_common(
        dc, 0, 0, text, length, tab_count,
        tabs, 0, false);
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
static wndclass_a_t *win32_class_for_window(void *hwnd) {
    win_window_t *window = window_from_handle(hwnd);
    win_class_t *entry = window ? win32_find_class(window->class_name) : NULL;
    return entry ? &entry->definition : &registered_class;
}
static int32_t WIN32_API win32_GetClassLongA(void *hwnd,int index) {
    wndclass_a_t *definition = win32_class_for_window(hwnd);
    switch(index){
        case GCL_STYLE:return(int32_t)definition->style;
        case GCL_WNDPROC:return(int32_t)(uintptr_t)definition->proc;
        case GCL_CBCLSEXTRA:return definition->cls_extra;
        case GCL_CBWNDEXTRA:return definition->win_extra;
        case GCL_HMODULE:return(int32_t)(uintptr_t)definition->instance;
        case GCL_HICON:return(int32_t)(uintptr_t)definition->icon;
        case GCL_HCURSOR:return(int32_t)(uintptr_t)definition->cursor;
        case GCL_HBRBACKGROUND:return(int32_t)(uintptr_t)definition->background;
        case GCL_MENUNAME:return(int32_t)(uintptr_t)definition->menu;
        default:return 0;
    }
}
static int32_t WIN32_API win32_SetClassLongA(void *hwnd,int index,int32_t value) {
    wndclass_a_t *definition=win32_class_for_window(hwnd);
    int32_t old=0;
    switch(index){
        case GCL_STYLE:old=(int32_t)definition->style;definition->style=(uint32_t)value;break;
        case GCL_WNDPROC:old=(int32_t)(uintptr_t)definition->proc;definition->proc=(wndproc_t)(uintptr_t)value;break;
        case GCL_CBCLSEXTRA:old=definition->cls_extra;definition->cls_extra=value;break;
        case GCL_CBWNDEXTRA:old=definition->win_extra;definition->win_extra=value;break;
        case GCL_HMODULE:old=(int32_t)(uintptr_t)definition->instance;definition->instance=(void*)(uintptr_t)value;break;
        case GCL_HICON:old=(int32_t)(uintptr_t)definition->icon;definition->icon=(void*)(uintptr_t)value;break;
        case GCL_HCURSOR:old=(int32_t)(uintptr_t)definition->cursor;definition->cursor=(void*)(uintptr_t)value;break;
        case GCL_HBRBACKGROUND:old=(int32_t)(uintptr_t)definition->background;definition->background=(void*)(uintptr_t)value;break;
        case GCL_MENUNAME:old=(int32_t)(uintptr_t)definition->menu;definition->menu=(const char*)(uintptr_t)value;break;
        default:return 0;
    }
    return old;
}
static int32_t WIN32_API win32_GetClassLongW(void *hwnd,int index){return win32_GetClassLongA(hwnd,index);}
static int32_t WIN32_API win32_SetClassLongW(void *hwnd,int index,int32_t value){return win32_SetClassLongA(hwnd,index,value);}
static uint16_t WIN32_API win32_GetWindowWord(void *hwnd, int index) {
    win_window_t *w = window_from_handle(hwnd);
    uint16_t value = 0U;
    if (!w) return 0U;
    if (index < 0) return (uint16_t)win32_GetWindowLongA(hwnd, index);
    uint32_t offset = (uint32_t)index;
    if (!w->window_extra || offset > w->window_extra_size ||
        2U > w->window_extra_size - offset) return 0U;
    kmemcpy(&value, w->window_extra + offset, sizeof(value));
    return value;
}
static uint16_t WIN32_API win32_SetWindowWord(void *hwnd, int index,
                                               uint16_t value) {
    win_window_t *w = window_from_handle(hwnd);
    uint16_t old = 0U;
    if (!w) return 0U;
    if (index < 0)
        return (uint16_t)win32_SetWindowLongA(hwnd, index, (int32_t)value);
    uint32_t offset = (uint32_t)index;
    if (!w->window_extra || offset > w->window_extra_size ||
        2U > w->window_extra_size - offset) return 0U;
    kmemcpy(&old, w->window_extra + offset, sizeof(old));
    kmemcpy(w->window_extra + offset, &value, sizeof(value));
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

static int WIN32_API win32_UnregisterClassW(const uint16_t *name,
                                             void *instance) {
    char ansi[64];
    return wide_to_ansi(name, ansi, sizeof(ansi))
        ? win32_UnregisterClassA(ansi, instance) : 0;
}

static int WIN32_API win32_GetClassInfoW(void *instance,
                                         const uint16_t *name,
                                         wndclass_w_t *out) {
    char ansi[64];
    wndclass_a_t value;
    if (!out || !wide_to_ansi(name, ansi, sizeof(ansi)) ||
        !win32_GetClassInfoA(instance, ansi, &value)) return 0;
    out->style=value.style;out->proc=value.proc;
    out->cls_extra=value.cls_extra;out->win_extra=value.win_extra;
    out->instance=value.instance;out->icon=value.icon;out->cursor=value.cursor;
    out->background=value.background;out->menu=NULL;out->name=name;
    return 1;
}

static int WIN32_API win32_GetClassInfoExW(void *instance,
                                           const uint16_t *name, void *raw) {
    uint8_t *bytes=(uint8_t*)raw;
    return bytes&&win32_GetClassInfoW(instance,name,(wndclass_w_t*)(bytes+4U));
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

static int win32_user_send_message_prepare_common(
    void *hwnd, uint32_t message, uint32_t wparam, int32_t lparam,
    win32_user_message_plan_t *plan, bool wide) {
    int32_t result;
    if (!plan) return 0;
    kmemset(plan, 0, sizeof(*plan));
    plan->hwnd = (uint32_t)(uintptr_t)hwnd;
    plan->message = message;
    plan->wparam = wparam;
    plan->lparam = lparam;

    /* Keep the capture pointer task-local in practice by preventing a context
     * switch for the very short kernel dispatch phase. */
    task_preempt_disable();
    g_sync_message_plan = plan;
    result = wide ? win32_SendMessageW(hwnd, message, wparam, lparam)
                  : win32_SendMessageA(hwnd, message, wparam, lparam);
    g_sync_message_plan = NULL;
    task_preempt_enable();
    plan->result = result;
    return plan->invoke ? 1 : 0;
}

int WIN32_API win32_user_send_message_prepare_a(
    void *hwnd, uint32_t message, uint32_t wparam, int32_t lparam,
    win32_user_message_plan_t *plan) {
    return win32_user_send_message_prepare_common(
        hwnd, message, wparam, lparam, plan, false);
}

int WIN32_API win32_user_send_message_prepare_w(
    void *hwnd, uint32_t message, uint32_t wparam, int32_t lparam,
    win32_user_message_plan_t *plan) {
    return win32_user_send_message_prepare_common(
        hwnd, message, wparam, lparam, plan, true);
}

int32_t WIN32_API win32_user_send_message_complete(
    win32_user_message_plan_t *plan, int32_t result) {
    win_window_t *window = plan ? window_from_handle(
        (void *)(uintptr_t)plan->hwnd) : NULL;
    if (window && window->native) {
        window->native->dirty = true;
        gui_desktop_invalidate_rect(gui_get_desktop(), window->native->bounds);
        gui_request_paint();
    }
    return result;
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

/* BLES_WINE_MESSAGEBOXINDIRECT_20260723 */
typedef struct {
    uint32_t cbSize;
    void *hwndOwner;
    void *hInstance;
    const char *lpszText;
    const char *lpszCaption;
    uint32_t dwStyle;
    const char *lpszIcon;
    uint32_t dwContextHelpId;
    void *lpfnMsgBoxCallback;
    uint32_t dwLanguageId;
} win32_msgbox_params_a_t;

typedef struct {
    uint32_t cbSize;
    void *hwndOwner;
    void *hInstance;
    const uint16_t *lpszText;
    const uint16_t *lpszCaption;
    uint32_t dwStyle;
    const uint16_t *lpszIcon;
    uint32_t dwContextHelpId;
    void *lpfnMsgBoxCallback;
    uint32_t dwLanguageId;
} win32_msgbox_params_w_t;

static int WIN32_API win32_MessageBoxIndirectA(
    const win32_msgbox_params_a_t *params) {
    if (!params || params->cbSize < sizeof(*params)) {
        pe_win32_set_last_error(87U);
        return 0;
    }
    /*
     * This entry is commonly used as an early capability/error notification
     * by Win9x applications.  A normal MessageBoxA owns a kernel-side modal
     * loop; entering that loop from a Ring-3 API thunk prevents the caller's
     * startup frame from returning and can freeze the process before its main
     * window exists (WinZip does exactly this during its dynamic probing).
     *
     * Treat the indirect notification as acknowledged. Direct MessageBoxA/W
     * retain their visible interactive implementation for applications that
     * explicitly request a modal dialog after startup.
     */
    pe_win32_set_last_error(0U);
    return WIN32_IDOK;
}

static int WIN32_API win32_MessageBoxIndirectW(
    const win32_msgbox_params_w_t *params) {
    if (!params || params->cbSize < sizeof(*params)) {
        pe_win32_set_last_error(87U);
        return 0;
    }
    pe_win32_set_last_error(0U);
    return WIN32_IDOK;
}

uint32_t win32_user32_resolve(const char *name) {
#define U(api) if(equal(name,#api))return(uint32_t)(uintptr_t)&win32_##api
    U(MessageBoxA); U(MessageBoxW);
    U(MessageBoxIndirectA); U(MessageBoxIndirectW);
    U(RegisterClassA); U(RegisterClassW); U(RegisterClassExA); U(RegisterClassExW);
    U(UnregisterClassA); U(UnregisterClassW);
    U(GetClassInfoA); U(GetClassInfoW); U(GetClassInfoExA); U(GetClassInfoExW);
    U(CreateWindowExA); U(CreateWindowExW); U(ShowWindow); U(UpdateWindow);
    U(GetDC); U(ReleaseDC); U(BeginPaint); U(EndPaint);
    U(DestroyWindow); U(PostQuitMessage);
    U(GetDesktopWindow); U(GetActiveWindow); U(GetLastActivePopup); U(SetActiveWindow);
    U(SetCapture); U(GetCapture); U(ReleaseCapture);
    U(GetMessageA); U(GetMessageW); U(PeekMessageA); U(PeekMessageW);
    U(DispatchMessageA); U(DispatchMessageW); U(TranslateMessage);
    U(WaitMessage); U(GetMessageTime); U(GetMessagePos);
    U(DefWindowProcA); U(DefWindowProcW);
    U(SetWindowTextA); U(SetWindowTextW); U(GetClientRect);
    U(GetWindowTextA); U(GetWindowTextW);
    U(GetWindowTextLengthA); U(GetWindowTextLengthW);
    U(SendMessageA); U(SendMessageW);
    U(SendMessageTimeoutA); U(SendMessageTimeoutW);
    U(PostMessageA); U(PostMessageW);
    U(InvalidateRect); U(MoveWindow); U(GetWindowRect); U(SetWindowPos); U(ScrollWindowEx); U(ScrollWindow);
    U(AdjustWindowRect); U(AdjustWindowRectEx); U(MapWindowPoints); U(IsChild); U(ChildWindowFromPoint); U(ChildWindowFromPointEx); U(RealChildWindowFromPoint); U(WindowFromPoint);
    U(EnableWindow); U(IsWindowEnabled); U(IsWindowVisible); U(IsIconic); U(IsZoomed); U(GetParent); U(SetParent);
    U(GetWindow); U(GetTopWindow); U(GetAncestor);
    U(SetPropA); U(SetPropW); U(GetPropA); U(GetPropW); U(RemovePropA); U(RemovePropW);
    U(GetKeyState); U(GetAsyncKeyState); U(MapVirtualKeyA); U(VkKeyScanA);
    U(GetKeyboardType); U(MessageBeep); U(WinHelpA);
    U(OpenClipboard); U(CloseClipboard); U(EmptyClipboard);
    U(SetClipboardData); U(GetClipboardData); U(IsClipboardFormatAvailable);
    U(GetForegroundWindow); U(SetForegroundWindow); U(BringWindowToTop);
    U(ClientToScreen); U(ScreenToClient); U(GetSystemMetrics);
    U(GetClassNameA); U(GetClassNameW); U(FindWindowA); U(FindWindowW);
    U(GetWindowLongA); U(GetWindowLongW); U(SetWindowLongA); U(SetWindowLongW);
    U(GetClassLongA); U(GetClassLongW); U(SetClassLongA); U(SetClassLongW);
    U(GetWindowWord); U(SetWindowWord); U(CallWindowProcA); U(CallWindowProcW);
    U(SetRect); U(SetRectEmpty); U(IsRectEmpty); U(EqualRect); U(PtInRect);
    U(OffsetRect); U(InflateRect); U(FillRect); U(DrawFocusRect);
    U(GetDlgItem); U(GetDlgCtrlID); U(SetDlgItemInt); U(GetDlgItemInt);
    U(IsWindow); U(SetFocus); U(GetFocus); U(GetWindowThreadProcessId);
    U(EnumWindows); U(EnumChildWindows);
    U(LoadStringA); U(LoadStringW); U(LoadCursorA); U(LoadCursorW);
    U(LoadIconA); U(LoadIconW); U(LoadBitmapA); U(LoadBitmapW);
    U(LoadImageA); U(LoadImageW); U(DrawIcon); U(DrawIconEx);
    U(DestroyIcon); U(CopyIcon);
    U(CreateIconFromResource); U(CreateIconFromResourceEx);
    U(CreateMenu); U(CreatePopupMenu); U(AppendMenuA); U(AppendMenuW);
    U(LoadMenuA); U(LoadMenuW); U(SetMenu); U(GetMenu); U(GetSystemMenu); U(DrawMenuBar);
    U(DestroyMenu); U(CheckMenuItem); U(SetMenuItemInfoA); U(GetMenuItemInfoA);
    U(EnableMenuItem); U(CheckMenuRadioItem); U(InsertMenuItemA); U(DeleteMenu);
    U(GetSubMenu); U(GetMenuItemCount); U(GetMenuItemID); U(GetMenuState);
    U(GetMenuStringA); U(TrackPopupMenuEx);
    U(DialogBoxParamA); U(DialogBoxParamW);
    U(DialogBoxIndirectParamA); U(DialogBoxIndirectParamW);
    U(CreateDialogIndirectParamA); U(CreateDialogIndirectParamW);
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
    U(CharToOemA); U(OemToCharA); U(CharToOemW); U(OemToCharW);
    U(CharToOemBuffA); U(OemToCharBuffA); U(CharToOemBuffW); U(OemToCharBuffW);
    U(wsprintfA); U(wvsprintfA); U(TabbedTextOutA); U(GetTabbedTextExtentA);
    U(GetSysColor); U(GetSysColorBrush); U(RedrawWindow);
    U(GetWindowPlacement); U(GetScrollInfo); U(EnableScrollBar);
    U(GetKeyboardState); U(SetKeyboardState); U(RegisterWindowMessageA);
    U(SetCursor); U(GetCursorPos); U(SystemParametersInfoA); U(GetDialogBaseUnits);
    U(SetTimer); U(KillTimer);
#undef U
    if(equal(name,"AnsiToOemA"))return(uint32_t)(uintptr_t)&win32_CharToOemA;
    if(equal(name,"OemToAnsiA"))return(uint32_t)(uintptr_t)&win32_OemToCharA;
    if(equal(name,"AnsiToOemBuffA"))return(uint32_t)(uintptr_t)&win32_CharToOemBuffA;
    if(equal(name,"OemToAnsiBuffA"))return(uint32_t)(uintptr_t)&win32_OemToCharBuffA;
    if(equal(name,"GetWindowLongPtrA"))return(uint32_t)(uintptr_t)&win32_GetWindowLongA;
    if(equal(name,"GetWindowLongPtrW"))return(uint32_t)(uintptr_t)&win32_GetWindowLongW;
    if(equal(name,"SetWindowLongPtrA"))return(uint32_t)(uintptr_t)&win32_SetWindowLongA;
    if(equal(name,"SetWindowLongPtrW"))return(uint32_t)(uintptr_t)&win32_SetWindowLongW;
    if(equal(name,"GetClassLongPtrA"))return(uint32_t)(uintptr_t)&win32_GetClassLongA;
    if(equal(name,"GetClassLongPtrW"))return(uint32_t)(uintptr_t)&win32_GetClassLongW;
    if(equal(name,"SetClassLongPtrA"))return(uint32_t)(uintptr_t)&win32_SetClassLongA;
    if(equal(name,"SetClassLongPtrW"))return(uint32_t)(uintptr_t)&win32_SetClassLongW;
    return 0;
}
