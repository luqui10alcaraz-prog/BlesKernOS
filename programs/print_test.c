#include <bleskernos_api.h>
#include <bleskernos_print.h>

static const uint8_t logo_bits[16] = {
    0x3CU, 0x42U, 0xA5U, 0x81U,
    0xA5U, 0x99U, 0x42U, 0x3CU,
    0x3CU, 0x42U, 0x99U, 0xA5U,
    0x81U, 0xA5U, 0x42U, 0x3CU
};

void bleskernos_program_main(bk_gui_desktop_t *desktop UNUSED) {
    bk_print_job_t *job;

    if (bk_sys_api_version() < 25U ||
        (bk_sys_capabilities() & (BK_API_CAP_FILES | BK_API_CAP_PRINT)) !=
        (BK_API_CAP_FILES | BK_API_CAP_PRINT)) {
        bk_sys_log("[PRINTTEST] API 25 con impresion requerida\n");
        bk_proc_exit();
    }

    job = print_begin("Prueba de impresion BlesKernOS", "PSFILE");
    if (!job ||
        !print_set_font(job, BK_PRINT_FONT_SERIF, 20U,
                        BK_PRINT_FONT_BOLD) ||
        !print_text(job, 72, 84, "BlesKernOS Printing System") ||
        !print_line(job, 72, 96, 523, 96, 1U) ||
        !print_set_font(job, BK_PRINT_FONT_SANS, 12U, 0U) ||
        !print_text(job, 72, 126,
                    "Pagina abstracta, spooler Ring 3 y perfil PSFILE.") ||
        !print_set_font(job, BK_PRINT_FONT_MONO, 10U,
                        BK_PRINT_FONT_UNDERLINE) ||
        !print_text(job, 72, 154,
                    "Motores: texto, ESC/P, ESC/P2, PCL 5 y PostScript.") ||
        !print_bitmap_mono(job, 72, 184, 8U, 16U, 1U, 72U, logo_bits)) {
        if (job) print_cancel(job);
        bk_sys_log("[PRINTTEST] no se pudo construir el trabajo\n");
        bk_proc_exit();
    }
    if (!print_submit(job)) {
        /* print_submit siempre consume el objeto, incluso al fallar. */
        bk_sys_log("[PRINTTEST] no se pudo poner el trabajo en cola\n");
        bk_proc_exit();
    }

    bk_sys_log("[PRINTTEST] trabajo enviado a /TEMP/SPOOL; salida en /TEMP/PRINT\n");
    bk_proc_exit();
}
