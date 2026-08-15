#include "win32.h"
#include "process.h"
#include "thread.h"
#include "../include/memory.h"
#include "../include/task.h"
#include "../include/pe_loader.h"
#include "../include/pit.h"
#include "../include/rtc.h"
#include "exception.h"
#include "../stdio.h"
#include "../string.h"
#include "../stdlib.h"
#include "../stdarg.h"

/* BLES_WINE_CRTDLL_LONGJMP_FIX_20260723 */
extern void win32_crt_longjmp(void *buffer, int32_t value)
    __attribute__((noreturn));

#define CRT_PROCESS_SLOTS TASK_MAX
#define CRT_MAX_ARGS 16U
#define CRT_COMMAND_LINE_CHARS 512U
#define CRT_ENV_ENTRIES 4U
#define CRT_ATEXIT_SLOTS 32U
#define CRT_THREAD_SLOTS TASK_MAX
#define CRT_EINVAL 22
#define CRT_MB_CP_SBCS 0
#define CRT_MB_CP_OEM (-2)
#define CRT_MB_CP_ANSI (-3)
#define CRT_MB_CP_LOCALE (-4)
#define CRT_FD_SLOTS 32U

typedef void (*crt_void_function_t)(void);
typedef int (*crt_int_function_t)(void);
typedef void (*crt_signal_handler_t)(int);

typedef struct {
    uint32_t pid;
    bool initialized;
    int argc;
    int fmode;
    int commode;
    int app_type;
    int mb_cur_max;
    int mb_codepage;
    char command_line[CRT_COMMAND_LINE_CHARS];
    uint16_t wide_command_line[CRT_COMMAND_LINE_CHARS];
    char *argv[CRT_MAX_ARGS + 1U];
    uint16_t *wide_argv[CRT_MAX_ARGS + 1U];
    char **argv_value;
    uint16_t **wide_argv_value;
    char *acmdln_value;
    uint16_t *wcmdln_value;
    char *environment[CRT_ENV_ENTRIES];
    uint16_t *wide_environment[CRT_ENV_ENTRIES];
    char **environment_value;
    uint16_t **wide_environment_value;
    char env_path[64];
    char env_temp[32];
    uint16_t wide_env_path[64];
    uint16_t wide_env_temp[32];
    crt_void_function_t atexit_handlers[CRT_ATEXIT_SLOTS];
    uint32_t atexit_count;
    crt_signal_handler_t signal_handlers[8];
    void *file_handles[CRT_FD_SLOTS];
    uint32_t random_seed;
} crt_process_state_t;

static crt_process_state_t crt_processes[CRT_PROCESS_SLOTS];

typedef struct {
    uint32_t process_id;
    uint32_t thread_id;
    int errno_value;
    uint32_t doserrno_value;
    char *strtok_next;
} crt_thread_state_t;

/* errno y _doserrno son privados por hilo en MSVCRT. La tabla es estable: las
 * funciones devuelven punteros a sus campos, tal como esperan MinGW y Wine. */
static crt_thread_state_t crt_threads[CRT_THREAD_SLOTS];


/* Layout binario de FILE usado por MSVCRT clasico de 32 bits. MinGW importa
 * _iob como un array de tres estructuras de 32 bytes y calcula stdout/stderr
 * mediante aritmetica de punteros sobre ese array. Los wrappers de stdio que
 * aparecen mas abajo traducen esas direcciones a los FILE nativos del OS. */
typedef struct {
    char *ptr;
    int count;
    char *base;
    int flags;
    int file;
    int char_buffer;
    int buffer_size;
    char *temporary_name;
} crt_iobuf_t;

static crt_iobuf_t crt_data_iob[3] = {
    {NULL, 0, NULL, 0x0001, 0, 0, 0, NULL},
    {NULL, 0, NULL, 0x0002, 1, 0, 0, NULL},
    {NULL, 0, NULL, 0x0002, 2, 0, 0, NULL},
};

typedef struct {
    char *decimal_point;
    char *thousands_sep;
    char *grouping;
    char *int_curr_symbol;
    char *currency_symbol;
    char *mon_decimal_point;
    char *mon_thousands_sep;
    char *mon_grouping;
    char *positive_sign;
    char *negative_sign;
    char int_frac_digits;
    char frac_digits;
    char p_cs_precedes;
    char p_sep_by_space;
    char n_cs_precedes;
    char n_sep_by_space;
    char p_sign_posn;
    char n_sign_posn;
    char int_p_cs_precedes;
    char int_p_sep_by_space;
    char int_n_cs_precedes;
    char int_n_sep_by_space;
    char int_p_sign_posn;
    char int_n_sign_posn;
} crt_lconv_t;

static char crt_locale_c[] = "C";
static char crt_locale_dot[] = ".";
static char crt_locale_empty[] = "";
static char crt_locale_grouping[] = "";
static crt_lconv_t crt_locale = {
    crt_locale_dot, crt_locale_empty, crt_locale_grouping,
    crt_locale_empty, crt_locale_empty, crt_locale_empty,
    crt_locale_empty, crt_locale_grouping, crt_locale_empty,
    crt_locale_empty,
    127, 127, 127, 127, 127, 127, 127, 127,
    127, 127, 127, 127, 127, 127
};

/*
 * MSVCRT tambien exporta varios datos, no solo funciones. Los PE importan
 * la direccion de estas variables a traves de la IAT. En el modelo actual
 * sin espacios de direcciones separados se mantiene una vista global del
 * proceso Win32 que esta ejecutando el CRT.
 */
static uint32_t crt_import_data_pid;
static int crt_data_argc;
static int crt_data_fmode = 0x4000;
static int crt_data_commode;
static int crt_data_mb_cur_max = 1;
static char **crt_data_argv;
static uint16_t **crt_data_wargv;
static char **crt_data_environ;
static uint16_t **crt_data_wenviron;
static char **crt_data_initenv;
static uint16_t **crt_data_winitenv;
static char *crt_data_acmdln;
static uint16_t *crt_data_wcmdln;
static void *crt_data_aexit_rtn_dll;
static uint16_t crt_ctype_storage[384];
static uint16_t *crt_ctype_pointer = &crt_ctype_storage[128];
static uint8_t crt_mbctype_storage[257];
static uint8_t *crt_mbctype_pointer = &crt_mbctype_storage[1];
static bool crt_ctype_initialized;

static void crt_sync_import_data(crt_process_state_t *state) {
    if (!state) return;
    crt_import_data_pid = state->pid;
    crt_data_argc = state->argc;
    crt_data_fmode = state->fmode;
    crt_data_commode = state->commode;
    crt_data_mb_cur_max = state->mb_cur_max;
    crt_data_argv = state->argv;
    crt_data_wargv = state->wide_argv;
    crt_data_environ = state->environment;
    crt_data_wenviron = state->wide_environment;
    crt_data_initenv = state->environment;
    crt_data_winitenv = state->wide_environment;
    crt_data_acmdln = state->command_line;
    crt_data_wcmdln = state->wide_command_line;
}

static bool equal(const char *a,const char *b){if(!a||!b)return false;while(*a&&*b&&*a==*b){a++;b++;}return *a==*b;}
static void *crt_calloc(size_t count,size_t size){if(count && size>0xFFFFFFFFU/count)return NULL;return kzalloc(count*size);}
static int crt_sprintf(char *out,const char *fmt,...){va_list a;int n;va_start(a,fmt);n=vsnprintf(out,0x7FFFFFFFU,fmt,a);va_end(a);return n;}
static int crt_vsprintf(char *out,const char *fmt,va_list args){return vsnprintf(out,0x7FFFFFFFU,fmt,args);}
static int crt_snprintf(char *out,size_t size,const char *fmt,...){va_list a;int n;va_start(a,fmt);n=vsnprintf(out,size,fmt,a);va_end(a);return n;}
static char *crt_gcvt(double value,int digits,char *buffer){union{double d;uint64_t u;}bits={value};char*out=buffer;double n;int exponent=0,precision,index=0,decimal_at;if(!buffer)return NULL;if((bits.u&0x7FFFFFFFFFFFFFFFULL)>0x7FF0000000000000ULL){kstrcpy(buffer,"1.#QNAN");return buffer;}if((bits.u&0x7FFFFFFFFFFFFFFFULL)==0x7FF0000000000000ULL){if(bits.u>>63)*out++='-';kstrcpy(out,"1.#INF");return buffer;}if(value<0.0){*out++='-';n=-value;}else n=value;precision=digits<1?1:(digits>17?17:digits);if(n==0.0){*out++='0';*out=0;return buffer;}while(n>=10.0&&exponent<308){n/=10.0;exponent++;}while(n<1.0&&exponent>-308){n*=10.0;exponent--;}double round=0.5;for(int i=1;i<precision;i++)round/=10.0;n+=round;if(n>=10.0){n/=10.0;exponent++;}uint8_t decimal[17];for(int i=0;i<precision;i++){int digit=(int)n;if(digit<0)digit=0;if(digit>9)digit=9;decimal[i]=(uint8_t)digit;n=(n-(double)digit)*10.0;}bool scientific=exponent<-4||exponent>=precision;if(scientific){*out++=(char)('0'+decimal[index++]);if(precision>1)*out++='.';while(index<precision)*out++=(char)('0'+decimal[index++]);while(out>buffer&&out[-1]=='0')out--;if(out>buffer&&out[-1]=='.')out--;*out++='e';*out++=exponent<0?'-':'+';int e=exponent<0?-exponent:exponent;if(e>=100)*out++=(char)('0'+(e/100)%10);*out++=(char)('0'+(e/10)%10);*out++=(char)('0'+e%10);*out=0;return buffer;}decimal_at=exponent+1;if(decimal_at<=0){*out++='0';*out++='.';for(int i=0;i<-decimal_at;i++)*out++='0';while(index<precision)*out++=(char)('0'+decimal[index++]);}else{for(int i=0;i<decimal_at;i++)*out++=index<precision?(char)('0'+decimal[index++]):'0';if(index<precision){*out++='.';while(index<precision)*out++=(char)('0'+decimal[index++]);}}while(out>buffer&&out[-1]=='0'&&out>buffer+decimal_at+1)out--;if(out>buffer&&out[-1]=='.')out--;*out=0;return buffer;}
static long crt_strtol(const char *s,char **end,int base){long value=0,sign=1;const char *p=s;if(!p)return 0;while(*p==' '||*p=='\t')p++;if(*p=='-'){sign=-1;p++;}else if(*p=='+')p++;if(base==0){base=10;if(p[0]=='0'&&(p[1]=='x'||p[1]=='X')){base=16;p+=2;}}while(*p){int d=*p>='0'&&*p<='9'?*p-'0':(*p>='a'&&*p<='z'?*p-'a'+10:(*p>='A'&&*p<='Z'?*p-'A'+10:-1));if(d<0||d>=base)break;value=value*base+d;p++;}if(end)*end=(char*)p;return value*sign;}
static void crt_exit(int status UNUSED) NORETURN; static void crt_exit(int status UNUSED){task_exit();}
static void crt_abort(void) NORETURN; static void crt_abort(void){task_exit();}
static int *crt_errno(void);


static FILE *crt_native_stream(void *stream) {
    uintptr_t value = (uintptr_t)stream;
    uintptr_t base = (uintptr_t)&crt_data_iob[0];
    uintptr_t end = base + sizeof(crt_data_iob);
    uint32_t index;

    if (value >= base && value < end &&
        ((value - base) % sizeof(crt_iobuf_t)) == 0U) {
        index = (uint32_t)((value - base) / sizeof(crt_iobuf_t));
        if (index == 0U) return stdin;
        if (index == 1U) return stdout;
        if (index == 2U) return stderr;
    }
    return (FILE *)stream;
}

static int crt_vfprintf(void *stream, const char *format, va_list arguments) {
    FILE *native = crt_native_stream(stream);
    return native ? vfprintf(native, format, arguments) : -1;
}

static int crt_fprintf(void *stream, const char *format, ...) {
    va_list arguments;
    int result;
    va_start(arguments, format);
    result = crt_vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

static int crt_fputc(int character, void *stream) {
    unsigned char byte = (unsigned char)character;
    FILE *native = crt_native_stream(stream);
    if (!native) return EOF;
    return fwrite(&byte, 1U, 1U, native) == 1U ? (int)byte : EOF;
}

static size_t crt_fwrite(const void *buffer, size_t size, size_t count,
                         void *stream) {
    FILE *native = crt_native_stream(stream);
    return native ? fwrite(buffer, size, count, native) : 0U;
}

static int crt_fflush(void *stream) {
    if (!stream) {
        int a = fflush(stdout);
        int b = fflush(stderr);
        return (a == 0 && b == 0) ? 0 : EOF;
    }
    FILE *native = crt_native_stream(stream);
    return native ? fflush(native) : EOF;
}

/* MSVCRT exposes fpos_t as a signed 64-bit offset, including its old
 * versioned DLLs.  Keep these wrappers separate from the kernel libc symbols
 * so streams from the exported _iob table are translated consistently. */
static int crt_fgetpos(void *stream, int64_t *position) {
    FILE *native = crt_native_stream(stream);
    long value;
    if (!native || !position || (value = ftell(native)) < 0L) return -1;
    *position = value;
    return 0;
}

static int crt_fsetpos(void *stream, const int64_t *position) {
    FILE *native = crt_native_stream(stream);
    if (!native || !position || *position < -2147483648LL ||
        *position > 2147483647LL) return -1;
    return fseek(native, (long)*position, 0);
}

static double crt_qnan(void) {
    union { uint64_t bits; double value; } number;
    number.bits = 0x7FF8000000000000ULL;
    return number.value;
}

static double crt_infinity(bool negative) {
    union { uint64_t bits; double value; } number;
    number.bits = negative ? 0xFFF0000000000000ULL
                           : 0x7FF0000000000000ULL;
    return number.value;
}

static double crt_fabs(double value) { return value < 0.0 ? -value : value; }

static double crt_sqrt(double value) {
    double result;
    __asm__ volatile("fldl %1; fsqrt; fstpl %0"
                     : "=m"(result) : "m"(value));
    return result;
}

static double crt_atan2_internal(double y, double x) {
    double result;
    __asm__ volatile("fldl %1; fldl %2; fpatan; fstpl %0"
                     : "=m"(result) : "m"(y), "m"(x));
    return result;
}

static double crt_atan(double value) {
    return crt_atan2_internal(value, 1.0);
}

static double crt_asin(double value) {
    double rest;
    if (value < -1.0 || value > 1.0) {
        int *error = crt_errno();
        if (error) *error = 33; /* EDOM */
        return crt_qnan();
    }
    rest = 1.0 - value * value;
    if (rest < 0.0) rest = 0.0;
    return crt_atan2_internal(value, crt_sqrt(rest));
}

static double crt_acos(double value) {
    double rest;
    if (value < -1.0 || value > 1.0) {
        int *error = crt_errno();
        if (error) *error = 33;
        return crt_qnan();
    }
    rest = 1.0 - value * value;
    if (rest < 0.0) rest = 0.0;
    return crt_atan2_internal(crt_sqrt(rest), value);
}

static double crt_tan(double value) {
    double result;
    __asm__ volatile("fldl %1; fptan; fstp %%st(0); fstpl %0"
                     : "=m"(result) : "m"(value));
    return result;
}

static double crt_log10(double value) {
    double result;
    if (value < 0.0) {
        int *error = crt_errno();
        if (error) *error = 33;
        return crt_qnan();
    }
    if (value == 0.0) {
        int *error = crt_errno();
        if (error) *error = 34; /* ERANGE */
        return crt_infinity(true);
    }
    __asm__ volatile("fldlg2; fldl %1; fyl2x; fstpl %0"
                     : "=m"(result) : "m"(value));
    return result;
}

static double crt_pow2_integer(int exponent) {
    union { uint64_t bits; double value; } number;
    if (exponent > 1023) return crt_infinity(false);
    if (exponent < -1022) return 0.0;
    number.bits = (uint64_t)(exponent + 1023) << 52;
    return number.value;
}

static double crt_exp_internal(double value) {
    const double inv_ln2 = 1.4426950408889634074;
    const double ln2 = 0.69314718055994530942;
    int exponent;
    double reduced, term, sum;

    if (value > 709.0) return crt_infinity(false);
    if (value < -745.0) return 0.0;
    exponent = value >= 0.0
        ? (int)(value * inv_ln2 + 0.5)
        : (int)(value * inv_ln2 - 0.5);
    reduced = value - (double)exponent * ln2;
    term = 1.0;
    sum = 1.0;
    for (int i = 1; i <= 12; i++) {
        term *= reduced / (double)i;
        sum += term;
    }
    return sum * crt_pow2_integer(exponent);
}

static double crt_sinh(double value) {
    double magnitude = crt_fabs(value);
    double exponential = crt_exp_internal(magnitude);
    double result = 0.5 * (exponential - 1.0 / exponential);
    return value < 0.0 ? -result : result;
}

static double crt_cosh(double value) {
    double exponential = crt_exp_internal(crt_fabs(value));
    return 0.5 * (exponential + 1.0 / exponential);
}

static double crt_tanh(double value) {
    double exponential;
    if (value > 20.0) return 1.0;
    if (value < -20.0) return -1.0;
    exponential = crt_exp_internal(2.0 * value);
    return (exponential - 1.0) / (exponential + 1.0);
}

static int crt_ismbblead(unsigned int character UNUSED) { return 0; }

static char *crt_setlocale(int category UNUSED, const char *locale_name) {
    if (!locale_name || !*locale_name || equal(locale_name, "C") ||
        equal(locale_name, "POSIX")) return crt_locale_c;
    return NULL;
}

static crt_lconv_t *crt_localeconv(void) { return &crt_locale; }

static size_t crt_wcslen(const uint16_t *text) {
    size_t length = 0U;
    if (!text) return 0U;
    while (text[length]) length++;
    return length;
}

static char *crt_strrev(char *text) {
    size_t left = 0U, right;
    char temporary;
    if (!text) return NULL;
    right = strlen(text);
    if (right == 0U) return text;
    right--;
    while (left < right) {
        temporary = text[left];
        text[left++] = text[right];
        text[right--] = temporary;
    }
    return text;
}

static char *crt_strerror(int error) {
    switch (error) {
        case 0: return "No error";
        case 2: return "No such file or directory";
        case 12: return "Not enough memory";
        case 13: return "Permission denied";
        case 22: return "Invalid argument";
        case 33: return "Domain error";
        case 34: return "Result too large";
        default: return "Unknown error";
    }
}

static uint32_t ascii_to_wide(const char *source, uint16_t *destination,
                              uint32_t capacity) {
    uint32_t length = 0U;
    if (!destination || capacity == 0U) return 0U;
    while (source && source[length] && length + 1U < capacity) {
        destination[length] = (uint8_t)source[length];
        length++;
    }
    destination[length] = 0U;
    return length;
}

static void crt_parse_command_line(crt_process_state_t *state,
                                   const char *source) {
    char *read;
    char *write;
    bool quoted = false;

    if (!state) return;
    kstrncpy(state->command_line, source ? source : "",
             sizeof(state->command_line) - 1U);
    state->command_line[sizeof(state->command_line) - 1U] = '\0';
    read = state->command_line;
    state->argc = 0;
    while (*read && state->argc < (int)CRT_MAX_ARGS) {
        while (*read == ' ' || *read == '\t') read++;
        if (!*read) break;
        state->argv[state->argc++] = read;
        write = read;
        quoted = false;
        while (*read) {
            if (*read == '"') {
                quoted = !quoted;
                read++;
                continue;
            }
            if (!quoted && (*read == ' ' || *read == '\t')) break;
            *write++ = *read++;
        }
        *write = '\0';
        while (*read == ' ' || *read == '\t') read++;
    }
    state->argv[state->argc] = NULL;

    if (state->argc == 0) {
        const char *image = win32_process_current_image_path();
        kstrncpy(state->command_line, image ? image : "", sizeof(state->command_line) - 1U);
        state->command_line[sizeof(state->command_line) - 1U] = '\0';
        state->argv[0] = state->command_line;
        state->argv[1] = NULL;
        state->argc = 1;
    }

    {
        uint32_t wide_cursor = 0U;
        for (int i = 0; i < state->argc; i++) {
            uint32_t remaining = CRT_COMMAND_LINE_CHARS - wide_cursor;
            uint32_t length;
            if (remaining <= 1U) {
                state->wide_argv[i] = NULL;
                state->argc = i;
                break;
            }
            state->wide_argv[i] = &state->wide_command_line[wide_cursor];
            length = ascii_to_wide(state->argv[i], state->wide_argv[i], remaining);
            wide_cursor += length + 1U;
        }
        state->wide_argv[state->argc] = NULL;
    }
}

static void crt_initialize_state(crt_process_state_t *state, uint32_t pid) {
    if (!state) return;
    kmemset(state, 0, sizeof(*state));
    state->pid = pid;
    state->fmode = 0x4000; /* _O_TEXT */
    state->commode = 0;
    state->mb_cur_max = 1;
    state->mb_codepage = 1252;
    crt_data_aexit_rtn_dll = (void *)(uintptr_t)&crt_exit;
    state->random_seed = pid ^ 0xA5A55A5AU;

    crt_parse_command_line(state, win32_process_current_command_line());

    kstrncpy(state->env_path, "PATH=C:\\SYSTEM;C:\\SYSTEM\\WIN32", sizeof(state->env_path) - 1U);
    kstrncpy(state->env_temp, "TEMP=C:\\TEMP", sizeof(state->env_temp) - 1U);
    ascii_to_wide(state->env_path, state->wide_env_path,
                  sizeof(state->wide_env_path) / sizeof(state->wide_env_path[0]));
    ascii_to_wide(state->env_temp, state->wide_env_temp,
                  sizeof(state->wide_env_temp) / sizeof(state->wide_env_temp[0]));
    state->environment[0] = state->env_path;
    state->environment[1] = state->env_temp;
    state->environment[2] = NULL;
    state->wide_environment[0] = state->wide_env_path;
    state->wide_environment[1] = state->wide_env_temp;
    state->wide_environment[2] = NULL;
    state->environment_value = state->environment;
    state->wide_environment_value = state->wide_environment;
    state->initialized = true;
    crt_sync_import_data(state);
}

static crt_thread_state_t *crt_current_thread_state(void) {
    uint32_t process_id = task_current_process_id();
    uint32_t thread_id = task_current_pid();
    crt_thread_state_t *free_state = NULL;

    task_preempt_disable();
    for (uint32_t i = 0; i < CRT_THREAD_SLOTS; i++) {
        if (crt_threads[i].process_id == process_id &&
            crt_threads[i].thread_id == thread_id) {
            task_preempt_enable();
            return &crt_threads[i];
        }
        if (!free_state && crt_threads[i].thread_id == 0U)
            free_state = &crt_threads[i];
    }
    if (free_state) {
        kmemset(free_state, 0, sizeof(*free_state));
        free_state->process_id = process_id;
        free_state->thread_id = thread_id;
    }
    task_preempt_enable();
    return free_state;
}

static crt_process_state_t *crt_current_state(void) {
    uint32_t pid = task_current_process_id();
    crt_process_state_t *free_state = NULL;

    task_preempt_disable();
    for (uint32_t i = 0; i < CRT_PROCESS_SLOTS; i++) {
        if (crt_processes[i].pid == pid && crt_processes[i].initialized) {
            crt_sync_import_data(&crt_processes[i]);
            task_preempt_enable();
            return &crt_processes[i];
        }
        if (!free_state && crt_processes[i].pid == 0U) free_state = &crt_processes[i];
    }
    if (free_state) crt_initialize_state(free_state, pid);
    task_preempt_enable();
    return free_state;
}

void win32_msvcrt_cleanup_thread(uint32_t tid) {
    task_preempt_disable();
    for (uint32_t i = 0; i < CRT_THREAD_SLOTS; i++) {
        if (crt_threads[i].thread_id == tid) {
            kmemset(&crt_threads[i], 0, sizeof(crt_threads[i]));
            break;
        }
    }
    task_preempt_enable();
}

void win32_msvcrt_cleanup_process(uint32_t pid) {
    typedef int (WIN32_API *close_handle_t)(void *);
    close_handle_t close_handle = (close_handle_t)(uintptr_t)
        pe_win32_resolve_export("KERNEL32.DLL", "CloseHandle");
    for (uint32_t i = 0; i < CRT_PROCESS_SLOTS; i++) {
        if (crt_processes[i].pid != pid) continue;
        if (close_handle) for (uint32_t fd = 0; fd < CRT_FD_SLOTS; fd++)
            if (crt_processes[i].file_handles[fd])
                (void)close_handle(crt_processes[i].file_handles[fd]);
        break;
    }
    task_preempt_disable();
    for (uint32_t i = 0; i < CRT_THREAD_SLOTS; i++) {
        if (crt_threads[i].process_id == pid)
            kmemset(&crt_threads[i], 0, sizeof(crt_threads[i]));
    }
    for (uint32_t i = 0; i < CRT_PROCESS_SLOTS; i++) {
        if (crt_processes[i].pid != pid) continue;
        kmemset(&crt_processes[i], 0, sizeof(crt_processes[i]));
        if (crt_import_data_pid == pid) {
            crt_import_data_pid = 0U;
            crt_data_argc = 0;
            crt_data_argv = NULL;
            crt_data_wargv = NULL;
            crt_data_environ = NULL;
            crt_data_wenviron = NULL;
            crt_data_initenv = NULL;
            crt_data_winitenv = NULL;
            crt_data_acmdln = NULL;
            crt_data_wcmdln = NULL;
        }
        break;
    }
    task_preempt_enable();
}

static int crt_getmainargs(int *argc, char ***argv, char ***environment,
                           int expand_wildcards UNUSED, void *startup_info UNUSED) {
    crt_process_state_t *state = crt_current_state();
    if (!state) return -1;
    if (argc) *argc = state->argc;
    if (argv) *argv = state->argv;
    if (environment) *environment = state->environment;
    crt_sync_import_data(state);
    return 0;
}

static int crt_wgetmainargs(int *argc, uint16_t ***argv, uint16_t ***environment,
                            int expand_wildcards UNUSED, void *startup_info UNUSED) {
    crt_process_state_t *state = crt_current_state();
    if (!state) return -1;
    if (argc) *argc = state->argc;
    if (argv) *argv = state->wide_argv;
    if (environment) *environment = state->wide_environment;
    crt_sync_import_data(state);
    return 0;
}

static int *crt_p_argc(void) {
    crt_process_state_t *state = crt_current_state();
    return state ? &state->argc : NULL;
}

static char ***crt_p_argv(void) {
    crt_process_state_t *state = crt_current_state();
    if (!state) return NULL;
    state->argv_value = state->argv;
    return &state->argv_value;
}

static uint16_t ***crt_p_wargv(void) {
    crt_process_state_t *state = crt_current_state();
    if (!state) return NULL;
    state->wide_argv_value = state->wide_argv;
    return &state->wide_argv_value;
}

static char ***crt_p_environ(void) {
    crt_process_state_t *state = crt_current_state();
    return state ? &state->environment_value : NULL;
}

static uint16_t ***crt_p_wenviron(void) {
    crt_process_state_t *state = crt_current_state();
    return state ? &state->wide_environment_value : NULL;
}

static char **crt_p_acmdln(void) {
    crt_process_state_t *state = crt_current_state();
    if (!state) return NULL;
    state->acmdln_value = state->command_line;
    return &state->acmdln_value;
}

static uint16_t **crt_p_wcmdln(void) {
    crt_process_state_t *state = crt_current_state();
    if (!state) return NULL;
    state->wcmdln_value = state->wide_command_line;
    return &state->wcmdln_value;
}

static int *crt_p_fmode(void) {
    crt_process_state_t *state = crt_current_state();
    return state ? &state->fmode : NULL;
}

static int *crt_p_commode(void) {
    crt_process_state_t *state = crt_current_state();
    return state ? &state->commode : NULL;
}

static int *crt_p_mb_cur_max(void) {
    crt_process_state_t *state = crt_current_state();
    return state ? &state->mb_cur_max : NULL;
}

static int *crt_errno(void) {
    crt_thread_state_t *state = crt_current_thread_state();
    return state ? &state->errno_value : NULL;
}

static uint32_t *crt_doserrno(void) {
    crt_thread_state_t *state = crt_current_thread_state();
    return state ? &state->doserrno_value : NULL;
}

static int crt_get_errno(int *value) {
    int *current = crt_errno();
    if (!value || !current) return CRT_EINVAL;
    *value = *current;
    return 0;
}

static int crt_set_errno(int value) {
    int *current = crt_errno();
    if (!current) return CRT_EINVAL;
    *current = value;
    return 0;
}

static int crt_get_doserrno(uint32_t *value) {
    uint32_t *current = crt_doserrno();
    if (!value || !current) return CRT_EINVAL;
    *value = *current;
    return 0;
}

static int crt_set_doserrno(uint32_t value) {
    uint32_t *current = crt_doserrno();
    if (!current) return CRT_EINVAL;
    *current = value;
    return 0;
}

static void crt_set_app_type(int app_type) {
    crt_process_state_t *state = crt_current_state();
    if (state) state->app_type = app_type;
}

static void crt_setusermatherr(void *handler UNUSED) {}

static void crt_initterm(crt_void_function_t *first, crt_void_function_t *last) {
    if (!first || !last) return;
    while (first < last) {
        crt_void_function_t function = *first++;
        if (function) function();
    }
}

static int crt_initterm_e(crt_int_function_t *first, crt_int_function_t *last) {
    if (!first || !last) return 0;
    while (first < last) {
        crt_int_function_t function = *first++;
        int result;
        if (!function) continue;
        result = function();
        if (result) return result;
    }
    return 0;
}

static crt_void_function_t crt_onexit(crt_void_function_t function) {
    crt_process_state_t *state = crt_current_state();
    if (!state || !function || state->atexit_count >= CRT_ATEXIT_SLOTS) return NULL;
    state->atexit_handlers[state->atexit_count++] = function;
    return function;
}

static int crt_atexit(crt_void_function_t function) {
    return crt_onexit(function) ? 0 : -1;
}

static void crt_cexit(void) {
    crt_process_state_t *state = crt_current_state();
    if (!state) return;
    while (state->atexit_count) {
        crt_void_function_t function = state->atexit_handlers[--state->atexit_count];
        state->atexit_handlers[state->atexit_count] = NULL;
        if (function) function();
    }
}

static void crt_c_exit(void) {}
static void crt_amsg_exit(int error UNUSED) { crt_abort(); }

static crt_signal_handler_t crt_signal(int signal_number,
                                       crt_signal_handler_t handler) {
    crt_process_state_t *state = crt_current_state();
    crt_signal_handler_t previous = NULL;
    uint32_t index = (uint32_t)signal_number & 7U;
    if (!state) return (crt_signal_handler_t)(uintptr_t)-1;
    previous = state->signal_handlers[index];
    state->signal_handlers[index] = handler;
    return previous;
}

static int crt_raise(int signal_number) {
    crt_process_state_t *state = crt_current_state();
    crt_signal_handler_t handler;
    uint32_t index = (uint32_t)signal_number & 7U;
    if (!state) return -1;
    handler = state->signal_handlers[index];
    if (handler && handler != (crt_signal_handler_t)(uintptr_t)1U &&
        handler != (crt_signal_handler_t)(uintptr_t)-1) handler(signal_number);
    return 0;
}

static int crt_get_pgmptr(char **value) {
    crt_process_state_t *state = crt_current_state();
    if (!value || !state) return -1;
    *value = state->argv[0];
    return 0;
}

static int crt_get_wpgmptr(uint16_t **value) {
    crt_process_state_t *state = crt_current_state();
    if (!value || !state) return -1;
    *value = state->wide_argv[0];
    return 0;
}


static long crt_atol(const char *text) { return (long)crt_strtol(text, NULL, 10); }
static char *crt_strrchr(const char *text, int value) {
    const char *last = NULL;
    if (!text) return NULL;
    do { if ((uint8_t)*text == (uint8_t)value) last = text; } while (*text++);
    return (char *)last;
}
static void *crt_memchr(const void *memory, int value, size_t count) {
    const uint8_t *bytes = (const uint8_t *)memory;
    if (!bytes) return NULL;
    for (size_t i = 0; i < count; i++)
        if (bytes[i] == (uint8_t)value) return (void *)(uintptr_t)(bytes + i);
    return NULL;
}
static int crt_isdigit(int c) { return c >= '0' && c <= '9'; }
static int crt_isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
static int crt_isalpha(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
static int crt_isalnum(int c) { return crt_isalpha(c) || crt_isdigit(c); }
static int crt_isprint(int c) { return c >= 0x20 && c <= 0x7e; }


static int crt_tolower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int crt_toupper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
static int crt_stricmp(const char *a, const char *b) {
    int ca, cb;
    if (!a || !b) return a ? 1 : (b ? -1 : 0);
    do { ca = crt_tolower((uint8_t)*a++); cb = crt_tolower((uint8_t)*b++); }
    while (ca && ca == cb);
    return ca - cb;
}
static int crt_strnicmp(const char *a, const char *b, size_t count) {
    int ca = 0, cb = 0;
    if (!count) return 0;
    if (!a || !b) return a ? 1 : (b ? -1 : 0);
    while (count--) {
        ca = crt_tolower((uint8_t)*a++); cb = crt_tolower((uint8_t)*b++);
        if (ca != cb || !ca) break;
    }
    return ca - cb;
}
static char *crt_strdup(const char *text) {
    size_t size; char *copy;
    if (!text) return NULL;
    size = kstrlen(text) + 1U; copy = (char *)kmalloc(size);
    if (copy) kmemcpy(copy, text, size);
    return copy;
}
static char *crt_strupr(char *text) { if(text)for(char*p=text;*p;p++)*p=(char)crt_toupper((uint8_t)*p);return text; }
static char *crt_strlwr(char *text) { if(text)for(char*p=text;*p;p++)*p=(char)crt_tolower((uint8_t)*p);return text; }
/* BLES_WINE_CRTDLL_WCSTOMBS_BATCH_20260723
 *
 * CRTDLL de Win9x usa wchar_t de 16 bits. Esta conversión sigue la semántica
 * de wcstombs para el locale C: U+0000..U+00ff se convierten a un byte;
 * caracteres fuera del codepage se reemplazan por '?'.
 */
static size_t crt_wcstombs(char *destination, const uint16_t *source,
                           size_t count) {
    size_t converted = 0U;

    if (!source) return (size_t)-1;

    if (!destination) {
        while (source[converted]) converted++;
        return converted;
    }

    while (converted < count && source[converted]) {
        uint16_t value = source[converted];
        destination[converted] = value <= 0x00ffU ? (char)value : '?';
        converted++;
    }

    if (converted < count)
        destination[converted] = '\0';

    return converted;
}

static uint16_t *crt_wcscpy(uint16_t*d,const uint16_t*s){uint16_t*r=d;if(!d||!s)return d;while((*d++=*s++));return r;}
static uint16_t *crt_wcsncpy(uint16_t*d,const uint16_t*s,size_t n){uint16_t*r=d;if(!d||!s)return d;while(n&&*s){*d++=*s++;n--;}while(n--)*d++=0;return r;}
static uint16_t *crt_wcscat(uint16_t*d,const uint16_t*s){uint16_t*r=d;if(!d||!s)return d;while(*d)d++;while((*d++=*s++));return r;}
static int crt_wcscmp(const uint16_t*a,const uint16_t*b){if(!a||!b)return a?1:(b?-1:0);while(*a&&*a==*b){a++;b++;}return(int)*a-(int)*b;}
static int crt_wcsncmp(const uint16_t*a,const uint16_t*b,size_t n){if(!n)return 0;if(!a||!b)return a?1:(b?-1:0);while(n--&&*a&&*a==*b){if(!n)return 0;a++;b++;}return(int)*a-(int)*b;}
static uint16_t *crt_wcschr(const uint16_t*s,int c){if(!s)return NULL;do{if(*s==(uint16_t)c)return(uint16_t*)s;}while(*s++);return NULL;}
static uint16_t *crt_wcsrchr(const uint16_t*s,int c){const uint16_t*last=NULL;if(!s)return NULL;do{if(*s==(uint16_t)c)last=s;}while(*s++);return(uint16_t*)last;}
static int crt_wide_to_ansi(const uint16_t *wide, char *out, uint32_t cap) {uint32_t i=0;if(!wide||!out||!cap)return 0;while(wide[i]&&i+1U<cap){out[i]=wide[i]<=255U?(char)wide[i]:'?';i++;}out[i]=0;return wide[i]==0;}

static int crt_allocate_fd(crt_process_state_t *state, void *handle) {
    if (!state || !handle) return -1;
    for (uint32_t i = 0; i < CRT_FD_SLOTS; i++) if (!state->file_handles[i]) {
        state->file_handles[i] = handle; return (int)i + 3;
    }
    return -1;
}
static void *crt_handle_from_fd(int fd) {
    crt_process_state_t *state = crt_current_state();
    typedef void *(WIN32_API *get_std_handle_t)(int32_t);
    if (fd >= 3 && fd < (int)CRT_FD_SLOTS + 3 && state)
        return state->file_handles[(uint32_t)(fd - 3)];
    if (fd >= 0 && fd <= 2) {
        get_std_handle_t get_std = (get_std_handle_t)(uintptr_t)
            pe_win32_resolve_export("KERNEL32.DLL", "GetStdHandle");
        if (get_std) return get_std(fd == 0 ? -10 : (fd == 1 ? -11 : -12));
    }
    return NULL;
}
static int crt_open(const char *path, int flags, ...) {
    typedef void *(WIN32_API *create_file_t)(const char *,uint32_t,uint32_t,void*,uint32_t,uint32_t,void*);
    typedef uint32_t (WIN32_API *set_pointer_t)(void*,int32_t,int32_t*,uint32_t);
    typedef int (WIN32_API *close_handle_t)(void*);
    create_file_t create_file; set_pointer_t set_pointer; close_handle_t close_handle;
    crt_process_state_t *state = crt_current_state(); void *handle; uint32_t access, creation;
    if (!path || !state) { int*e=crt_errno();if(e)*e=CRT_EINVAL;return -1; }
    create_file=(create_file_t)(uintptr_t)pe_win32_resolve_export("KERNEL32.DLL","CreateFileA");
    set_pointer=(set_pointer_t)(uintptr_t)pe_win32_resolve_export("KERNEL32.DLL","SetFilePointer");
    close_handle=(close_handle_t)(uintptr_t)pe_win32_resolve_export("KERNEL32.DLL","CloseHandle");
    if(!create_file)return -1;
    access=(flags&2)?0xC0000000U:((flags&1)?0x40000000U:0x80000000U);
    if((flags&0x0100)&&(flags&0x0200))creation=2U;else if(flags&0x0100)creation=4U;else if(flags&0x0200)creation=5U;else creation=3U;
    handle=create_file(path,access,3U,NULL,creation,0x80U,NULL);
    if(!handle||handle==(void*)(uintptr_t)0xFFFFFFFFU)return -1;
    int fd=crt_allocate_fd(state,handle);if(fd<0){if(close_handle)close_handle(handle);return -1;}
    if((flags&0x0008)&&set_pointer)(void)set_pointer(handle,0,NULL,2U);
    return fd;
}
static int crt_wopen(const uint16_t *path,int flags,...){char ansi[512];if(!crt_wide_to_ansi(path,ansi,sizeof(ansi)))return-1;return crt_open(ansi,flags);}
static int crt_sopen(const char*path,int flags,int share,...){(void)share;return crt_open(path,flags);}
static int crt_close(int fd){typedef int(WIN32_API*close_handle_t)(void*);crt_process_state_t*state=crt_current_state();void*handle;if(fd<3||fd>=((int)CRT_FD_SLOTS+3)||!state)return fd<3?0:-1;handle=state->file_handles[fd-3];if(!handle)return-1;close_handle_t close_handle=(close_handle_t)(uintptr_t)pe_win32_resolve_export("KERNEL32.DLL","CloseHandle");if(close_handle&&!close_handle(handle))return-1;state->file_handles[fd-3]=NULL;return 0;}
static int crt_read_fd(int fd,void*buffer,uint32_t count){typedef int(WIN32_API*read_file_t)(void*,void*,uint32_t,uint32_t*,void*);void*handle=crt_handle_from_fd(fd);uint32_t read=0;read_file_t fn=(read_file_t)(uintptr_t)pe_win32_resolve_export("KERNEL32.DLL","ReadFile");if(!handle||!fn||!fn(handle,buffer,count,&read,NULL))return-1;return(int)read;}
static int crt_write_fd(int fd,const void*buffer,uint32_t count){void*handle=crt_handle_from_fd(fd);uint32_t written=0;if(!handle||!win32_file_write(handle,buffer,count,&written))return-1;return(int)written;}
static long crt_lseek(int fd,long offset,int origin){typedef uint32_t(WIN32_API*set_pointer_t)(void*,int32_t,int32_t*,uint32_t);void*handle=crt_handle_from_fd(fd);set_pointer_t fn=(set_pointer_t)(uintptr_t)pe_win32_resolve_export("KERNEL32.DLL","SetFilePointer");uint32_t result;if(!handle||!fn)return-1;result=fn(handle,(int32_t)offset,NULL,(uint32_t)origin);return result==0xFFFFFFFFU?-1:(long)result;}
static long crt_tell(int fd){return crt_lseek(fd,0,1);}
static long crt_filelength(int fd){long current=crt_tell(fd),end;if(current<0)return-1;end=crt_lseek(fd,0,2);(void)crt_lseek(fd,current,0);return end;}
static intptr_t crt_get_osfhandle(int fd){void*h=crt_handle_from_fd(fd);return h?(intptr_t)h:-1;}
static int crt_open_osfhandle(intptr_t handle,int flags UNUSED){return crt_allocate_fd(crt_current_state(),(void*)handle);}
static int crt_isatty(int fd){return fd>=0&&fd<=2;}
static int crt_commit(int fd){typedef int(WIN32_API*flush_t)(void*);void*h=crt_handle_from_fd(fd);flush_t fn=(flush_t)(uintptr_t)pe_win32_resolve_export("KERNEL32.DLL","FlushFileBuffers");return h&&fn&&fn(h)?0:-1;}
static int crt_unlink(const char*path){typedef int(WIN32_API*fn_t)(const char*);fn_t fn=(fn_t)(uintptr_t)pe_win32_resolve_export("KERNEL32.DLL","DeleteFileA");return fn&&fn(path)?0:-1;}
static int crt_wunlink(const uint16_t*path){char a[512];return crt_wide_to_ansi(path,a,sizeof(a))?crt_unlink(a):-1;}
static int crt_mkdir(const char*path){typedef int(WIN32_API*fn_t)(const char*,void*);fn_t fn=(fn_t)(uintptr_t)pe_win32_resolve_export("KERNEL32.DLL","CreateDirectoryA");return fn&&fn(path,NULL)?0:-1;}
static int crt_rmdir(const char*path){typedef int(WIN32_API*fn_t)(const char*);fn_t fn=(fn_t)(uintptr_t)pe_win32_resolve_export("KERNEL32.DLL","RemoveDirectoryA");return fn&&fn(path)?0:-1;}
static int crt_chdir(const char*path){typedef int(WIN32_API*fn_t)(const char*);fn_t fn=(fn_t)(uintptr_t)pe_win32_resolve_export("KERNEL32.DLL","SetCurrentDirectoryA");return fn&&fn(path)?0:-1;}
static int crt_rename(const char*a,const char*b){typedef int(WIN32_API*fn_t)(const char*,const char*);fn_t fn=(fn_t)(uintptr_t)pe_win32_resolve_export("KERNEL32.DLL","MoveFileA");return fn&&fn(a,b)?0:-1;}
static int crt_access(const char*path,int mode UNUSED){typedef uint32_t(WIN32_API*fn_t)(const char*);fn_t fn=(fn_t)(uintptr_t)pe_win32_resolve_export("KERNEL32.DLL","GetFileAttributesA");return fn&&fn(path)!=0xFFFFFFFFU?0:-1;}

static char *crt_getenv(const char *name){crt_process_state_t*s=crt_current_state();uint32_t n;if(!s||!name)return NULL;n=(uint32_t)kstrlen(name);for(uint32_t i=0;s->environment[i];i++){char*e=s->environment[i];if(crt_strnicmp(e,name,n)==0&&e[n]=='=')return e+n+1U;}return NULL;}
static int crt_putenv(const char *entry){crt_process_state_t*s=crt_current_state();if(!s||!entry)return-1;if(crt_strnicmp(entry,"PATH=",5U)==0)kstrncpy(s->env_path,entry,sizeof(s->env_path)-1U);else if(crt_strnicmp(entry,"TEMP=",5U)==0)kstrncpy(s->env_temp,entry,sizeof(s->env_temp)-1U);else return-1;ascii_to_wide(s->env_path,s->wide_env_path,sizeof(s->wide_env_path)/2U);ascii_to_wide(s->env_temp,s->wide_env_temp,sizeof(s->wide_env_temp)/2U);return 0;}
static void crt_srand(uint32_t seed){crt_process_state_t*s=crt_current_state();if(s)s->random_seed=seed;}
static int crt_rand(void){crt_process_state_t*s=crt_current_state();if(!s)return 0;s->random_seed=s->random_seed*214013U+2531011U;return(int)((s->random_seed>>16U)&0x7FFFU);}
static long crt_clock(void){uint32_t hz=pit_get_frequency_hz();return hz?(long)(((uint64_t)pit_get_ticks()*1000U)/hz):0;}
static bool crt_leap(uint32_t y){return(y%4U==0U&&y%100U!=0U)||y%400U==0U;}
static long crt_time(long *result){rtc_datetime_t dt;static const uint16_t before[12]={0,31,59,90,120,151,181,212,243,273,304,334};uint64_t days=0;if(!rtc_get_datetime(&dt)||dt.date.year<1970U)return-1;for(uint32_t y=1970U;y<dt.date.year;y++)days+=crt_leap(y)?366U:365U;days+=before[dt.date.month?dt.date.month-1U:0U];if(dt.date.month>2U&&crt_leap(dt.date.year))days++;if(dt.date.day)days+=dt.date.day-1U;long value=(long)(days*86400ULL+dt.time.hour*3600U+dt.time.minute*60U+dt.time.second);if(result)*result=value;return value;}
static void crt_qsort(void*base,size_t count,size_t size,int(*compare)(const void*,const void*)){uint8_t*bytes=(uint8_t*)base,*tmp;if(!base||!compare||!size||count<2U)return;tmp=(uint8_t*)kmalloc(size);if(!tmp)return;for(size_t i=1;i<count;i++){size_t j=i;kmemcpy(tmp,bytes+i*size,size);while(j&&compare(bytes+(j-1U)*size,tmp)>0){memmove(bytes+j*size,bytes+(j-1U)*size,size);j--;}kmemcpy(bytes+j*size,tmp,size);}kfree(tmp);}
static void *crt_bsearch(const void*key,const void*base,size_t count,size_t size,int(*compare)(const void*,const void*)){size_t low=0,high=count;while(low<high){size_t mid=low+(high-low)/2U;const void*item=(const uint8_t*)base+mid*size;int c=compare(key,item);if(c<0)high=mid;else if(c>0)low=mid+1U;else return(void*)item;}return NULL;}
static double crt_floor(double x){int64_t i=(int64_t)x;if((double)i>x)i--;return(double)i;}
static double crt_ceil(double x){int64_t i=(int64_t)x;if((double)i<x)i++;return(double)i;}
static double crt_fmod(double x,double y){if(y==0.0)return crt_qnan();int64_t q=(int64_t)(x/y);return x-(double)q*y;}
static int crt_purecall(void){crt_abort();return 0;}
static void *crt_new_handler;
static void *crt_set_new_handler(void *handler){void*old=crt_new_handler;crt_new_handler=handler;return old;}
static void *crt_operator_new(uint32_t size){void*p=kmalloc(size?size:1U);if(!p&&crt_new_handler)((void(*)(void))crt_new_handler)();return p;}
static void crt_operator_delete(void*p){if(p)kfree(p);}
static crt_void_function_t crt_dllonexit(crt_void_function_t fn,crt_void_function_t **begin,crt_void_function_t **end){uint32_t count;crt_void_function_t*grown;if(!fn||!begin||!end)return NULL;count=(*begin&&*end&&*end>=*begin)?(uint32_t)(*end-*begin):0U;grown=(crt_void_function_t*)(*begin?krealloc(*begin,(count+1U)*sizeof(*grown)):kmalloc(sizeof(*grown)));if(!grown)return NULL;grown[count]=fn;*begin=grown;*end=grown+count+1U;return fn;}
static uint32_t crt_controlfp(uint32_t value,uint32_t mask){uint16_t old,next;__asm__ volatile("fnstcw %0":"=m"(old));next=(uint16_t)((old&~mask)|(value&mask));__asm__ volatile("fldcw %0"::"m"(next));return old;}
static uint32_t crt_control87(uint32_t value,uint32_t mask){return crt_controlfp(value,mask);}
static int32_t crt_except_handler3(void*a UNUSED,void*b UNUSED,void*c UNUSED,void*d UNUSED){return WIN32_EXCEPTION_DISPOSITION_CONTINUE_SEARCH;}
static int32_t crt_CxxFrameHandler(void*a UNUSED,void*b UNUSED,void*c UNUSED,void*d UNUSED){return WIN32_EXCEPTION_DISPOSITION_CONTINUE_SEARCH;}
static void WIN32_API crt_CxxThrowException(void*object,void*throw_info){uint32_t args[3]={0x19930520U,(uint32_t)(uintptr_t)object,(uint32_t)(uintptr_t)throw_info};win32_exception_raise(0xE06D7363U,1U,3U,args,(uint32_t)(uintptr_t)__builtin_return_address(0));}
__attribute__((naked)) static void crt_ftol(void){__asm__ volatile("subl $8,%esp; fistpll (%esp); popl %eax; popl %edx; ret");}
__attribute__((naked)) static void crt_CIsqrt(void){__asm__ volatile("fsqrt; ret");}
__attribute__((naked)) static void crt_CIsin(void){__asm__ volatile("fsin; ret");}
__attribute__((naked)) static void crt_CIcos(void){__asm__ volatile("fcos; ret");}
__attribute__((naked)) static void crt_CItan(void){__asm__ volatile("fptan; fstp %st(0); ret");}
__attribute__((naked)) static void crt_CIlog(void){__asm__ volatile("fldln2; fxch %st(1); fyl2x; ret");}




/* WIN32_MSVCRT_SYSTEMATIC_BATCH3 */
#define CRT_CTYPE_UPPER   0x0001U
#define CRT_CTYPE_LOWER   0x0002U
#define CRT_CTYPE_DIGIT   0x0004U
#define CRT_CTYPE_SPACE   0x0008U
#define CRT_CTYPE_PUNCT   0x0010U
#define CRT_CTYPE_CONTROL 0x0020U
#define CRT_CTYPE_BLANK   0x0040U
#define CRT_CTYPE_HEX     0x0080U
#define CRT_CTYPE_ALPHA   0x0100U

static void crt_initialize_ctype(void) {
    if (crt_ctype_initialized) return;
    kmemset(crt_ctype_storage, 0, sizeof(crt_ctype_storage));
    kmemset(crt_mbctype_storage, 0, sizeof(crt_mbctype_storage));
    for (int value = 0; value <= 255; value++) {
        uint16_t flags = 0U;
        if (value >= 'A' && value <= 'Z')
            flags |= CRT_CTYPE_UPPER | CRT_CTYPE_ALPHA;
        if (value >= 'a' && value <= 'z')
            flags |= CRT_CTYPE_LOWER | CRT_CTYPE_ALPHA;
        if (value >= '0' && value <= '9') flags |= CRT_CTYPE_DIGIT;
        if ((value >= '0' && value <= '9') ||
            (value >= 'A' && value <= 'F') ||
            (value >= 'a' && value <= 'f')) flags |= CRT_CTYPE_HEX;
        if (value == ' ' || value == '\t') flags |= CRT_CTYPE_BLANK;
        if (value == ' ' || (value >= '\t' && value <= '\r'))
            flags |= CRT_CTYPE_SPACE;
        if (value < 32 || value == 127) flags |= CRT_CTYPE_CONTROL;
        if (value >= 33 && value <= 126 &&
            !(flags & (CRT_CTYPE_ALPHA | CRT_CTYPE_DIGIT)))
            flags |= CRT_CTYPE_PUNCT;
        crt_ctype_pointer[value] = flags;
    }
    crt_ctype_initialized = true;
}

static int crt_isctype(int character, int mask) {
    crt_initialize_ctype();
    if (character < -128 || character > 255) return 0;
    return (crt_ctype_pointer[character] & (uint16_t)mask) != 0U;
}
static uint16_t **crt_p_pctype(void) {
    crt_initialize_ctype();
    return &crt_ctype_pointer;
}
static uint8_t **crt_p_mbctype(void) {
    crt_initialize_ctype();
    return &crt_mbctype_pointer;
}
static int crt_ismbcspace(uint32_t c) {
    return c <= 255U && crt_isctype((int)c, CRT_CTYPE_SPACE);
}
static int crt_ismbcdigit(uint32_t c) {
    return c <= 255U && crt_isctype((int)c, CRT_CTYPE_DIGIT);
}
static int crt_mbclen(const unsigned char *text) {
    return text && *text ? 1 : 0;
}
static unsigned char *crt_mbsinc(const unsigned char *text) {
    return text ? (unsigned char *)(text + (*text ? 1 : 0)) : NULL;
}
static unsigned char *crt_mbsdec(const unsigned char *start,
                                  const unsigned char *current) {
    if (!start || !current || current <= start) return NULL;
    return (unsigned char *)(current - 1);
}
static int crt_mbscmp(const unsigned char *left,
                       const unsigned char *right) {
    if (!left || !right) return left ? 1 : (right ? -1 : 0);
    return kstrcmp((const char *)left, (const char *)right);
}
static unsigned char *crt_mbschr(const unsigned char *text,
                                  uint32_t character) {
    return (unsigned char *)strchr((const char *)text,
                                   (int)(character & 0xFFU));
}
static unsigned char *crt_mbsrchr(const unsigned char *text,
                                   uint32_t character) {
    return (unsigned char *)crt_strrchr((const char *)text,
                                        (int)(character & 0xFFU));
}
static unsigned char *crt_mbsstr(const unsigned char *text,
                                  const unsigned char *needle) {
    size_t needle_length;
    if (!text || !needle) return NULL;
    needle_length = kstrlen((const char *)needle);
    if (!needle_length) return (unsigned char *)text;
    for (; *text; text++)
        if (kstrncmp((const char *)text, (const char *)needle,
                     needle_length) == 0)
            return (unsigned char *)text;
    return NULL;
}
static size_t crt_mbscspn(const unsigned char *text,
                           const unsigned char *reject) {
    size_t length = 0U;
    if (!text || !reject) return 0U;
    while (text[length] && !strchr((const char *)reject, text[length]))
        length++;
    return length;
}
static size_t crt_mbsspn(const unsigned char *text,
                          const unsigned char *accept) {
    size_t length = 0U;
    if (!text || !accept) return 0U;
    while (text[length] && strchr((const char *)accept, text[length]))
        length++;
    return length;
}
static unsigned char *crt_mbspbrk(const unsigned char *text,
                                   const unsigned char *accept) {
    if (!text || !accept) return NULL;
    while (*text) {
        if (strchr((const char *)accept, *text))
            return (unsigned char *)text;
        text++;
    }
    return NULL;
}

static uintptr_t crt_beginthreadex(
        void *security UNUSED, unsigned stack_size,
        win32_thread_start_t start, void *argument,
        unsigned creation_flags, unsigned *thread_id) {
    return (uintptr_t)win32_thread_create(
        stack_size, start, argument, creation_flags, thread_id);
}
static void crt_endthreadex(unsigned exit_code) NORETURN;
static void crt_endthreadex(unsigned exit_code) {
    win32_thread_exit(exit_code);
}

typedef void (*crt_beginthread_callback_t)(void *argument);
typedef struct {
    crt_beginthread_callback_t callback;
    void *argument;
} crt_beginthread_context_t;
static uint32_t WIN32_API crt_beginthread_adapter(void *raw) {
    crt_beginthread_context_t *context = (crt_beginthread_context_t *)raw;
    crt_beginthread_callback_t callback;
    void *argument;
    if (!context) return 0U;
    callback = context->callback;
    argument = context->argument;
    kfree(context);
    if (callback) callback(argument);
    return 0U;
}
static uintptr_t crt_beginthread(crt_beginthread_callback_t callback,
                                  unsigned stack_size, void *argument) {
    crt_beginthread_context_t *context;
    void *handle;
    if (!callback) return (uintptr_t)-1;
    context = (crt_beginthread_context_t *)kmalloc(sizeof(*context));
    if (!context) return (uintptr_t)-1;
    context->callback = callback;
    context->argument = argument;
    handle = win32_thread_create(stack_size, crt_beginthread_adapter,
                                 context, 0U, NULL);
    if (!handle) {
        kfree(context);
        return (uintptr_t)-1;
    }
    return (uintptr_t)handle;
}
static void crt_endthread(void) NORETURN;
static void crt_endthread(void) { win32_thread_exit(0U); }

static int32_t crt_except_handler2(
        void *record UNUSED, void *frame UNUSED,
        void *context UNUSED, void *dispatcher UNUSED) {
    return WIN32_EXCEPTION_DISPOSITION_CONTINUE_SEARCH;
}
static int crt_XcptFilter(uint32_t exception_number UNUSED,
                           void *exception_pointers) {
    if (!exception_pointers) return WIN32_EXCEPTION_CONTINUE_SEARCH;
    return win32_exception_unhandled_filter(
        (win32_exception_pointers32_t *)exception_pointers);
}
static void crt_EH_prolog_marker(void) {}

static crt_iobuf_t *crt_p_iob(void) { return crt_data_iob; }
static char ***crt_p_initenv(void) {
    (void)crt_current_state();
    return &crt_data_initenv;
}
static uint16_t ***crt_p_winitenv(void) {
    (void)crt_current_state();
    return &crt_data_winitenv;
}
static char *crt_strtok(char *text, const char *delimiters) {
    crt_thread_state_t *thread = crt_current_thread_state();
    char *start;
    char *cursor;
    if (!thread || !delimiters) return NULL;
    cursor = text ? text : thread->strtok_next;
    if (!cursor) return NULL;
    while (*cursor && strchr(delimiters, *cursor)) cursor++;
    if (!*cursor) {
        thread->strtok_next = NULL;
        return NULL;
    }
    start = cursor;
    while (*cursor && !strchr(delimiters, *cursor)) cursor++;
    if (*cursor) {
        *cursor++ = '\0';
        thread->strtok_next = cursor;
    } else thread->strtok_next = NULL;
    return start;
}
static size_t crt_msize(void *memory) {
    return mm_allocation_size(memory);
}
static void *crt_expand(void *memory, size_t new_size) {
    size_t old_size = mm_allocation_size(memory);
    if (!memory) return NULL;
    return new_size <= old_size ? memory : NULL;
}
static int crt_getch(void) { return -1; }

static void *crt_terminate_handler;
static void *crt_set_terminate(void *handler) {
    void *previous = crt_terminate_handler;
    crt_terminate_handler = handler;
    return previous;
}
static void crt_terminate(void) NORETURN;
static void crt_terminate(void) {
    if (crt_terminate_handler)
        ((void (*)(void))crt_terminate_handler)();
    crt_abort();
}

/* WIN32_MSVCRT_SETMBCP */
static int crt_setmbcp(int codepage) {
    crt_process_state_t *state = crt_current_state();
    int resolved = codepage;

    if (!state) return -1;

    switch (codepage) {
        case CRT_MB_CP_SBCS:
            resolved = 0;
            break;
        case CRT_MB_CP_OEM:
            resolved = 437;
            break;
        case CRT_MB_CP_ANSI:
        case CRT_MB_CP_LOCALE:
            resolved = 1252;
            break;
        default:
            if (codepage < 0) {
                int *error = crt_errno();
                if (error) *error = CRT_EINVAL;
                return -1;
            }
            break;
    }

    /*
     * The current BlesKernOS ANSI layer is single-byte. Keep the requested
     * code page for compatibility while reporting one byte per character.
     */
    state->mb_codepage = resolved;
    state->mb_cur_max = 1;
    crt_sync_import_data(state);
    return 0;
}

static int crt_getmbcp(void) {
    crt_process_state_t *state = crt_current_state();
    return state ? state->mb_codepage : 1252;
}

static int crt_abs(int value) { return value < 0 ? -value : value; }

/* WIN32_MSVCRT_MBSNBCMP */
static int crt_mbsnbcmp(const unsigned char *left,
                        const unsigned char *right,
                        size_t count) {
    if (count == 0U || left == right) return 0;
    if (!left) return -1;
    if (!right) return 1;

    for (size_t i = 0; i < count; i++) {
        unsigned int a = left[i];
        unsigned int b = right[i];

        if (a != b) return a < b ? -1 : 1;
        if (a == 0U) return 0;
    }

    return 0;
}

uint32_t win32_msvcrt_resolve(const char *name) {
#define MAP(api,target) if(equal(name,api))return (uint32_t)(uintptr_t)&target
#define MAP_DATA(api,target) if(equal(name,api))return (uint32_t)(uintptr_t)&target
    MAP_DATA("__initenv",crt_data_initenv); MAP_DATA("__winitenv",crt_data_winitenv);
    MAP_DATA("_environ",crt_data_environ); MAP_DATA("_wenviron",crt_data_wenviron);
    MAP_DATA("__argc",crt_data_argc); MAP_DATA("__argv",crt_data_argv);
    MAP_DATA("__wargv",crt_data_wargv); MAP_DATA("_acmdln",crt_data_acmdln);
    MAP_DATA("_wcmdln",crt_data_wcmdln); MAP_DATA("_fmode",crt_data_fmode);
    MAP_DATA("_commode",crt_data_commode); MAP_DATA("__mb_cur_max",crt_data_mb_cur_max);
    MAP_DATA("_iob",crt_data_iob);
    MAP_DATA("_acmdln_dll",crt_data_acmdln);
    MAP_DATA("_commode_dll",crt_data_commode);
    MAP_DATA("_fmode_dll",crt_data_fmode);
    MAP_DATA("_aexit_rtn_dll",crt_data_aexit_rtn_dll);
    MAP("__getmainargs",crt_getmainargs); MAP("__wgetmainargs",crt_wgetmainargs);
    MAP("__GetMainArgs",crt_getmainargs);
    MAP("_beginthreadex",crt_beginthreadex); MAP("_endthreadex",crt_endthreadex);
    MAP("_beginthread",crt_beginthread); MAP("_endthread",crt_endthread);
    MAP("__p___argc",crt_p_argc); MAP("__p___argv",crt_p_argv); MAP("__p___wargv",crt_p_wargv);
    MAP("__p__environ",crt_p_environ); MAP("__p__wenviron",crt_p_wenviron);
    MAP("__p__acmdln",crt_p_acmdln); MAP("__p__wcmdln",crt_p_wcmdln);
    MAP("__p__fmode",crt_p_fmode); MAP("__p__commode",crt_p_commode);
    MAP("__p__iob",crt_p_iob); MAP("__iob_func",crt_p_iob);
    MAP("__p___initenv",crt_p_initenv); MAP("__p___winitenv",crt_p_winitenv);
    MAP("__p___mb_cur_max",crt_p_mb_cur_max);
    MAP("_errno",crt_errno); MAP("__p__errno",crt_errno);
    MAP("__doserrno",crt_doserrno); MAP("__p__doserrno",crt_doserrno);
    MAP("_get_errno",crt_get_errno); MAP("_set_errno",crt_set_errno);
    MAP("_get_doserrno",crt_get_doserrno); MAP("_set_doserrno",crt_set_doserrno);
    MAP("__set_app_type",crt_set_app_type); MAP("__setusermatherr",crt_setusermatherr);
    MAP("_initterm",crt_initterm); MAP("_initterm_e",crt_initterm_e);
    MAP("_onexit",crt_onexit); MAP("atexit",crt_atexit);
    MAP("_cexit",crt_cexit); MAP("_c_exit",crt_c_exit); MAP("_amsg_exit",crt_amsg_exit);
    MAP("signal",crt_signal); MAP("raise",crt_raise);
    MAP("_get_pgmptr",crt_get_pgmptr); MAP("_get_wpgmptr",crt_get_wpgmptr);
    MAP("malloc",kmalloc); MAP("calloc",crt_calloc); MAP("realloc",krealloc); MAP("free",kfree); MAP("_msize",crt_msize); MAP("_expand",crt_expand);
    MAP("memcpy",memcpy); MAP("memmove",memmove); MAP("memset",memset); MAP("memcmp",memcmp);
    /* BLES_WINE_CRTDLL_STRSTR_20260723 */
    MAP("strlen",strlen); MAP("strcmp",strcmp); MAP("strncmp",strncmp); MAP("strcpy",strcpy);
    MAP("strncpy",strncpy); MAP("strcat",strcat); MAP("strchr",strchr); MAP("strstr",strstr); MAP("strtok",crt_strtok);
    MAP("strerror",crt_strerror); MAP("wcslen",crt_wcslen); MAP("_strrev",crt_strrev);
    MAP("_stricmp",crt_stricmp); MAP("stricmp",crt_stricmp); MAP("_strcmpi",crt_stricmp);
    MAP("_strnicmp",crt_strnicmp); MAP("strnicmp",crt_strnicmp); MAP("_strdup",crt_strdup);
    MAP("_strupr",crt_strupr); MAP("_strlwr",crt_strlwr);
    MAP("wcscpy",crt_wcscpy); MAP("wcsncpy",crt_wcsncpy); MAP("wcscat",crt_wcscat);
        MAP("wcstombs",crt_wcstombs);
MAP("wcscmp",crt_wcscmp); MAP("wcsncmp",crt_wcsncmp); MAP("wcschr",crt_wcschr); MAP("wcsrchr",crt_wcsrchr);
    MAP("atoi",atoi); MAP("atof",atof); MAP("atol",crt_atol);
    MAP("abs",crt_abs);
    MAP("strtol",crt_strtol);
    MAP("strrchr",crt_strrchr); MAP("memchr",crt_memchr);
    MAP("tolower",crt_tolower); MAP("toupper",crt_toupper);
    MAP("getenv",crt_getenv); MAP("_putenv",crt_putenv); MAP("putenv",crt_putenv);
    MAP("rand",crt_rand); MAP("srand",crt_srand); MAP("clock",crt_clock); MAP("time",crt_time);
    MAP("qsort",crt_qsort); MAP("bsearch",crt_bsearch);
    MAP("isdigit",crt_isdigit); MAP("isspace",crt_isspace);
    MAP("isalnum",crt_isalnum); MAP("isprint",crt_isprint);
    MAP("printf",printf); MAP("fprintf",crt_fprintf); MAP("vfprintf",crt_vfprintf);
    MAP("sprintf",crt_sprintf); MAP("vsprintf",crt_vsprintf);
    MAP("snprintf",crt_snprintf); MAP("_snprintf",crt_snprintf);
    MAP("vsnprintf",vsnprintf); MAP("_vsnprintf",vsnprintf);
    MAP("_gcvt",crt_gcvt); MAP("gcvt",crt_gcvt);
    MAP("fopen",fopen); MAP("fclose",fclose); MAP("fread",fread); MAP("fwrite",crt_fwrite); MAP("fseek",fseek);
    MAP("fgetpos",crt_fgetpos); MAP("fsetpos",crt_fsetpos);
    MAP("_open",crt_open); MAP("_wopen",crt_wopen); MAP("_sopen",crt_sopen); MAP("_close",crt_close);
    MAP("_read",crt_read_fd); MAP("_write",crt_write_fd); MAP("_lseek",crt_lseek); MAP("_tell",crt_tell);
    MAP("_filelength",crt_filelength); MAP("_get_osfhandle",crt_get_osfhandle); MAP("_open_osfhandle",crt_open_osfhandle);
    MAP("_isatty",crt_isatty); MAP("_commit",crt_commit); MAP("_unlink",crt_unlink); MAP("_wunlink",crt_wunlink);
    MAP("_mkdir",crt_mkdir); MAP("_rmdir",crt_rmdir); MAP("_chdir",crt_chdir);
    MAP("remove",crt_unlink); MAP("rename",crt_rename); MAP("_access",crt_access);
    MAP("ftell",ftell); MAP("fflush",crt_fflush); MAP("fputc",crt_fputc);
    MAP("puts",puts); MAP("putchar",putchar); MAP("_getch",crt_getch);
    MAP("setlocale",crt_setlocale); MAP("localeconv",crt_localeconv);
    MAP("_ismbblead",crt_ismbblead);
    MAP("_isctype",crt_isctype); MAP("__p__pctype",crt_p_pctype);
    MAP("__p__mbctype",crt_p_mbctype);
    MAP("_ismbcspace",crt_ismbcspace); MAP("_ismbcdigit",crt_ismbcdigit);
    MAP("_mbclen",crt_mbclen); MAP("_mbsinc",crt_mbsinc);
    MAP("_mbsdec",crt_mbsdec); MAP("_mbscmp",crt_mbscmp);
    MAP("_mbschr",crt_mbschr); MAP("_mbsrchr",crt_mbsrchr);
    MAP("_mbsstr",crt_mbsstr); MAP("_mbscspn",crt_mbscspn);
    MAP("_mbsspn",crt_mbsspn); MAP("_mbspbrk",crt_mbspbrk);
    MAP("_setmbcp",crt_setmbcp);
    MAP("_getmbcp",crt_getmbcp);
    MAP("_mbsnbcmp",crt_mbsnbcmp);
    MAP("acos",crt_acos); MAP("asin",crt_asin); MAP("atan",crt_atan);
    MAP("cosh",crt_cosh); MAP("log10",crt_log10); MAP("sinh",crt_sinh);
    MAP("tan",crt_tan); MAP("tanh",crt_tanh); MAP("sqrt",crt_sqrt); MAP("fabs",crt_fabs);
    MAP("floor",crt_floor); MAP("ceil",crt_ceil); MAP("fmod",crt_fmod);
    MAP("_controlfp",crt_controlfp); MAP("_control87",crt_control87);
    MAP("_ftol",crt_ftol); MAP("_ftol2",crt_ftol); MAP("_CIsqrt",crt_CIsqrt);
    MAP("_CIsin",crt_CIsin); MAP("_CIcos",crt_CIcos); MAP("_CItan",crt_CItan); MAP("_CIlog",crt_CIlog);
    MAP("_purecall",crt_purecall); MAP("_set_new_handler",crt_set_new_handler);
    MAP("?set_terminate@@YAP6AXXZP6AXXZ@Z",crt_set_terminate);
    MAP("?terminate@@YAXXZ",crt_terminate);
    MAP("?_set_new_handler@@YAP6AHI@ZP6AHI@Z@Z",crt_set_new_handler);
    MAP("__dllonexit",crt_dllonexit); MAP("_except_handler3",crt_except_handler3);
    MAP("_except_handler2",crt_except_handler2);
    MAP("_XcptFilter",crt_XcptFilter);
    MAP("longjmp",win32_crt_longjmp);
    MAP("_longjmp",win32_crt_longjmp);
    MAP("_EH_prolog",crt_EH_prolog_marker);
    MAP("__CxxFrameHandler",crt_CxxFrameHandler); MAP("_CxxThrowException",crt_CxxThrowException);
    MAP("??2@YAPAXI@Z",crt_operator_new); MAP("??3@YAXPAX@Z",crt_operator_delete);
    MAP("??_U@YAPAXI@Z",crt_operator_new); MAP("??_V@YAXPAX@Z",crt_operator_delete);
    MAP("exit",crt_exit); MAP("_exit",crt_exit); MAP("abort",crt_abort);
#undef MAP_DATA
#undef MAP
    return 0;
}
