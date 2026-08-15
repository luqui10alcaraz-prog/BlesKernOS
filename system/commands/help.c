#include "common.h"

typedef struct { const char *name; const char *description; } help_entry_t;
static const help_entry_t entries[] = {
    {"help", "@H64B5E302"},
    {"about", "@H50A5DC5C"},
    {"uname", "@H197F32FB"},
    {"hostname", "@HD4D920C0"},
    {"uptime", "@H693E0122"},
    {"date", "@H89ADBFE8"},
    {"time", "@H661F1A85"},
    {"shutdown", "@HBDFAF6C1"},
    {"reboot", "@H824F043F"},
    {"sleep", "@HB4AED1C4"},
    {"fdisk", "@HA5A65D1E"},
    {"format", "@H84A5E791"},
    {"mount", "@H91B7D7CD"},
    {"unmount", "@HDD9AA1E8"},
    {"label", "@H7479CB03"},
    {"checkdisk", "@H0A0F801B"},
    {"fsinfo", "@HC0D42777"},
    {"backup", "@H24C3A329"},
    {"dir", "@HD66F685B"},
    {"ls", "@HD66F685B"},
    {"copy", "@H03557182"},
    {"move", "@H750025E1"},
    {"delete", "@HE6A54F53"},
    {"mkdir", "@H754D1993"},
    {"rmdir", "@HAF9DA64B"},
    {"rename", "@HE8DEFBC5"},
    {"touch", "@H3F72182F"},
    {"tree", "@H2FC15407"},
    {"find", "@HB3286F0D"},
    {"attrib", "@H3EA429F0"},
    {"chmod", "@HCA961957"},
    {"type", "@HD47CBBB8"},
    {"more", "@H001BF70E"},
    {"cat", "@HD47CBBB8"},
    {"diff", "@HC55E9FEE"},
    {"ps", "@H790CBC98"},
    {"kill", "@H9990CD5F"},
    {"tasklist", "@H8C49CF51"},
    {"taskkill", "@HA9B27CF8"},
    {"top", "@H6FA74EC4"},
    {"nice", "@HE48CEA58"},
    {"pci", "@H67C1E3FE"},
    {"usb", "@H29F77D5C"},
    {"lspci", "@H67C1E3FE"},
    {"lsusb", "@H29F77D5C"},
    {"cpuinfo", "@H4A11B3CD"},
    {"mem", "@H4E799AD9"},
    {"soundtest", "@H83851F2C"},
    {"ipconfig", "@H10343E79"},
    {"ping", "@HE895C4C4"},
    {"netstat", "@HC8F4040A"},
    {"ftp", "@HB2C9F8B4"},
    {"wget", "@H39C85BFF"},
    {"curl", "@H8C7F21BA"},
    {"compile", "@H794E2C87"},
    {"link", "@H9DEC7DC6"},
    {"objdump", "@H329A62AB"},
    {"nm", "@H7F0BFF11"},
    {"hexdump", "@H0D133197"},
    {"strings", "@H5B35F816"},
    {"calc", "@H1CB1E66A"},
    {"hexedit", "@H72C0C8F4"},
    {"compress", "@HA97D2626"},
    {"extract", "@H231750C4"},
    {"checksum", "@H6587A601"},
    {"benchmark", "@HB6A5B657"},
    {"start", "@H96EA3386"},
    {"cd", "@HF26CDC57"},
    {"pwd", "@H0963CA17"},
    {"exit", "@H5AA123D8"},
    {"clear", "@H2E1891D8"},
    {"history", "@H7B1BF2ED"},
    {"alias", "@HD9155830"},
    {"unalias", "@HBD387DAB"},
    {"set", "@H4916BE75"},
    {"echo", "@H4F69F172"},
    {"ver", "@HB21683E7"}
};
static int run(int argc, char **argv) {
    if (argc > 1) {
        for (uint32_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
            if (command_is(entries[i].name, argv[1])) {
                kprintf("%s - %s\n", entries[i].name, entries[i].description);
                return 0;
            }
        }
        return command_error("help", "@HD3E24BEC");
    }
    bk_console_write("COMANDO - DESCRIPCION\n");
    bk_console_write("Use: help comando para consultar uno en particular.\n\n");
    for (uint32_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++)
        kprintf("%s - %s\n", entries[i].name, entries[i].description);
    return 0;
}

BK_COMMAND_MAIN(run)
