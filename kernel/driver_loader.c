#include "include/driver.h"
#include "include/elf_loader.h"
#include "include/memory.h"
#include "include/vfs.h"
#include "include/vga.h"

static bk_loaded_driver_t g_drivers[BK_DRIVER_MAX_LOADED];
static uint32_t g_driver_count;
static const char *g_driver_error = "sin error";

static int driver_name_compare(const char *left, const char *right) {
    if (!left) return right ? -1 : 0;
    if (!right) return 1;
    while (*left && *right) {
        char a = *left++;
        char b = *right++;
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
        if (a != b) return (int)(uint8_t)a - (int)(uint8_t)b;
    }
    return (int)(uint8_t)*left - (int)(uint8_t)*right;
}

static bool driver_has_extension(const char *name) {
    uint32_t length;
    const char *extension;

    if (!name) return false;
    length = (uint32_t)kstrlen(name);
    if (length < 4) return false;
    extension = name + length - 4;
    return extension[0] == '.' &&
           (extension[1] == 'D' || extension[1] == 'd') &&
           (extension[2] == 'V' || extension[2] == 'v') &&
           (extension[3] == 'R' || extension[3] == 'r');
}

void driver_loader_init(void) {
    kmemset(g_drivers, 0, sizeof(g_drivers));
    g_driver_count = 0;
    g_driver_error = "sin error";
}

bool driver_load(const char *path) {
    void *image = NULL;
    void *entry = NULL;
    bk_driver_query_t query;
    const bk_driver_module_t *module;
    bk_loaded_driver_t *loaded;

    if (!path || !path[0]) {
        g_driver_error = "ruta de driver invalida";
        return false;
    }
    for (uint32_t i = 0; i < g_driver_count; i++) {
        if (driver_name_compare(g_drivers[i].path, path) == 0) {
            g_driver_error = "driver ya cargado";
            return false;
        }
    }
    if (g_driver_count >= BK_DRIVER_MAX_LOADED) {
        g_driver_error = "tabla de drivers llena";
        return false;
    }
    if (!elf_load_resident(path, "bleskernos_driver_query", &image, &entry)) {
        g_driver_error = elf_last_error();
        return false;
    }

    query = (bk_driver_query_t)entry;
    module = query();
    if (!module || module->abi_version != BK_DRIVER_ABI_VERSION ||
        module->descriptor_size != sizeof(*module) ||
        !module->name || !module->name[0] || !module->init) {
        elf_release_image(image);
        g_driver_error = "descriptor o ABI .DVR invalido";
        return false;
    }
    for (uint32_t i = 0; i < g_driver_count; i++) {
        if (driver_name_compare(g_drivers[i].name, module->name) == 0) {
            elf_release_image(image);
            g_driver_error = "nombre de driver duplicado";
            return false;
        }
    }
    if (!module->init()) {
        elf_release_image(image);
        g_driver_error = "init del driver fallo";
        return false;
    }

    loaded = &g_drivers[g_driver_count++];
    kstrncpy(loaded->name, module->name, sizeof(loaded->name) - 1);
    kstrncpy(loaded->path, path, sizeof(loaded->path) - 1);
    loaded->description = module->description;
    loaded->image = image;
    loaded->shutdown = module->shutdown;
    g_driver_error = "sin error";
    kprintf("[DVR] %s: cargado (%s)\n", loaded->name, path);
    return true;
}

uint32_t driver_load_directory(const char *path) {
    vfs_dir_entry_t *entries;
    uint32_t count = 0;
    uint32_t loaded = 0;
    char full[VFS_MAX_PATH];

    if (!path) return 0;
    entries = (vfs_dir_entry_t *)kmalloc(sizeof(*entries) *
                                         VFS_MAX_DIR_ENTRIES);
    if (!entries) {
        g_driver_error = "sin memoria para listar drivers";
        return 0;
    }
    if (!vfs_listdir(path, entries, VFS_MAX_DIR_ENTRIES, &count)) {
        kfree(entries);
        g_driver_error = "no se pudo abrir el directorio de drivers";
        return 0;
    }

    /* FAT conserva el orden de creacion, pero no forma parte del ABI. Cargar
       alfabeticamente vuelve reproducible el arranque y sus diagnosticos. */
    for (uint32_t i = 1; i < count; i++) {
        vfs_dir_entry_t entry = entries[i];
        uint32_t position = i;
        while (position > 0 &&
               driver_name_compare(entries[position - 1].name,
                                   entry.name) > 0) {
            entries[position] = entries[position - 1];
            position--;
        }
        entries[position] = entry;
    }

    for (uint32_t i = 0; i < count; i++) {
        uint32_t base_length;
        if (entries[i].type != VFS_NODE_FILE ||
            !driver_has_extension(entries[i].name)) continue;
        base_length = (uint32_t)kstrlen(path);
        if (base_length + 1U + kstrlen(entries[i].name) >= sizeof(full)) {
            kprintf("[DVR] nombre demasiado largo: %s\n", entries[i].name);
            continue;
        }
        kstrcpy(full, path);
        if (base_length && full[base_length - 1] != '/') kstrcat(full, "/");
        kstrcat(full, entries[i].name);
        if (driver_load(full)) loaded++;
        else kprintf("[DVR] %s: ERROR: %s\n", full, g_driver_error);
    }
    kfree(entries);
    return loaded;
}

uint32_t driver_count(void) {
    return g_driver_count;
}

const bk_loaded_driver_t *driver_at(uint32_t index) {
    if (index >= g_driver_count) return NULL;
    return &g_drivers[index];
}

const char *driver_last_error(void) {
    return g_driver_error;
}
