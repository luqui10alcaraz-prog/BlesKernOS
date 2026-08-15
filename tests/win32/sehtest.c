#include <windows.h>

#define TEST_SOFTWARE_EXCEPTION 0xE0421001UL

/* EXCEPTION_DISPOSITION values returned by a low-level x86 SEH handler. */
#define SEH_CONTINUE_EXECUTION 0
#define SEH_CONTINUE_SEARCH    1

typedef LONG (__cdecl *seh_handler_t)(PEXCEPTION_RECORD, void *,
                                      PCONTEXT, void *);
typedef struct seh_frame {
    struct seh_frame *previous;
    seh_handler_t handler;
} seh_frame_t;

static volatile LONG software_seen;
static volatile LONG divide_seen;
static volatile LONG vectored_seen;
static DWORD divide_recovery_eip;

static void write_text(const char *text) {
    DWORD written = 0;
    DWORD length = 0;
    while (text[length]) length++;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, length, &written, 0);
}

static LONG WINAPI vectored_handler(PEXCEPTION_POINTERS pointers) {
    if (pointers && pointers->ExceptionRecord) vectored_seen++;
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG __cdecl frame_handler(PEXCEPTION_RECORD record,
                                  void *frame_unused,
                                  PCONTEXT context,
                                  void *dispatcher_unused) {
    (void)frame_unused;
    (void)dispatcher_unused;
    if (!record || !context) return SEH_CONTINUE_SEARCH;
    if (record->ExceptionCode == TEST_SOFTWARE_EXCEPTION) {
        software_seen = 1;
        return SEH_CONTINUE_EXECUTION;
    }
    if (record->ExceptionCode == EXCEPTION_INT_DIVIDE_BY_ZERO) {
        divide_seen = 1;
        context->Eip = divide_recovery_eip;
        return SEH_CONTINUE_EXECUTION;
    }
    return SEH_CONTINUE_SEARCH;
}

void entry(void) {
    seh_frame_t frame;
    seh_frame_t *old_frame;
    PVOID vectored;
    volatile LONG zero = 0;
    ULONG_PTR argument = 0x12345678UL;

    __asm__ volatile ("movl %%fs:0,%0" : "=r"(old_frame));
    frame.previous = old_frame;
    frame.handler = frame_handler;
    __asm__ volatile ("movl %0,%%fs:0" : : "r"(&frame) : "memory");

    vectored = AddVectoredExceptionHandler(1, vectored_handler);
    SetUnhandledExceptionFilter(0);

    RaiseException(TEST_SOFTWARE_EXCEPTION, 0, 1, &argument);
    if (!software_seen) {
        write_text("SEH software RaiseException FAILED!\r\n");
        ExitProcess(1);
    }

    /*
     * La etiqueta debe formar parte del CFG del compilador. Tomar simplemente
     * &&divide_recovered permite que GCC fusione/mueva la etiqueta y, con -Os,
     * puede terminar apuntando otra vez al IDIV que produjo la excepcion.
     * asm goto obliga a conservar un destino real posterior al IDIV.
     */
    __asm__ goto volatile (
        "leal %l[divide_recovered],%%ecx\n\t"
        "movl %%ecx,(%0)\n\t"
        "movl $100,%%eax\n\t"
        "xorl %%edx,%%edx\n\t"
        "idivl %1\n\t"
        :
        : "r"(&divide_recovery_eip), "r"(zero)
        : "eax", "ecx", "edx", "memory", "cc"
        : divide_recovered);
    write_text("SEH hardware divide FAILED!\r\n");
    ExitProcess(2);

divide_recovered:
    __asm__ volatile ("movl %0,%%fs:0" : : "r"(old_frame) : "memory");
    if (vectored) RemoveVectoredExceptionHandler(vectored);

    if (!divide_seen || vectored_seen < 2) {
        write_text("SEH dispatch counters FAILED!\r\n");
        ExitProcess(3);
    }
    write_text("Win32 SEH software + CPU exception dispatch OK on BlesKernOS!\r\n");
    ExitProcess(0);
}
