#include "win32.h"
#include "process.h"
#include "../include/memory.h"
#include "../include/task.h"
#include "../stdio.h"
#include "../string.h"
#include "../stdlib.h"
#include "../stdarg.h"

#define CRT_PROCESS_SLOTS TASK_MAX
#define CRT_MAX_ARGS 16U
#define CRT_COMMAND_LINE_CHARS 512U
#define CRT_ENV_ENTRIES 4U
#define CRT_ATEXIT_SLOTS 32U
#define CRT_THREAD_SLOTS TASK_MAX
#define CRT_EINVAL 22

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
} crt_process_state_t;

static crt_process_state_t crt_processes[CRT_PROCESS_SLOTS];

typedef struct {
    uint32_t process_id;
    uint32_t thread_id;
    int errno_value;
    uint32_t doserrno_value;
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
static int crt_snprintf(char *out,size_t size,const char *fmt,...){va_list a;int n;va_start(a,fmt);n=vsnprintf(out,size,fmt,a);va_end(a);return n;}
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
    MAP("__getmainargs",crt_getmainargs); MAP("__wgetmainargs",crt_wgetmainargs);
    MAP("__p___argc",crt_p_argc); MAP("__p___argv",crt_p_argv); MAP("__p___wargv",crt_p_wargv);
    MAP("__p__environ",crt_p_environ); MAP("__p__wenviron",crt_p_wenviron);
    MAP("__p__acmdln",crt_p_acmdln); MAP("__p__wcmdln",crt_p_wcmdln);
    MAP("__p__fmode",crt_p_fmode); MAP("__p__commode",crt_p_commode);
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
    MAP("malloc",kmalloc); MAP("calloc",crt_calloc); MAP("realloc",krealloc); MAP("free",kfree);
    MAP("memcpy",memcpy); MAP("memmove",memmove); MAP("memset",memset); MAP("memcmp",memcmp);
    MAP("strlen",strlen); MAP("strcmp",strcmp); MAP("strncmp",strncmp); MAP("strcpy",strcpy);
    MAP("strncpy",strncpy); MAP("strcat",strcat); MAP("strchr",strchr);
    MAP("strerror",crt_strerror); MAP("wcslen",crt_wcslen); MAP("_strrev",crt_strrev);
    MAP("atoi",atoi); MAP("atof",atof); MAP("atol",crt_atol);
    MAP("strtol",crt_strtol);
    MAP("strrchr",crt_strrchr); MAP("memchr",crt_memchr);
    MAP("isdigit",crt_isdigit); MAP("isspace",crt_isspace);
    MAP("isalnum",crt_isalnum); MAP("isprint",crt_isprint);
    MAP("printf",printf); MAP("fprintf",crt_fprintf); MAP("vfprintf",crt_vfprintf);
    MAP("sprintf",crt_sprintf); MAP("snprintf",crt_snprintf); MAP("_snprintf",crt_snprintf);
    MAP("vsnprintf",vsnprintf); MAP("_vsnprintf",vsnprintf);
    MAP("fopen",fopen); MAP("fclose",fclose); MAP("fread",fread); MAP("fwrite",crt_fwrite); MAP("fseek",fseek);
    MAP("ftell",ftell); MAP("fflush",crt_fflush); MAP("fputc",crt_fputc);
    MAP("puts",puts); MAP("putchar",putchar);
    MAP("setlocale",crt_setlocale); MAP("localeconv",crt_localeconv);
    MAP("_ismbblead",crt_ismbblead);
    MAP("acos",crt_acos); MAP("asin",crt_asin); MAP("atan",crt_atan);
    MAP("cosh",crt_cosh); MAP("log10",crt_log10); MAP("sinh",crt_sinh);
    MAP("tan",crt_tan); MAP("tanh",crt_tanh);
    MAP("exit",crt_exit); MAP("_exit",crt_exit); MAP("abort",crt_abort);
#undef MAP_DATA
#undef MAP
    return 0;
}
