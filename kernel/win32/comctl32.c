#include "win32.h"
#include "resources.h"
#include "../include/pe_loader.h"
#include "../include/types.h"
#include "../include/memory.h"
#include "../stdio.h"

#define WS_CHILD   0x40000000U
#define WS_VISIBLE 0x10000000U
#define CCS_TOP    0x00000001U
#define TBSTYLE_TOOLTIPS 0x0100U
#define CW_USEDEFAULT ((int)0x80000000U)
#define COMCTL32_ORD_CREATE_STATUS_WINDOW_A 6U
#define WM_INITDIALOG 0x0110U
#define WM_COMMAND 0x0111U
#define WM_NOTIFY 0x004EU
#define WIN32_IDOK 1U
#define WIN32_IDCANCEL 2U
#define PSH_PROPSHEETPAGE 0x00000008U
#define PSN_APPLY (-202)
#define PSN_RESET (-203)
#define PSNRET_INVALID 1

#define COMCTL32_ORD_CREATE_MAPPED_BITMAP 8U
#define COMCTL32_ORD_INIT_COMMON_CONTROLS 17U
#define IMAGELIST_BASE 0x7B000000U
#define IMAGELIST_MAX 16U
#define IMAGELIST_IMAGES 64U

typedef void * (WIN32_API *create_window_ex_a_t)(uint32_t,const char*,const char*,
    uint32_t,int,int,int,int,void*,void*,void*,void*);
typedef void * (WIN32_API *load_bitmap_a_t)(void*,const char*);
typedef int32_t (WIN32_API *dialog_box_param_a_t)(void*,const char*,void*,void*,int32_t);
typedef int32_t (WIN32_API *send_message_a_t)(void*,uint32_t,uint32_t,int32_t);
typedef void * (WIN32_API *create_window_a_t)(uint32_t,const char*,const char*,uint32_t,
    int,int,int,int,void*,void*,void*,void*);
typedef int (WIN32_API *end_dialog_t)(void*,int);
typedef int (WIN32_API *set_window_pos_t)(void*,void*,int,int,int,int,uint32_t);
typedef int32_t (WIN32_API *page_proc_t)(void*,uint32_t,uint32_t,int32_t);

typedef struct {
    uint32_t dwSize, dwFlags;
    void *hInstance;
    const char *pszTemplate;
    void *hIcon;
    const char *pszTitle;
    page_proc_t pfnDlgProc;
    int32_t lParam;
    void *pfnCallback;
    uint32_t *pcRefParent;
} prop_sheet_page_a_t;

typedef struct {
    uint32_t dwSize, dwFlags;
    void *hwndParent, *hInstance, *hIcon;
    const char *pszCaption;
    uint32_t nPages, nStartPage;
    const void *pages;
    void *pfnCallback;
} prop_sheet_header_a_t;

typedef struct { void *hwndFrom; uint32_t idFrom; int32_t code; } notify_header_t;
typedef struct { const prop_sheet_page_a_t *page; } prop_sheet_state_t;
static prop_sheet_state_t active_sheet;
typedef struct {
    bool used;
    int width, height, count;
    uint32_t bk_color;
    void *images[IMAGELIST_IMAGES];
    uint32_t *pixels[IMAGELIST_IMAGES];
} image_list_t;
static image_list_t image_lists[IMAGELIST_MAX];

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static void WIN32_API comctl_InitCommonControls(void) {
    /* BlesKernOS registers its built-in controls eagerly. */
}
static int WIN32_API comctl_InitCommonControlsEx(const void*controls){return controls?1:0;}
static image_list_t*image_list_from(void*handle){uint32_t value=(uint32_t)(uintptr_t)handle;if(value<IMAGELIST_BASE||value>=IMAGELIST_BASE+IMAGELIST_MAX)return NULL;value-=IMAGELIST_BASE;return image_lists[value].used?&image_lists[value]:NULL;}
static void*WIN32_API comctl_ImageList_Create(int width,int height,uint32_t flags UNUSED,int initial,int grow UNUSED){if(width<=0||height<=0)return NULL;for(uint32_t i=0;i<IMAGELIST_MAX;i++)if(!image_lists[i].used){kmemset(&image_lists[i],0,sizeof(image_lists[i]));image_lists[i].used=true;image_lists[i].width=width;image_lists[i].height=height;(void)initial;return(void*)(uintptr_t)(IMAGELIST_BASE+i);}return NULL;}
static void image_list_release_slot(image_list_t *list, int index) {
    if (!list || index < 0 || index >= IMAGELIST_IMAGES) return;
    if (list->pixels[index]) kfree(list->pixels[index]);
    list->pixels[index] = NULL;
    list->images[index] = NULL;
}
static bool image_list_copy_pixels(image_list_t *list, int index,
                                   const uint32_t *source, int source_width,
                                   int source_height, int source_x) {
    uint32_t *copy;
    if (!list || !source || index < 0 || index >= IMAGELIST_IMAGES ||
        source_width <= 0 || source_height <= 0) return false;
    copy = (uint32_t *)kzalloc((size_t)list->width * (size_t)list->height *
                               sizeof(uint32_t));
    if (!copy) return false;
    for (int y = 0; y < list->height && y < source_height; y++)
        for (int x = 0; x < list->width &&
                        source_x + x < source_width; x++)
            copy[(uint32_t)y * (uint32_t)list->width + (uint32_t)x] =
                source[(uint32_t)y * (uint32_t)source_width +
                       (uint32_t)(source_x + x)];
    image_list_release_slot(list, index);
    list->pixels[index] = copy;
    return true;
}
static int WIN32_API comctl_ImageList_Destroy(void*handle){image_list_t*list=image_list_from(handle);if(!list)return 0;for(int i=0;i<IMAGELIST_IMAGES;i++)image_list_release_slot(list,i);kmemset(list,0,sizeof(*list));return 1;}
static int WIN32_API comctl_ImageList_Add(void*handle,void*image,void*mask UNUSED){
    image_list_t*list=image_list_from(handle);
    const uint32_t *pixels=NULL;
    int width=0,height=0,first,count;
    if(!list||list->count>=IMAGELIST_IMAGES)return-1;
    first=list->count;
    if(win32_gdi_bitmap_query(image,&width,&height,&pixels)&&pixels){
        count=width/list->width;if(count<1)count=1;
        if(count>IMAGELIST_IMAGES-list->count)
            count=IMAGELIST_IMAGES-list->count;
        for(int i=0;i<count;i++){
            if(!image_list_copy_pixels(list,list->count,pixels,width,height,
                                       i*list->width))break;
            list->images[list->count]=image;
            list->count++;
        }
        return list->count>first?first:-1;
    }
    if(win32_icon_get(image,&pixels,&width,&height)&&pixels&&
       image_list_copy_pixels(list,list->count,pixels,width,height,0)){
        list->images[list->count]=image;list->count++;return first;
    }
    return-1;
}
static int WIN32_API comctl_ImageList_AddMasked(void*handle,void*image,uint32_t mask UNUSED){return comctl_ImageList_Add(handle,image,NULL);}
static int WIN32_API comctl_ImageList_ReplaceIcon(void*handle,int index,void*icon){image_list_t*list=image_list_from(handle);const uint32_t*p=NULL;int w=0,h=0;if(!list)return-1;if(index<0)return comctl_ImageList_Add(handle,icon,NULL);if(index>=list->count||!win32_icon_get(icon,&p,&w,&h)||!image_list_copy_pixels(list,index,p,w,h,0))return-1;list->images[index]=icon;return index;}
static int WIN32_API comctl_ImageList_GetImageCount(void*handle){image_list_t*list=image_list_from(handle);return list?list->count:0;}
static uint32_t WIN32_API comctl_ImageList_SetBkColor(void*handle,uint32_t color){image_list_t*list=image_list_from(handle);uint32_t old;if(!list)return 0xFFFFFFFFU;old=list->bk_color;list->bk_color=color;return old;}
static int WIN32_API comctl_ImageList_SetImageCount(void*handle,uint32_t count){image_list_t*list=image_list_from(handle);if(!list||count>IMAGELIST_IMAGES)return 0;if(count<(uint32_t)list->count)for(uint32_t i=count;i<(uint32_t)list->count;i++)image_list_release_slot(list,(int)i);else for(uint32_t i=(uint32_t)list->count;i<count;i++){list->images[i]=NULL;list->pixels[i]=NULL;}list->count=(int)count;return 1;}
static int WIN32_API comctl_ImageList_Remove(void*handle,int index){image_list_t*list=image_list_from(handle);if(!list)return 0;if(index==-1){for(int i=0;i<list->count;i++)image_list_release_slot(list,i);list->count=0;return 1;}if(index<0||index>=list->count)return 0;image_list_release_slot(list,index);for(int i=index+1;i<list->count;i++){list->images[i-1]=list->images[i];list->pixels[i-1]=list->pixels[i];}list->count--;list->images[list->count]=NULL;list->pixels[list->count]=NULL;return 1;}
static int WIN32_API comctl_ImageList_GetIconSize(void*handle,int*width,int*height){image_list_t*list=image_list_from(handle);if(!list)return 0;if(width)*width=list->width;if(height)*height=list->height;return 1;}
static int WIN32_API comctl_ImageList_SetIconSize(void*handle,int width,int height){image_list_t*list=image_list_from(handle);if(!list||width<=0||height<=0)return 0;list->width=width;list->height=height;return 1;}
static void*WIN32_API comctl_ImageList_GetIcon(void*handle,int index,uint32_t flags UNUSED){image_list_t*list=image_list_from(handle);return list&&index>=0&&index<list->count?list->images[index]:NULL;}
bool win32_comctl_image_list_get_pixels(void *handle, int index,
                                        const uint32_t **pixels,
                                        int *width, int *height) {
    image_list_t *list=image_list_from(handle);
    if(!list||index<0||index>=list->count||!list->pixels[index])return false;
    if(pixels)*pixels=list->pixels[index];
    if(width)*width=list->width;
    if(height)*height=list->height;
    return true;
}
static int WIN32_API comctl_ImageList_Draw(void*handle,int index,void*dc,int x,int y,uint32_t style UNUSED){
    image_list_t *list=image_list_from(handle);
    if(!list||!dc||index<0||index>=list->count)return 0;
    return list->pixels[index]&&win32_gdi_blit(dc,x,y,list->width,list->height,
        list->pixels[index],list->width,0,0)?1:0;
}
static int WIN32_API comctl_ImageList_DrawEx(void*handle,int index,void*dc,int x,int y,int width,int height,uint32_t background UNUSED,uint32_t foreground UNUSED,uint32_t style){
    image_list_t *list=image_list_from(handle);
    if(!list||!dc||index<0||index>=list->count)return 0;
    (void)background;(void)foreground;(void)style;
    return list->pixels[index]&&win32_gdi_blit(dc,x,y,
        width>0?width:list->width,height>0?height:list->height,
        list->pixels[index],list->width,0,0)?1:0;
}

/* BLES_WINE_CREATESTATUSWINDOW_20260723
 *
 * Wine implements CreateStatusWindowA/W as a normal creation of the
 * msctls_statusbar32 class using CW_USEDEFAULT geometry.
 */
static void *WIN32_API comctl_CreateStatusWindowA(
    int32_t style, const char *text, void *parent, uint32_t id) {
    create_window_ex_a_t create_window =
        (create_window_ex_a_t)(uintptr_t)pe_win32_resolve_export(
            "USER32.DLL", "CreateWindowExA");
    void *hwnd;

    if (!create_window || !parent) return NULL;

    hwnd = create_window(
        0U, "msctls_statusbar32", text ? text : "",
        (uint32_t)style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT,
        parent, (void *)(uintptr_t)id, NULL, NULL);

    kprintf("[COMCTL:STATUS] CreateStatusWindowA parent=%x id=%u "
            "style=%x text=%s -> hwnd=%x\n",
            (uint32_t)(uintptr_t)parent, id, (uint32_t)style,
            text ? text : "",
            (uint32_t)(uintptr_t)hwnd);
    return hwnd;
}

static void *WIN32_API comctl_CreateStatusWindowW(
    int32_t style, const uint16_t *text, void *parent, uint32_t id) {
    char ansi[128];
    uint32_t index = 0U;

    if (text) {
        while (text[index] && index + 1U < sizeof(ansi)) {
            ansi[index] = text[index] <= 0xFFU ? (char)text[index] : '?';
            index++;
        }
    }
    ansi[index] = '\0';

    kprintf("[COMCTL:STATUS] CreateStatusWindowW parent=%x id=%u "
            "style=%x\n",
            (uint32_t)(uintptr_t)parent, id, (uint32_t)style);
    return comctl_CreateStatusWindowA(style, ansi, parent, id);
}

static void *WIN32_API comctl_CreateMappedBitmap(void *instance, uint32_t id,
                                                  uint32_t flags UNUSED,
                                                  const void *map UNUSED,
                                                  int map_count UNUSED) {
    load_bitmap_a_t load_bitmap = (load_bitmap_a_t)(uintptr_t)
        pe_win32_resolve_export("USER32.DLL", "LoadBitmapA");
    if (!load_bitmap) return NULL;
    return load_bitmap(instance, (const char *)(uintptr_t)id);
}

static void *WIN32_API comctl_CreateToolbarEx(void *parent, uint32_t style,
                                               uint32_t id, int bitmap_count,
                                               void *bitmap_instance,
                                               uint32_t bitmap_id,
                                               const void *buttons,
                                               int button_count,
                                               int button_width,
                                               int button_height,
                                               int bitmap_width,
                                               int bitmap_height,
                                               uint32_t struct_size UNUSED) {
    create_window_ex_a_t create_window = (create_window_ex_a_t)(uintptr_t)
        pe_win32_resolve_export("USER32.DLL", "CreateWindowExA");
    int width = button_width > 0 ? button_width * (button_count > 0 ? button_count : 1) : 240;
    int height = button_height > 0 ? button_height + 6 : 26;
    void *toolbar;
    if (!create_window || !parent) return NULL;
    toolbar = create_window(0U, "ToolbarWindow32", "",
                         style | WS_CHILD | WS_VISIBLE | CCS_TOP | TBSTYLE_TOOLTIPS,
                         0, 0, width, height, parent,
                         (void *)(uintptr_t)id, bitmap_instance, NULL);
    if (toolbar)
        win32_toolbar_configure(toolbar, buttons,
            button_count > 0 ? (uint32_t)button_count : 0U,
            button_width, button_height);
    if (toolbar && bitmap_count > 0 && bitmap_instance) {
        send_message_a_t send_message = (send_message_a_t)(uintptr_t)
            pe_win32_resolve_export("USER32.DLL", "SendMessageA");
        struct { void *instance; uint32_t bitmap_id; } add = {
            bitmap_instance, bitmap_id
        };
        if (send_message) {
            if (bitmap_width > 0 && bitmap_height > 0)
                (void)send_message(toolbar, 0x0420U, 0U,
                    (int32_t)(((uint32_t)(uint16_t)bitmap_height << 16) |
                              (uint16_t)bitmap_width));
            (void)send_message(toolbar, 0x0413U, (uint32_t)bitmap_count,
                               (int32_t)(uintptr_t)&add);
        }
    }
    return toolbar;
}

static int32_t WIN32_API property_page_proxy(void *hwnd, uint32_t message,
                                               uint32_t wparam, int32_t lparam) {
    const prop_sheet_page_a_t *page = active_sheet.page;
    page_proc_t page_proc = page ? page->pfnDlgProc : NULL;
    if (message == WM_INITDIALOG) {
        create_window_a_t create_window = (create_window_a_t)(uintptr_t)
            pe_win32_resolve_export("USER32.DLL", "CreateWindowExA");
        set_window_pos_t set_position = (set_window_pos_t)(uintptr_t)
            pe_win32_resolve_export("USER32.DLL", "SetWindowPos");
        int32_t result = page_proc ? page_proc(hwnd, message, wparam,
            (int32_t)(uintptr_t)page) : 1;
        if (set_position) (void)set_position(hwnd, NULL, 0, 0, 470, 390, 0x0002U | 0x0004U);
        if (create_window) {
            (void)create_window(0U, "BUTTON", "OK", WS_CHILD | WS_VISIBLE,
                280, 326, 78, 26, hwnd, (void *)(uintptr_t)WIN32_IDOK,
                page ? page->hInstance : NULL, NULL);
            (void)create_window(0U, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE,
                368, 326, 78, 26, hwnd, (void *)(uintptr_t)WIN32_IDCANCEL,
                page ? page->hInstance : NULL, NULL);
        }
        return result;
    }
    if (message == WM_COMMAND && ((wparam & 0xFFFFU) == WIN32_IDOK ||
                                  (wparam & 0xFFFFU) == WIN32_IDCANCEL)) {
        end_dialog_t end_dialog = (end_dialog_t)(uintptr_t)
            pe_win32_resolve_export("USER32.DLL", "EndDialog");
        uint32_t id = wparam & 0xFFFFU;
        if (page_proc) {
            notify_header_t notify = { hwnd, 0U,
                id == WIN32_IDOK ? PSN_APPLY : PSN_RESET };
            int32_t result = page_proc(hwnd, WM_NOTIFY, 0U,
                                      (int32_t)(uintptr_t)&notify);
            if (id == WIN32_IDOK && result == PSNRET_INVALID) return 1;
        }
        if (end_dialog) (void)end_dialog(hwnd, (int)id);
        return 1;
    }
    return page_proc ? page_proc(hwnd, message, wparam, lparam) : 0;
}

static int WIN32_API comctl_PropertySheetA(const void *raw_header) {
    const prop_sheet_header_a_t *header = (const prop_sheet_header_a_t *)raw_header;
    const prop_sheet_page_a_t *page;
    dialog_box_param_a_t dialog_box;
    uint32_t page_index, page_size;
    int32_t result;
    if (!header || header->dwSize < 40U || !header->nPages || !header->pages ||
        !(header->dwFlags & PSH_PROPSHEETPAGE)) return -1;
    page = (const prop_sheet_page_a_t *)header->pages;
    page_size = page->dwSize;
    if (page_size < 40U) return -1;
    page_index = header->nStartPage < header->nPages ? header->nStartPage : 0U;
    page = (const prop_sheet_page_a_t *)((const uint8_t *)header->pages +
                                        page_index * page_size);
    if (!page->pfnDlgProc || !page->pszTemplate) return -1;
    dialog_box = (dialog_box_param_a_t)(uintptr_t)
        pe_win32_resolve_export("USER32.DLL", "DialogBoxParamA");
    if (!dialog_box) return -1;
    active_sheet.page = page;
    result = dialog_box(page->hInstance ? page->hInstance : header->hInstance,
                        page->pszTemplate, header->hwndParent,
                        (void *)(uintptr_t)&property_page_proxy,
                        (int32_t)(uintptr_t)page);
    active_sheet.page = NULL;
    return result == (int32_t)WIN32_IDOK ? 1 : 0;
}

uint32_t win32_comctl32_resolve(const char *name) {
#define C(api) if (equal(name, #api)) return (uint32_t)(uintptr_t)&comctl_##api
    C(InitCommonControls);
    C(InitCommonControlsEx);
    C(ImageList_Create); C(ImageList_Destroy); C(ImageList_Add); C(ImageList_AddMasked);
    C(ImageList_ReplaceIcon); C(ImageList_GetImageCount); C(ImageList_SetImageCount);
    C(ImageList_SetBkColor);
    C(ImageList_Remove); C(ImageList_GetIconSize); C(ImageList_SetIconSize);
    C(ImageList_GetIcon); C(ImageList_Draw); C(ImageList_DrawEx);
    C(CreateMappedBitmap);
    C(CreateStatusWindowA);
    C(CreateStatusWindowW);
    if (equal(name, "CreateStatusWindow"))
        return (uint32_t)(uintptr_t)&comctl_CreateStatusWindowA;
    C(CreateToolbarEx);
    C(PropertySheetA);
#undef C
    return 0;
}

uint32_t win32_comctl32_resolve_ordinal(uint16_t ordinal) {
    if (ordinal == COMCTL32_ORD_CREATE_STATUS_WINDOW_A)
        return (uint32_t)(uintptr_t)&comctl_CreateStatusWindowA;
    if (ordinal == COMCTL32_ORD_CREATE_MAPPED_BITMAP)
        return (uint32_t)(uintptr_t)&comctl_CreateMappedBitmap;
    if (ordinal == COMCTL32_ORD_INIT_COMMON_CONTROLS)
        return (uint32_t)(uintptr_t)&comctl_InitCommonControls;
    return 0;
}
