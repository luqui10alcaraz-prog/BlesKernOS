#include "common.h"

typedef struct { const char *name; const char *description; } help_entry_t;
static const help_entry_t entries[] = {
    {"help", "Muestra esta lista completa de comandos"},
    {"about", "Muestra informacion de BlesKernOS"},
    {"uname", "Muestra nombre, version y arquitectura del sistema"},
    {"hostname", "Muestra el nombre del equipo"},
    {"uptime", "Muestra cuanto tiempo lleva encendido el sistema"},
    {"date", "Muestra la fecha del reloj de tiempo real"},
    {"time", "Muestra la hora del reloj de tiempo real"},
    {"shutdown", "Apaga el equipo"},
    {"reboot", "Reinicia el equipo"},
    {"sleep", "Espera una cantidad de segundos"},
    {"fdisk", "Muestra discos y particiones MBR reales"},
    {"format", "Crea un sistema FAT en un disco no montado"},
    {"mount", "Monta un dispositivo FAT como volumen activo"},
    {"unmount", "Desmonta el volumen activo cuando el VFS lo permita"},
    {"label", "Muestra la etiqueta del volumen FAT activo"},
    {"checkdisk", "Analiza FAT; /fix aplica reparaciones controladas"},
    {"fsinfo", "Muestra capacidad, salud FAT y fragmentacion"},
    {"backup", "Crea, lista, verifica y restaura archivos BKB1"},
    {"dir", "Lista archivos y directorios"},
    {"ls", "Lista archivos y directorios"},
    {"copy", "Copia un archivo"},
    {"move", "Mueve o renombra un archivo"},
    {"delete", "Elimina un archivo"},
    {"mkdir", "Crea un directorio"},
    {"rmdir", "Elimina un directorio vacio"},
    {"rename", "Cambia el nombre o ruta de un archivo"},
    {"touch", "Crea un archivo vacio"},
    {"tree", "Muestra un arbol de directorios"},
    {"find", "Busca nombres dentro de un arbol"},
    {"attrib", "Administra atributos cuando el VFS los soporte"},
    {"chmod", "Administra permisos cuando el VFS los soporte"},
    {"type", "Muestra el contenido de un archivo"},
    {"more", "Muestra un archivo paginado"},
    {"cat", "Muestra el contenido de un archivo"},
    {"diff", "Compara dos archivos byte a byte"},
    {"ps", "Lista los procesos"},
    {"kill", "Solicita finalizar un proceso por PID"},
    {"tasklist", "Lista las tareas activas"},
    {"taskkill", "Solicita finalizar una tarea por PID"},
    {"top", "Muestra uso de CPU y procesos detallados"},
    {"nice", "Cambia prioridades cuando el planificador lo permita"},
    {"pci", "Lista dispositivos PCI"},
    {"usb", "Lista controladores USB detectados por PCI"},
    {"lspci", "Lista dispositivos PCI"},
    {"lsusb", "Lista controladores USB detectados por PCI"},
    {"cpuinfo", "Muestra informacion basica del procesador"},
    {"mem", "Muestra memoria total, usada y libre"},
    {"soundtest", "Reproduce un tono para probar el audio"},
    {"ipconfig", "Muestra configuracion IP cuando exista red"},
    {"ping", "Prueba conectividad cuando exista red"},
    {"netstat", "Muestra conexiones cuando exista red"},
    {"ftp", "Cliente FTP futuro"},
    {"wget", "Descarga archivos cuando exista red"},
    {"curl", "Transfiere datos cuando exista red"},
    {"compile", "Compila codigo cuando exista toolchain nativo"},
    {"link", "Enlaza objetos cuando exista toolchain nativo"},
    {"objdump", "Muestra un volcado hexadecimal de un objeto"},
    {"nm", "Extrae cadenas visibles de un objeto"},
    {"hexdump", "Muestra bytes de un archivo en hexadecimal"},
    {"strings", "Extrae cadenas imprimibles de un archivo"},
    {"calc", "Realiza operaciones con numeros enteros"},
    {"hexedit", "Examina o modifica un byte de un archivo"},
    {"compress", "Comprime un archivo al formato RLE BKZ1"},
    {"extract", "Extrae un archivo RLE BKZ1"},
    {"checksum", "Calcula FNV-1a de 32 bits"},
    {"benchmark", "Ejecuta una prueba sencilla de CPU"},
    {"start", "Abre un programa por nombre o ruta"},
    {"cd", "Cambia el directorio actual"},
    {"pwd", "Muestra el directorio actual"},
    {"exit", "Cierra la shell"},
    {"clear", "Limpia la terminal"},
    {"history", "Muestra el historial de comandos"},
    {"alias", "Crea o lista alias"},
    {"unalias", "Elimina un alias"},
    {"set", "Define o lista variables"},
    {"echo", "Imprime texto"},
    {"ver", "Muestra las versiones del sistema y la shell"}
};
static int run(int argc, char **argv) {
    if (argc > 1) {
        for (uint32_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
            if (command_is(entries[i].name, argv[1])) {
                kprintf("%s - %s\n", entries[i].name, entries[i].description);
                return 0;
            }
        }
        return command_error("help", "comando desconocido");
    }
    bk_console_write("COMANDO - DESCRIPCION\n");
    bk_console_write("Use: help comando para consultar uno en particular.\n\n");
    for (uint32_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++)
        kprintf("%s - %s\n", entries[i].name, entries[i].description);
    return 0;
}

BK_COMMAND_MAIN(run)
