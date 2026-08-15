#include "win32.h"
#include "../include/memory.h"
#include "../include/task.h"
#include "../include/pit.h"
#include "../include/pe_loader.h"
#include "../string.h"

#define S_OK 0x00000000U
#define S_FALSE 0x00000001U
#define E_NOINTERFACE 0x80004002U
#define E_POINTER 0x80004003U
#define E_FAIL 0x80004005U
#define E_NOTIMPL 0x80004001U
#define E_INVALIDARG 0x80070057U
#define CLASS_E_NOAGGREGATION 0x80040110U
#define REGDB_E_CLASSNOTREG 0x80040154U
#define STG_E_INVALIDPOINTER 0x80030009U
#define STG_E_MEDIUMFULL 0x80030070U
#define STGTY_STREAM 2U
#define STREAM_SEEK_SET 0U
#define STREAM_SEEK_CUR 1U
#define STREAM_SEEK_END 2U
#define HKEY_CLASSES_ROOT 0x80000000U
#define COM_CLASS_SLOTS 32U
#define COM_STREAM_MAGIC 0x534D4342U

typedef struct PACKED {
    uint32_t data1;
    uint16_t data2, data3;
    uint8_t data4[8];
} win_guid_t;

typedef struct { uint32_t tid, process_id, count, mode; } com_state_t;
typedef struct {
    bool used;
    uint32_t owner_pid, context, flags, cookie;
    win_guid_t clsid;
    void *factory;
} com_class_t;
typedef struct {
    void **vtbl;
    uint32_t magic, refs;
    void *global;
    uint32_t position, size, capacity;
    bool delete_on_release;
} com_stream_t;
typedef struct PACKED {
    uint16_t *name;
    uint32_t type;
    uint64_t size;
    uint64_t mtime, ctime, atime;
    uint32_t mode, locks_supported;
    win_guid_t clsid;
    uint32_t state_bits, reserved;
} com_statstg_t;

static com_state_t com_states[TASK_MAX];
static com_class_t com_classes[COM_CLASS_SLOTS];
static uint32_t next_class_cookie = 1U;

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}
static bool guid_equal(const win_guid_t *a, const win_guid_t *b) {
    return a && b && kmemcmp(a, b, sizeof(*a)) == 0;
}
static com_state_t *com_state(bool create) {
    uint32_t tid = task_current_pid();
    com_state_t *free_slot = NULL;
    for (uint32_t i = 0; i < TASK_MAX; i++) {
        if (com_states[i].tid == tid) return &com_states[i];
        if (!com_states[i].tid && !free_slot) free_slot = &com_states[i];
    }
    if (create && free_slot) { free_slot->tid = tid; free_slot->process_id = task_current_process_id(); }
    return create ? free_slot : NULL;
}
static uint32_t WIN32_API com_query(void *object, const win_guid_t *iid,
                                    void **result) {
    void ***vtbl = (void ***)object;
    typedef uint32_t (WIN32_API *fn_t)(void *, const win_guid_t *, void **);
    if (result) *result = NULL;
    if (!object || !vtbl || !*vtbl || !(*vtbl)[0] || !result) return E_POINTER;
    return ((fn_t)(*vtbl)[0])(object, iid, result);
}
static uint32_t WIN32_API com_addref(void *object) {
    void ***vtbl = (void ***)object;
    typedef uint32_t (WIN32_API *fn_t)(void *);
    if (!object || !vtbl || !*vtbl || !(*vtbl)[1]) return 0U;
    return ((fn_t)(*vtbl)[1])(object);
}
static uint32_t WIN32_API com_release(void *object) {
    void ***vtbl = (void ***)object;
    typedef uint32_t (WIN32_API *fn_t)(void *);
    if (!object || !vtbl || !*vtbl || !(*vtbl)[2]) return 0U;
    return ((fn_t)(*vtbl)[2])(object);
}

static uint32_t WIN32_API ole_CoInitializeEx(void *reserved UNUSED, uint32_t mode) {
    com_state_t *state = com_state(true);
    if (!state) return E_FAIL;
    if (state->count++) return state->mode == mode ? S_FALSE : 0x80010106U;
    state->mode = mode;
    return S_OK;
}
static uint32_t WIN32_API ole_CoInitialize(void *reserved) { return ole_CoInitializeEx(reserved, 2U); }
static uint32_t WIN32_API ole_OleInitialize(void *reserved) { return ole_CoInitializeEx(reserved, 2U); }
static void WIN32_API ole_CoUninitialize(void) {
    com_state_t *state = com_state(false);
    if (state && state->count && --state->count == 0U) kmemset(state, 0, sizeof(*state));
}
static void WIN32_API ole_OleUninitialize(void) { ole_CoUninitialize(); }
static void *WIN32_API ole_CoTaskMemAlloc(uint32_t size) { return kmalloc(size ? size : 1U); }
static void *WIN32_API ole_CoTaskMemRealloc(void *memory, uint32_t size) {
    if (!memory) return ole_CoTaskMemAlloc(size);
    if (!size) { kfree(memory); return NULL; }
    return krealloc(memory, size);
}
static void WIN32_API ole_CoTaskMemFree(void *memory) { if (memory) kfree(memory); }
static int WIN32_API ole_IsEqualGUID(const win_guid_t *a, const win_guid_t *b) { return guid_equal(a, b); }
static uint8_t hex_digit(uint8_t value) { return value < 10U ? (uint8_t)('0' + value) : (uint8_t)('A' + value - 10U); }
static int WIN32_API ole_StringFromGUID2(const win_guid_t *guid, uint16_t *out, int count) {
    uint8_t bytes[16]; const uint8_t groups[5] = {4,2,2,2,6}; int pos = 0, index = 0;
    if (!guid) return 0;
    bytes[0]=(uint8_t)(guid->data1>>24);bytes[1]=(uint8_t)(guid->data1>>16);bytes[2]=(uint8_t)(guid->data1>>8);bytes[3]=(uint8_t)guid->data1;
    bytes[4]=(uint8_t)(guid->data2>>8);bytes[5]=(uint8_t)guid->data2;
    bytes[6]=(uint8_t)(guid->data3>>8);bytes[7]=(uint8_t)guid->data3;
    kmemcpy(bytes+8,guid->data4,8);
    if (!out || count < 39) return 0;
    out[pos++]='{';
    for (uint32_t g=0;g<5;g++) { if(g)out[pos++]='-'; for(uint32_t n=0;n<groups[g];n++){uint8_t v=bytes[index++];out[pos++]=hex_digit(v>>4);out[pos++]=hex_digit(v&15U);} }
    out[pos++]='}'; out[pos]=0; return pos+1;
}
static void guid_to_ansi(const win_guid_t *guid, char out[40]) {
    uint16_t wide[40]; int count = ole_StringFromGUID2(guid, wide, 40);
    uint32_t i = 0;
    if (!count) { out[0] = 0; return; }
    while (wide[i] && i < 39U) { out[i] = (char)wide[i]; i++; }
    out[i] = 0;
}
static uint8_t from_hex(uint16_t c) {
    if(c>='0'&&c<='9')return(uint8_t)(c-'0');
    if(c>='a'&&c<='f')return(uint8_t)(c-'a'+10);
    if(c>='A'&&c<='F')return(uint8_t)(c-'A'+10);
    return 0xFFU;
}
static uint32_t WIN32_API ole_CLSIDFromString(const uint16_t *text, win_guid_t *guid) {
    uint8_t bytes[16]; uint32_t count=0; uint8_t high=0; bool half=false;
    if(!guid)return E_POINTER;
    if(!text){kmemset(guid,0,sizeof(*guid));return S_OK;}
    for(uint32_t i=0;text[i]&&count<16U;i++){uint8_t v=from_hex(text[i]);if(v==0xFFU)continue;if(!half){high=v;half=true;}else{bytes[count++]=(uint8_t)((high<<4)|v);half=false;}}
    if(count!=16U||half)return E_INVALIDARG;
    guid->data1=((uint32_t)bytes[0]<<24)|((uint32_t)bytes[1]<<16)|((uint32_t)bytes[2]<<8)|bytes[3];
    guid->data2=(uint16_t)((bytes[4]<<8)|bytes[5]);guid->data3=(uint16_t)((bytes[6]<<8)|bytes[7]);kmemcpy(guid->data4,bytes+8,8);return S_OK;
}
static uint32_t WIN32_API ole_IIDFromString(const uint16_t *text, win_guid_t *guid) { return ole_CLSIDFromString(text,guid); }
static uint32_t WIN32_API ole_CoCreateGuid(win_guid_t *guid) {
    static uint32_t sequence; uint32_t seed;
    if(!guid)return E_POINTER;
    seed=pit_get_ticks()^(task_current_pid()<<16)^++sequence;
    guid->data1=0xB1E50000U^seed;guid->data2=(uint16_t)seed;guid->data3=(uint16_t)(0x4000U|(seed&0x0FFFU));
    for(uint32_t i=0;i<8;i++)guid->data4[i]=(uint8_t)(seed>>((i&3U)*8U));
    guid->data4[0]=(uint8_t)((guid->data4[0]&0x3FU)|0x80U);
    return S_OK;
}

static com_class_t *find_class(const win_guid_t *clsid) {
    for (uint32_t i = 0; i < COM_CLASS_SLOTS; i++)
        if (com_classes[i].used && guid_equal(&com_classes[i].clsid, clsid)) return &com_classes[i];
    return NULL;
}
static uint32_t WIN32_API ole_CoRegisterClassObject(const win_guid_t *clsid,
        void *factory, uint32_t context, uint32_t flags, uint32_t *cookie) {
    com_class_t *slot = NULL;
    if (cookie) *cookie = 0U;
    if (!clsid || !factory || !cookie) return E_INVALIDARG;
    for (uint32_t i = 0; i < COM_CLASS_SLOTS; i++) if (!com_classes[i].used) { slot = &com_classes[i]; break; }
    if (!slot) return E_FAIL;
    kmemset(slot, 0, sizeof(*slot)); slot->used = true;
    slot->owner_pid = task_current_process_id(); slot->context = context; slot->flags = flags;
    slot->cookie = next_class_cookie++; if (!slot->cookie) slot->cookie = next_class_cookie++;
    slot->clsid = *clsid; slot->factory = factory; com_addref(factory);
    *cookie = slot->cookie; return S_OK;
}
static uint32_t WIN32_API ole_CoRevokeClassObject(uint32_t cookie) {
    for (uint32_t i = 0; i < COM_CLASS_SLOTS; i++) if (com_classes[i].used && com_classes[i].cookie == cookie) {
        com_release(com_classes[i].factory); kmemset(&com_classes[i], 0, sizeof(com_classes[i])); return S_OK;
    }
    return E_INVALIDARG;
}
static uint32_t load_registered_factory(const win_guid_t *clsid,
                                        const win_guid_t *iid, void **object) {
    char guid[40], key[96], dll[260];
    void *module; void *entry;
    typedef uint32_t (WIN32_API *get_class_t)(const win_guid_t *, const win_guid_t *, void **);
    guid_to_ansi(clsid, guid); if (!guid[0]) return REGDB_E_CLASSNOTREG;
    kstrcpy(key, "CLSID\\"); kstrcat(key, guid); kstrcat(key, "\\InprocServer32");
    if (!win32_registry_query_string(HKEY_CLASSES_ROOT, key, NULL, dll, sizeof(dll))) return REGDB_E_CLASSNOTREG;
    module = pe_win32_load_library(dll); if (!module) return REGDB_E_CLASSNOTREG;
    entry = pe_win32_get_proc_address(module, "DllGetClassObject");
    if (!entry) return REGDB_E_CLASSNOTREG;
    return ((get_class_t)entry)(clsid, iid, object);
}
static uint32_t WIN32_API ole_CoGetClassObject(const win_guid_t *clsid,
        uint32_t context UNUSED, void *server_info UNUSED,
        const win_guid_t *iid, void **object) {
    com_class_t *entry;
    if (object) *object = NULL;
    if (!clsid || !iid || !object) return E_POINTER;
    entry = find_class(clsid);
    if (entry) return com_query(entry->factory, iid, object);
    return load_registered_factory(clsid, iid, object);
}
static uint32_t WIN32_API ole_CoCreateInstance(const win_guid_t *clsid, void *outer,
        uint32_t context, const win_guid_t *iid, void **object) {
    static const win_guid_t iid_factory = {1U,0U,0U,{0xC0,0,0,0,0,0,0,0x46}};
    void *factory = NULL; uint32_t result;
    typedef uint32_t (WIN32_API *create_t)(void *, void *, const win_guid_t *, void **);
    if (object) *object = NULL;
    if (!clsid || !iid || !object) return E_POINTER;
    if (outer) return CLASS_E_NOAGGREGATION;
    result = ole_CoGetClassObject(clsid, context, NULL, &iid_factory, &factory);
    if (result != S_OK || !factory) return result;
    if (!*(void ***)factory || !(*(void ***)factory)[3]) { com_release(factory); return E_NOINTERFACE; }
    result = ((create_t)(*(void ***)factory)[3])(factory, outer, iid, object);
    com_release(factory); return result;
}

/* Minimal process-wide IMalloc used by legacy COM clients. */
static const win_guid_t iid_iunknown = {0U,0U,0U,{0xC0,0,0,0,0,0,0,0x46}};
static const win_guid_t iid_imalloc = {0x00000002U,0U,0U,{0xC0,0,0,0,0,0,0,0x46}};
static uint32_t WIN32_API malloc_QueryInterface(void *self,const win_guid_t *iid,void **out){if(out)*out=NULL;if(!iid||!out)return E_POINTER;if(!guid_equal(iid,&iid_iunknown)&&!guid_equal(iid,&iid_imalloc))return E_NOINTERFACE;*out=self;return S_OK;}
static uint32_t WIN32_API malloc_AddRef(void*self UNUSED){return 2U;}
static uint32_t WIN32_API malloc_Release(void*self UNUSED){return 1U;}
static void *WIN32_API malloc_Alloc(void *self UNUSED,uint32_t n){return kmalloc(n?n:1U);}
static void *WIN32_API malloc_Realloc(void*self UNUSED,void*p,uint32_t n){return p?krealloc(p,n?n:1U):kmalloc(n?n:1U);}
static void WIN32_API malloc_Free(void*self UNUSED,void*p){if(p)kfree(p);}
static uint32_t WIN32_API malloc_GetSize(void*self UNUSED,void*p UNUSED){return 0U;}
static int WIN32_API malloc_DidAlloc(void*self UNUSED,void*p){return p?1:0;}
static void WIN32_API malloc_HeapMinimize(void*self UNUSED){}
static void *malloc_vtbl[]={(void*)malloc_QueryInterface,(void*)malloc_AddRef,(void*)malloc_Release,(void*)malloc_Alloc,(void*)malloc_Realloc,(void*)malloc_Free,(void*)malloc_GetSize,(void*)malloc_DidAlloc,(void*)malloc_HeapMinimize};
static void *malloc_object = malloc_vtbl;
static uint32_t WIN32_API ole_CoGetMalloc(uint32_t context UNUSED,void **out){if(!out)return E_POINTER;*out=&malloc_object;return S_OK;}

static bool stream_valid(com_stream_t *stream) { return stream && stream->magic == COM_STREAM_MAGIC; }
static uint32_t stream_grow(com_stream_t *stream, uint32_t need) {
    void *new_handle, *old_data, *new_data; uint32_t new_capacity;
    if (need <= stream->capacity) return S_OK;
    new_capacity = stream->capacity ? stream->capacity : 4096U;
    while (new_capacity < need) { uint32_t next = new_capacity * 2U; if (next <= new_capacity) return STG_E_MEDIUMFULL; new_capacity = next; }
    new_handle = win32_global_alloc_block(0x0042U, new_capacity); if (!new_handle) return STG_E_MEDIUMFULL;
    new_data = win32_global_lock_block(new_handle); old_data = win32_global_lock_block(stream->global);
    if (!new_data || !old_data) { if(new_data)win32_global_unlock_block(new_handle); if(old_data)win32_global_unlock_block(stream->global); win32_global_release_handle(new_handle); return E_FAIL; }
    if (stream->size) kmemcpy(new_data, old_data, stream->size);
    win32_global_unlock_block(stream->global); win32_global_unlock_block(new_handle);
    if (stream->delete_on_release) win32_global_release_handle(stream->global);
    stream->global = new_handle; stream->capacity = new_capacity; stream->delete_on_release = true;
    return S_OK;
}
static uint32_t WIN32_API stream_QueryInterface(com_stream_t*s,const win_guid_t*iid,void**out){
    static const win_guid_t unknown={0,0,0,{0xC0,0,0,0,0,0,0,0x46}};
    static const win_guid_t sequential={0x0c733a30U,0x2a1cU,0x11ceU,{0xad,0xe5,0,0xaa,0x00,0x44,0x77,0x3d}};
    static const win_guid_t stream={0x0000000cU,0,0,{0xC0,0,0,0,0,0,0,0x46}};
    if (out) *out = NULL;
    if (!stream_valid(s) || !iid || !out) return E_POINTER;
    if(!guid_equal(iid,&unknown)&&!guid_equal(iid,&sequential)&&!guid_equal(iid,&stream))return E_NOINTERFACE;
    *out=s;s->refs++;return S_OK;
}
static uint32_t WIN32_API stream_AddRef(com_stream_t*s){return stream_valid(s)?++s->refs:0U;}
static uint32_t WIN32_API stream_Release(com_stream_t*s){uint32_t refs;if(!stream_valid(s)||!s->refs)return 0;refs=--s->refs;if(!refs){if(s->delete_on_release&&s->global)win32_global_release_handle(s->global);s->magic=0;kfree(s);}return refs;}
static uint32_t WIN32_API stream_Read(com_stream_t*s,void*buffer,uint32_t count,uint32_t*read){uint32_t avail,n;void*data;if(read)*read=0;if(!stream_valid(s)||(!buffer&&count))return STG_E_INVALIDPOINTER;avail=s->position<s->size?s->size-s->position:0;n=count<avail?count:avail;data=win32_global_lock_block(s->global);if(!data)return E_FAIL;if(n)kmemcpy(buffer,(uint8_t*)data+s->position,n);win32_global_unlock_block(s->global);s->position+=n;if(read)*read=n;return n==count?S_OK:S_FALSE;}
static uint32_t WIN32_API stream_Write(com_stream_t*s,const void*buffer,uint32_t count,uint32_t*written){uint32_t result;void*data;if(written)*written=0;if(!stream_valid(s)||(!buffer&&count))return STG_E_INVALIDPOINTER;if(s->position+count<s->position)return STG_E_MEDIUMFULL;result=stream_grow(s,s->position+count);if(result)return result;data=win32_global_lock_block(s->global);if(!data)return E_FAIL;if(count)kmemcpy((uint8_t*)data+s->position,buffer,count);win32_global_unlock_block(s->global);s->position+=count;if(s->position>s->size)s->size=s->position;if(written)*written=count;return S_OK;}
static uint32_t WIN32_API stream_Seek(com_stream_t*s,int64_t move,uint32_t origin,uint64_t*position){int64_t base,next;if(!stream_valid(s))return E_POINTER;base=origin==STREAM_SEEK_SET?0:(origin==STREAM_SEEK_CUR?(int64_t)s->position:(origin==STREAM_SEEK_END?(int64_t)s->size:-1));if(base<0)return E_INVALIDARG;next=base+move;if(next<0||next>0xFFFFFFFFLL)return E_INVALIDARG;s->position=(uint32_t)next;if(position)*position=s->position;return S_OK;}
static uint32_t WIN32_API stream_SetSize(com_stream_t*s,uint64_t size){uint32_t result;if(!stream_valid(s)||size>0xFFFFFFFFULL)return E_INVALIDARG;result=stream_grow(s,(uint32_t)size);if(result)return result;s->size=(uint32_t)size;if(s->position>s->size)s->position=s->size;return S_OK;}
static uint32_t WIN32_API stream_CopyTo(com_stream_t*s,void*destination,uint64_t count,uint64_t*read64,uint64_t*written64){uint8_t buffer[512];uint64_t total=0;if(read64)*read64=0;if(written64)*written64=0;if(!stream_valid(s)||!destination)return E_POINTER;while(total<count){uint32_t want=(count-total)>sizeof(buffer)?sizeof(buffer):(uint32_t)(count-total),got=0,wrote=0;uint32_t r=stream_Read(s,buffer,want,&got);if(got){void***vt=(void***)destination;typedef uint32_t(WIN32_API*write_t)(void*,const void*,uint32_t,uint32_t*);if(!vt||!*vt||!(*vt)[4])return E_NOINTERFACE;r=((write_t)(*vt)[4])(destination,buffer,got,&wrote);total+=got;if(written64)*written64+=wrote;if(r||wrote!=got)return r?r:STG_E_MEDIUMFULL;}if(r==S_FALSE||!got)break;}if(read64)*read64=total;return total==count?S_OK:S_FALSE;}
static uint32_t WIN32_API stream_Commit(com_stream_t*s UNUSED,uint32_t flags UNUSED){return S_OK;}
static uint32_t WIN32_API stream_Revert(com_stream_t*s UNUSED){return E_NOTIMPL;}
static uint32_t WIN32_API stream_LockRegion(com_stream_t*s UNUSED,uint64_t a UNUSED,uint64_t b UNUSED,uint32_t c UNUSED){return E_NOTIMPL;}
static uint32_t WIN32_API stream_UnlockRegion(com_stream_t*s UNUSED,uint64_t a UNUSED,uint64_t b UNUSED,uint32_t c UNUSED){return E_NOTIMPL;}
static uint32_t WIN32_API stream_Stat(com_stream_t*s,com_statstg_t*stat,uint32_t flags UNUSED){if(!stream_valid(s)||!stat)return E_POINTER;kmemset(stat,0,sizeof(*stat));stat->type=STGTY_STREAM;stat->size=s->size;stat->mode=2U;return S_OK;}
static uint32_t WIN32_API stream_Clone(com_stream_t*s UNUSED,void**out){if(out)*out=NULL;return E_NOTIMPL;}
static void *stream_vtbl[]={(void*)stream_QueryInterface,(void*)stream_AddRef,(void*)stream_Release,(void*)stream_Read,(void*)stream_Write,(void*)stream_Seek,(void*)stream_SetSize,(void*)stream_CopyTo,(void*)stream_Commit,(void*)stream_Revert,(void*)stream_LockRegion,(void*)stream_UnlockRegion,(void*)stream_Stat,(void*)stream_Clone};
static uint32_t WIN32_API ole_CreateStreamOnHGlobal(void*global,int delete_on_release,void**out){com_stream_t*s;uint32_t capacity;bool created=false;if(out)*out=NULL;if(!out)return E_POINTER;if(global&&!win32_global_handle_valid(global))return E_INVALIDARG;if(!global){global=win32_global_alloc_block(0x0042U,4096U);if(!global)return E_FAIL;delete_on_release=1;created=true;}capacity=win32_global_size_block(global);s=(com_stream_t*)kzalloc(sizeof(*s));if(!s){if(delete_on_release)win32_global_release_handle(global);return E_FAIL;}s->vtbl=stream_vtbl;s->magic=COM_STREAM_MAGIC;s->refs=1;s->global=global;s->capacity=capacity;s->size=created?0U:capacity;s->delete_on_release=delete_on_release!=0;*out=s;return S_OK;}
static uint32_t WIN32_API ole_GetHGlobalFromStream(void*object,void**global){com_stream_t*s=(com_stream_t*)object;if(global)*global=NULL;if(!stream_valid(s)||!global)return E_INVALIDARG;*global=s->global;return S_OK;}

/* BLES_WINE_RELEASESTGMEDIUM_BATCH_20260723 */
#define TYMED_NULL      0U
#define TYMED_HGLOBAL   1U
#define TYMED_FILE      2U
#define TYMED_ISTREAM   4U
#define TYMED_ISTORAGE  8U

typedef struct {
    uint32_t tymed;
    void *value;
    void *release_unknown;
} win_stgmedium_t;

static void WIN32_API ole_ReleaseStgMedium(win_stgmedium_t *medium) {
    if (!medium) return;

    if (medium->release_unknown) {
        com_release(medium->release_unknown);
    } else {
        switch (medium->tymed) {
        case TYMED_HGLOBAL:
            if (medium->value && win32_global_handle_valid(medium->value))
                win32_global_release_handle(medium->value);
            break;

        case TYMED_FILE:
            if (medium->value)
                ole_CoTaskMemFree(medium->value);
            break;

        case TYMED_ISTREAM:
        case TYMED_ISTORAGE:
            if (medium->value)
                com_release(medium->value);
            break;

        case TYMED_NULL:
        default:
            break;
        }
    }

    medium->tymed = TYMED_NULL;
    medium->value = NULL;
    medium->release_unknown = NULL;
}

static uint32_t WIN32_API ole_CoBuildVersion(void) { return 0x00040000U; }
static uint32_t WIN32_API ole_CoGetCurrentProcess(void) { return task_current_process_id(); }
static void WIN32_API ole_CoFreeUnusedLibraries(void) {}
static void WIN32_API ole_CoFreeAllLibraries(void) {}
static uint32_t WIN32_API ole_CoLockObjectExternal(void*object,int lock,int last_unlock_releases){if(!object)return E_POINTER;if(lock)com_addref(object);else{com_release(object);if(last_unlock_releases)com_release(object);}return S_OK;}

void win32_ole32_cleanup_process(uint32_t pid) {
    for (uint32_t i = 0; i < COM_CLASS_SLOTS; i++) if (com_classes[i].used && com_classes[i].owner_pid == pid) {
        com_release(com_classes[i].factory); kmemset(&com_classes[i], 0, sizeof(com_classes[i]));
    }
    for (uint32_t i = 0; i < TASK_MAX; i++) if (com_states[i].process_id == pid) kmemset(&com_states[i], 0, sizeof(com_states[i]));
}

uint32_t win32_ole32_resolve(const char *name) {
#define O(api) if(equal(name,#api))return(uint32_t)(uintptr_t)&ole_##api
    O(CoInitialize);O(CoInitializeEx);O(CoUninitialize);O(OleInitialize);O(OleUninitialize);
    O(CoTaskMemAlloc);O(CoTaskMemRealloc);O(CoTaskMemFree);O(IsEqualGUID);
    O(StringFromGUID2);O(CLSIDFromString);O(IIDFromString);O(CoCreateGuid);
    O(CoRegisterClassObject);O(CoRevokeClassObject);O(CoGetClassObject);O(CoCreateInstance);
    O(CoGetMalloc);O(CreateStreamOnHGlobal);O(GetHGlobalFromStream);O(CoLockObjectExternal);O(ReleaseStgMedium);
    O(CoBuildVersion);O(CoGetCurrentProcess);O(CoFreeUnusedLibraries);O(CoFreeAllLibraries);
#undef O
    return 0;
}
