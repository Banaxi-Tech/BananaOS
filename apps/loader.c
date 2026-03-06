#include <stdint.h>
#include <stddef.h>
#include "../drivers/disk.h"
#include "../drivers/fat16.h"
#include "../drivers/fat32.h"

extern void term_print(const char* s);
extern void jmp_user(uint32_t entry, uint32_t stack);

static void format_fat_name_loader(const char* raw, char* out) {
    int ni = 0;
    for (int k = 0; k < 8; k++) {
        if (raw[k] != ' ') out[ni++] = raw[k];
    }
    int has_ext = 0;
    for (int k = 8; k < 11; k++) if (raw[k] != ' ') { has_ext = 1; break; }
    if (has_ext) {
        out[ni++] = '.';
        for (int k = 8; k < 11; k++) if (raw[k] != ' ') out[ni++] = raw[k];
    }
    out[ni] = 0;
}

static int str_case_cmp_loader(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a; if (ca >= 'a' && ca <= 'z') ca -= 32;
        char cb = *b; if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    char ca = *a; if (ca >= 'a' && ca <= 'z') ca -= 32;
    char cb = *b; if (cb >= 'a' && cb <= 'z') cb -= 32;
    return ca - cb;
}

void load_bex(uint8_t drive, const char* filename) {
    if (!disk_drive_exists(drive)) {
        term_print("Disk not present.");
        return;
    }

    FAT32Entry entries[32];
    int count = 0;
    int fs = 16;
    if (fat32_init(drive)) { fs = 32; count = fat32_list_root(entries, 32); }
    else { fat16_init(drive); fs = 16; count = fat16_list_root((FAT16Entry*)entries, 32); }

    int found_idx = -1;
    for (int i = 0; i < count; i++) {
        char name[14]; format_fat_name_loader(entries[i].name, name);
        if (str_case_cmp_loader(name, filename) == 0) {
            if (entries[i].attr & 0x10) { term_print("Is a directory."); return; }
            found_idx = i;
            break;
        }
    }

    if (found_idx == -1) {
        term_print("File not found on disk.");
        return;
    }

    uint32_t size = entries[found_idx].size;
    if (size == 0) {
        term_print("File is empty.");
        return;
    }

    uint8_t* load_addr = (uint8_t*)0x2000000;
    
    // Clear out any old program data that might be there
    for (uint32_t i = 0; i < size + 4096; i++) {
        load_addr[i] = 0;
    }
    
    if (fs == 32) fat32_read_file(&entries[found_idx], load_addr);
    else fat16_read_file((FAT16Entry*)&entries[found_idx], load_addr);

    term_print("Executing .bex file...");
    
    uint32_t user_stack = 0x2800000;
    jmp_user((uint32_t)load_addr, user_stack);
}
