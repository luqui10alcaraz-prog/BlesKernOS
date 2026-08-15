#include "win32.h"
#include "../include/memory.h"

#define S_OK 0U
#define E_INVALIDARG 0x80070057U
#define E_OUTOFMEMORY 0x8007000EU
#define DISP_E_BADVARTYPE 0x80020008U
#define VT_EMPTY 0U
#define VT_I2 2U
#define VT_I4 3U
#define VT_BSTR 8U
#define VT_BOOL 11U
#define VT_UI1 17U

typedef struct PACKED { uint16_t vt, r1, r2, r3; uint32_t value, high; } variant32_t;
static bool equal(const char*a,const char*b){if(!a||!b)return false;while(*a&&*b&&*a==*b){a++;b++;}return *a==*b;}
static uint32_t wide_len(const uint16_t *s){uint32_t n=0;if(s)while(s[n])n++;return n;}
static uint16_t *WIN32_API oa_SysAllocStringLen(const uint16_t *source,uint32_t length){
    uint32_t bytes=length*2U;uint32_t *base=(uint32_t*)kmalloc(bytes+6U);uint16_t*out;
    if(!base)return NULL;
    *base=bytes;out=(uint16_t*)(base+1);
    if(source&&bytes)kmemcpy(out,source,bytes);else if(bytes)kmemset(out,0,bytes);
    out[length]=0;return out;
}
static uint16_t *WIN32_API oa_SysAllocString(const uint16_t *source){return oa_SysAllocStringLen(source,wide_len(source));}
static uint16_t *WIN32_API oa_SysAllocStringByteLen(const char *source,uint32_t bytes){
    uint32_t *base=(uint32_t*)kmalloc(bytes+6U);char*out;if(!base)return NULL;*base=bytes;out=(char*)(base+1);if(source&&bytes)kmemcpy(out,source,bytes);else if(bytes)kmemset(out,0,bytes);out[bytes]=out[bytes+1]=0;return(uint16_t*)out;
}
static void WIN32_API oa_SysFreeString(uint16_t *string){if(string)kfree(((uint32_t*)string)-1);}
static uint32_t WIN32_API oa_SysStringLen(const uint16_t *string){return string?*(((const uint32_t*)string)-1)/2U:0U;}
static uint32_t WIN32_API oa_SysStringByteLen(const uint16_t *string){return string?*(((const uint32_t*)string)-1):0U;}
static int WIN32_API oa_SysReAllocStringLen(uint16_t **target,const uint16_t *source,uint32_t length){uint16_t*n;if(!target)return 0;n=oa_SysAllocStringLen(source,length);if(!n)return 0;oa_SysFreeString(*target);*target=n;return 1;}
static int WIN32_API oa_SysReAllocString(uint16_t **target,const uint16_t *source){return oa_SysReAllocStringLen(target,source,wide_len(source));}
static void WIN32_API oa_VariantInit(variant32_t *value){if(value)kmemset(value,0,sizeof(*value));}
static uint32_t WIN32_API oa_VariantClear(variant32_t *value){if(!value)return E_INVALIDARG;if(value->vt==VT_BSTR)oa_SysFreeString((uint16_t*)(uintptr_t)value->value);kmemset(value,0,sizeof(*value));return S_OK;}
static uint32_t WIN32_API oa_VariantCopy(variant32_t *dest,const variant32_t *source){if(!dest||!source)return E_INVALIDARG;if(dest==source)return S_OK;oa_VariantClear(dest);*dest=*source;if(source->vt==VT_BSTR&&source->value){dest->value=(uint32_t)(uintptr_t)oa_SysAllocString((const uint16_t*)(uintptr_t)source->value);if(!dest->value){dest->vt=VT_EMPTY;return E_OUTOFMEMORY;}}return S_OK;}
static uint32_t WIN32_API oa_VariantCopyInd(variant32_t *dest,const variant32_t *source){return oa_VariantCopy(dest,source);}
static uint32_t variant_integer(const variant32_t *v,int32_t*out){if(!v||!out)return E_INVALIDARG;switch(v->vt){case VT_EMPTY:*out=0;return S_OK;case VT_UI1:*out=(uint8_t)v->value;return S_OK;case VT_I2:*out=(int16_t)v->value;return S_OK;case VT_I4:*out=(int32_t)v->value;return S_OK;case VT_BOOL:*out=(int16_t)v->value?1:0;return S_OK;default:return DISP_E_BADVARTYPE;}}
static uint32_t WIN32_API oa_VariantChangeType(variant32_t*dest,const variant32_t*source,uint16_t flags UNUSED,uint16_t type){int32_t number;uint32_t hr;if(!dest||!source)return E_INVALIDARG;if(type==source->vt)return oa_VariantCopy(dest,source);hr=variant_integer(source,&number);if(hr!=S_OK)return hr;oa_VariantClear(dest);dest->vt=type;switch(type){case VT_I2:dest->value=(uint16_t)number;break;case VT_I4:dest->value=(uint32_t)number;break;case VT_UI1:dest->value=(uint8_t)number;break;case VT_BOOL:dest->value=number?0xFFFFU:0U;break;default:dest->vt=VT_EMPTY;return DISP_E_BADVARTYPE;}return S_OK;}

uint32_t win32_oleaut32_resolve(const char*name){
#define A(api) if(equal(name,#api))return(uint32_t)(uintptr_t)&oa_##api
 A(SysAllocString);A(SysAllocStringLen);A(SysAllocStringByteLen);A(SysReAllocString);A(SysReAllocStringLen);A(SysFreeString);A(SysStringLen);A(SysStringByteLen);A(VariantInit);A(VariantClear);A(VariantCopy);A(VariantCopyInd);A(VariantChangeType);
#undef A
 return 0;
}
uint32_t win32_oleaut32_resolve_ordinal(uint16_t ordinal){switch(ordinal){case 2:return(uint32_t)(uintptr_t)&oa_SysAllocString;case 3:return(uint32_t)(uintptr_t)&oa_SysReAllocString;case 4:return(uint32_t)(uintptr_t)&oa_SysAllocStringLen;case 5:return(uint32_t)(uintptr_t)&oa_SysReAllocStringLen;case 6:return(uint32_t)(uintptr_t)&oa_SysFreeString;case 7:return(uint32_t)(uintptr_t)&oa_SysStringLen;case 8:return(uint32_t)(uintptr_t)&oa_VariantInit;case 9:return(uint32_t)(uintptr_t)&oa_VariantClear;case 10:return(uint32_t)(uintptr_t)&oa_VariantCopy;case 11:return(uint32_t)(uintptr_t)&oa_VariantCopyInd;case 12:return(uint32_t)(uintptr_t)&oa_VariantChangeType;case 150:return(uint32_t)(uintptr_t)&oa_SysAllocStringByteLen;default:return 0;}}
