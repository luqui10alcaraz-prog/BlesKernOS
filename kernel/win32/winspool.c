#include "win32.h"
static bool equal(const char*a,const char*b){if(!a||!b)return false;while(*a&&*b&&*a==*b){a++;b++;}return *a==*b;}
static int WIN32_API spool_OpenPrinterA(const char*name UNUSED,void**printer,void*defaults UNUSED){if(printer)*printer=NULL;return 0;}
static int WIN32_API spool_ClosePrinter(void*printer UNUSED){return 1;}
static uint32_t WIN32_API spool_GetDefaultPrinterA(char*name,uint32_t*size){const char*none="";if(!size)return 0;if(!name||*size<1U){*size=1U;return 0;}name[0]=none[0];*size=1U;return 0;}
static int WIN32_API spool_EnumPrintersA(uint32_t flags UNUSED,char*name UNUSED,uint32_t level UNUSED,uint8_t*buffer UNUSED,uint32_t bytes UNUSED,uint32_t*needed,uint32_t*returned){if(needed)*needed=0;if(returned)*returned=0;return 1;}
static int32_t WIN32_API spool_DocumentPropertiesA(void*hwnd UNUSED,void*printer UNUSED,char*device UNUSED,void*out UNUSED,void*in UNUSED,uint32_t mode UNUSED){return -1;}
uint32_t win32_winspool_resolve(const char*name){
#define P(api) if(equal(name,#api))return(uint32_t)(uintptr_t)&spool_##api
 P(OpenPrinterA);P(ClosePrinter);P(GetDefaultPrinterA);P(EnumPrintersA);P(DocumentPropertiesA);
#undef P
 return 0;
}
