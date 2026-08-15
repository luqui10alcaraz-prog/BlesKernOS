#include "win32.h"
static bool equal(const char*a,const char*b){if(!a||!b)return false;while(*a&&*b&&*a==*b){a++;b++;}return *a==*b;}
static void*WIN32_API imm_ImmGetContext(void*hwnd UNUSED){return NULL;}
static int WIN32_API imm_ImmReleaseContext(void*hwnd UNUSED,void*context UNUSED){return 1;}
static void*WIN32_API imm_ImmAssociateContext(void*hwnd UNUSED,void*context UNUSED){return NULL;}
static int WIN32_API imm_ImmGetOpenStatus(void*context UNUSED){return 0;}
static int WIN32_API imm_ImmSetOpenStatus(void*context UNUSED,int open UNUSED){return 1;}
static int WIN32_API imm_ImmIsIME(void*layout UNUSED){return 0;}
static int WIN32_API imm_ImmGetCompositionStringA(void*context UNUSED,uint32_t index UNUSED,void*buffer UNUSED,uint32_t size UNUSED){return 0;}
uint32_t win32_imm32_resolve(const char*name){
#define I(api) if(equal(name,#api))return(uint32_t)(uintptr_t)&imm_##api
 I(ImmGetContext);I(ImmReleaseContext);I(ImmAssociateContext);I(ImmGetOpenStatus);I(ImmSetOpenStatus);I(ImmIsIME);I(ImmGetCompositionStringA);
#undef I
 return 0;
}
