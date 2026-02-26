#include "apps.h"
#include "disk.h"
#include "fat16.h"
#include "fat32.h"
#include <stddef.h>

Window win_explorer = {200, 200, 400, 300, 0, 0, 0, 200, 200, 400, 300, "Banana Files"};

FAT32Entry file_entries[32]; 
int file_count = 0;
int fs_type = 16; 
int selected_drive = 0;
int drives_present[34] = {0}; // 2 IDE + 32 SATA

void explorer_init(uint8_t drive) {
    // Scan for drives
    drives_present[0] = disk_drive_exists(0);
    drives_present[1] = disk_drive_exists(1);
    // SATA drives (indices 2-33)
    // We'll just check first 4 SATA for the sidebar to avoid overcrowding
    for (int i=0; i<4; i++) {
        if (disk_drive_exists(i+2)) drives_present[i+2] = 1;
        else drives_present[i+2] = 0;
    }

    selected_drive = drive;
    // Try FAT32 first
    if (fat32_init(drive)) {
        fs_type = 32;
        file_count = fat32_list_root(file_entries, 32);
    } else {
        fat16_init(drive);
        fs_type = 16;
        file_count = fat16_list_root((FAT16Entry*)file_entries, 32);
    }
}

void explorer_open_file(int index) {
    if (index < 0 || index >= file_count) return;
    
    uint8_t temp_buf[1024]; 
    FAT32Entry* entry = &file_entries[index];
    if (entry->attr & 0x10) return; 
    
    uint32_t read_size = (entry->size > 1024) ? 1024 : entry->size;
    
    if (fs_type == 32) {
        fat32_init(selected_drive);
        fat32_read_file(entry, temp_buf);
    } else {
        fat16_init(selected_drive);
        fat16_read_file((FAT16Entry*)entry, temp_buf);
    }
    
    notepad_set_content((char*)temp_buf, read_size);
    win_notepad.open = 1;
    win_notepad.minimized = 0;
    force_render_frame = 1;
}

void draw_explorer() {
    if (!win_explorer.open || win_explorer.minimized) return;
    draw_window_frame(&win_explorer);

    // Sidebar
    int sidebar_w = 100;
    draw_rect(win_explorer.x, win_explorer.y + 20, sidebar_w, win_explorer.h - 20, current_theme ? 0xEEEEEE : 0x252525);
    draw_rect(win_explorer.x + sidebar_w - 1, win_explorer.y + 20, 1, win_explorer.h - 20, 0x888888);

    // Drive Buttons
    int btn_count = 0;
    // IDE
    for (int d = 0; d < 2; d++) {
        if (!drives_present[d]) continue;
        int bx = win_explorer.x + 5;
        int by = win_explorer.y + 40 + (btn_count * 30);
        uint32_t bg = (selected_drive == d) ? (current_theme ? 0xCCCCCC : 0x444444) : (current_theme ? 0xEEEEEE : 0x252525);
        draw_rect(bx, by, sidebar_w - 10, 25, bg);
        char dname[12];
        dname[0] = 'I'; dname[1] = 'D'; dname[2] = 'E'; dname[3] = ' '; dname[4] = d + '0'; dname[5] = '\0';
        draw_string(dname, bx + 5, by + 8, get_text_color());
        btn_count++;
    }
    // SATA
    for (int d = 0; d < 4; d++) {
        if (!drives_present[d+2]) continue;
        int bx = win_explorer.x + 5;
        int by = win_explorer.y + 40 + (btn_count * 30);
        uint32_t bg = (selected_drive == (d+2)) ? (current_theme ? 0xCCCCCC : 0x444444) : (current_theme ? 0xEEEEEE : 0x252525);
        draw_rect(bx, by, sidebar_w - 10, 25, bg);
        char dname[12];
        dname[0] = 'S'; dname[1] = 'A'; dname[2] = 'T'; dname[3] = 'A'; dname[4] = ' '; dname[5] = d + '0'; dname[6] = '\0';
        draw_string(dname, bx + 5, by + 8, get_text_color());
        btn_count++;
    }

    int ex = win_explorer.x + sidebar_w + 20;
    int ey = win_explorer.y + 40;

    if (file_count == 0) {
        draw_string("Empty or No Disk.", ex, ey, get_text_color());
    } else {
        for (int i = 0; i < file_count; i++) {
            char name_buf[13];
            int ni = 0;
            for (int k = 0; k < 8; k++) {
                if (file_entries[i].name[k] != ' ') name_buf[ni++] = file_entries[i].name[k];
            }
            if (!(file_entries[i].attr & 0x10)) {
                name_buf[ni++] = '.';
                for (int k = 0; k < 3; k++) {
                    if (file_entries[i].name[8+k] != ' ') name_buf[ni++] = file_entries[i].name[8+k];
                }
            }
            name_buf[ni] = '\0';

            draw_rect(ex - 5, ey - 12, 250, 18, current_theme ? 0xDDDDDD : 0x333333);
            draw_string(name_buf, ex, ey, get_text_color());
            ey += 22;
            if (ey > win_explorer.y + win_explorer.h - 20) break;
        }
    }
}
