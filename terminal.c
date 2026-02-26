#include "apps.h"
#include "disk.h"
#include "fat16.h"
#include "fat32.h"
#include "ahci.h"
#include <stddef.h>

// --- Terminal Window ---
Window win_terminal = {100, 100, 620, 380, 0, 0, 0, 100, 100, 620, 380, "Terminal"};

// --- Mount Table ---
#define MAX_MOUNTS 8
typedef struct {
    char path[32];
    uint8_t drive;
    int fs_type; // 16 or 32
} MountPoint;

static MountPoint mounts[MAX_MOUNTS];
static int mount_count = 0;

// --- Terminal State ---
#define TERM_COLS 78
#define TERM_ROWS 20
#define TERM_MAX_LINES 200

static char lines[TERM_MAX_LINES][TERM_COLS + 1];
static int line_count = 0;
static char input_buf[128];
static int input_len = 0;

// --- String Helpers ---
static int str_len(const char* s) { int i = 0; while (s[i]) i++; return i; }
static void str_copy(char* dst, const char* src) { while (*src) *dst++ = *src++; *dst = 0; }
static int str_cmp(const char* a, const char* b) { while (*a && *b && *a == *b) { a++; b++; } return *a - *b; }
static int str_ncmp(const char* a, const char* b, int n) { while (n-- && *a && *b && *a == *b) { a++; b++; } return n < 0 ? 0 : *a - *b; }

static char to_upper(char c) { if (c >= 'a' && c <= 'z') return c - ('a' - 'A'); return c; }

static int str_case_cmp(const char* a, const char* b) {
    while (*a && *b) {
        char ca = to_upper(*a);
        char cb = to_upper(*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return to_upper(*a) - to_upper(*b);
}

static void int_to_str(int v, char* buf) {
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
    char tmp[12]; int i = 0, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    while (v > 0) { tmp[i++] = '0' + (v % 10); v /= 10; }
    if (neg) tmp[i++] = '-';
    int j = 0; while (--i >= 0) buf[j++] = tmp[i];
    buf[j] = 0;
}

// --- Output Functions ---
static void term_print(const char* s) {
    if (line_count >= TERM_MAX_LINES) {
        // Scroll: shift lines up by one
        for (int i = 0; i < TERM_MAX_LINES - 1; i++)
            str_copy(lines[i], lines[i+1]);
        line_count = TERM_MAX_LINES - 1;
    }
    int l = str_len(s);
    if (l > TERM_COLS) l = TERM_COLS;
    for (int i = 0; i < l; i++) lines[line_count][i] = s[i];
    lines[line_count][l] = 0;
    line_count++;
}

static void term_clear() {
    line_count = 0;
    for (int i = 0; i < TERM_MAX_LINES; i++) lines[i][0] = 0;
}

// --- Format a FAT file entry name into a human-readable 8.3 string ---
static void format_fat_name(const char* raw, char* out) {
    int ni = 0;
    for (int k = 0; k < 8; k++) {
        if (raw[k] != ' ') out[ni++] = raw[k];
    }
    // Check if extension exists
    int has_ext = 0;
    for (int k = 8; k < 11; k++) if (raw[k] != ' ') { has_ext = 1; break; }
    if (has_ext) {
        out[ni++] = '.';
        for (int k = 8; k < 11; k++) if (raw[k] != ' ') out[ni++] = raw[k];
    }
    out[ni] = 0;
}

// --- Command: lsdisk ---
static void cmd_lsdisk() {
    int found = 0;
    for (int d = 0; d < 2; d++) {
        if (ata_drive_exists(d)) {
            char buf[20] = "/dev/disk";
            buf[9] = '0' + d; buf[10] = 0;
            term_print(buf);
            found = 1;
        }
    }
    for (int d = 0; d < 32; d++) {
        if (ahci_drive_exists(d)) {
            char buf[20] = "/dev/sata";
            buf[9] = '0' + (d / 10);
            if (buf[9] == '0') {
                buf[9] = '0' + d;
                buf[10] = 0;
            } else {
                buf[10] = '0' + (d % 10);
                buf[11] = 0;
            }
            term_print(buf);
            found = 1;
        }
    }
    if (!found) term_print("No disks found.");
}

// --- Command: ls on a drive ---
static void ls_drive(uint8_t drive) {
    FAT32Entry entries[32];
    int count = 0;
    int fs = 16;

    if (fat32_init(drive)) {
        fs = 32;
        count = fat32_list_root(entries, 32);
    } else {
        fat16_init(drive);
        fs = 16;
        count = fat16_list_root((FAT16Entry*)entries, 32);
    }

    if (count == 0) {
        term_print("  (empty or unreadable)");
        return;
    }

    for (int i = 0; i < count; i++) {
        char name[14]; format_fat_name(entries[i].name, name);
        char line[TERM_COLS + 1];
        int pos = 0;
        // Prefix: [DIR] or [   ]
        if (entries[i].attr & 0x10) {
            line[pos++]='['; line[pos++]='D'; line[pos++]='I'; line[pos++]='R'; line[pos++]=']';
        } else {
            line[pos++]=' '; line[pos++]=' '; line[pos++]=' '; line[pos++]=' '; line[pos++]=' ';
        }
        line[pos++] = ' ';
        int nl = str_len(name);
        for (int k = 0; k < nl && pos < TERM_COLS - 12; k++) line[pos++] = name[k];
        // Pad to column 20
        while (pos < 22) line[pos++] = ' ';
        // Size
        if (!(entries[i].attr & 0x10)) {
            char sz[12]; int_to_str(entries[i].size, sz);
            int sl = str_len(sz);
            for (int k = 0; k < sl && pos < TERM_COLS; k++) line[pos++] = sz[k];
            line[pos++] = 'B';
        } else {
            line[pos++] = '<'; line[pos++] = 'D'; line[pos++] = 'I'; line[pos++] = 'R'; line[pos++] = '>';
        }
        line[pos] = 0;
        term_print(line);
    }
    (void)fs;
}

// --- Command: ls ---
static void cmd_ls(const char* path) {
    if (str_len(path) == 0) {
        term_print("Usage: ls /dev/diskN or ls /mountpoint");
        return;
    }
    // Check if it's /dev/diskN or /dev/sataN
    if (str_ncmp(path, "/dev/disk", 9) == 0) {
        int drive = path[9] - '0';
        if (drive < 0 || drive > 1) { term_print("Invalid disk."); return; }
        if (!disk_drive_exists(drive)) { term_print("Disk not present."); return; }
        ls_drive(drive);
        return;
    }
    if (str_ncmp(path, "/dev/sata", 9) == 0) {
        int d = 0;
        if (path[10] >= '0' && path[10] <= '9') d = (path[9]-'0')*10 + (path[10]-'0');
        else d = path[9]-'0';
        if (d < 0 || d > 31) { term_print("Invalid SATA port."); return; }
        if (!disk_drive_exists(d + 2)) { term_print("SATA disk not present."); return; }
        ls_drive(d + 2);
        return;
    }
    // Check mount table
    for (int i = 0; i < mount_count; i++) {
        if (str_case_cmp(mounts[i].path, path) == 0) {
            ls_drive(mounts[i].drive);
            return;
        }
    }
    term_print("Path not found. Mount it first with: mount /dev/diskN /path");
}

// --- Command: mount ---
static void cmd_mount(const char* dev, const char* mount_path) {
    if (str_len(dev) == 0 || str_len(mount_path) == 0) {
        term_print("Usage: mount /dev/diskN /mountpoint");
        return;
    }
    int drive = -1;
    if (str_ncmp(dev, "/dev/disk", 9) == 0) {
        int d = dev[9] - '0';
        if (d >= 0 && d <= 1) drive = d;
    } else if (str_ncmp(dev, "/dev/sata", 9) == 0) {
        int d = 0;
        if (dev[10] >= '0' && dev[10] <= '9') d = (dev[9]-'0')*10 + (dev[10]-'0');
        else d = dev[9]-'0';
        if (d >= 0 && d <= 31) drive = d + 2;
    }

    if (drive == -1) {
        term_print("Invalid device. Use /dev/diskN or /dev/sataN.");
        return;
    }
    if (!disk_drive_exists(drive)) {
        term_print("Disk not present.");
        return;
    }
    if (mount_count >= MAX_MOUNTS) {
        term_print("Mount table full.");
        return;
    }
    // Check if already mounted
    for (int i = 0; i < mount_count; i++) {
        if (str_cmp(mounts[i].path, mount_path) == 0) {
            mounts[i].drive = drive;
            term_print("Remounted.");
            return;
        }
    }
    str_copy(mounts[mount_count].path, mount_path);
    mounts[mount_count].drive = drive;
    int fs = fat32_init(drive) ? 32 : 16;
    mounts[mount_count].fs_type = fs;
    mount_count++;

    // Print confirmation
    char msg[80];
    int p = 0;
    const char* mp = "Mounted ";
    while (*mp) msg[p++] = *mp++;
    const char* d = dev; while (*d) msg[p++] = *d++;
    msg[p++] = ' '; msg[p++] = '-'; msg[p++] = '>'; msg[p++] = ' ';
    const char* mp2 = mount_path; while (*mp2) msg[p++] = *mp2++;
    msg[p++] = ' '; msg[p++] = '(';
    if (fs == 32) { msg[p++]='F'; msg[p++]='A'; msg[p++]='T'; msg[p++]='3'; msg[p++]='2'; }
    else { msg[p++]='F'; msg[p++]='A'; msg[p++]='T'; msg[p++]='1'; msg[p++]='6'; }
    msg[p++] = ')'; msg[p] = 0;
    term_print(msg);
}

// --- Command: clear ---
static void cmd_clear() {
    term_clear();
}

// --- Command Parser ---
static char* next_token(char* s, char* tok) {
    // Skip spaces
    while (*s == ' ') s++;
    int i = 0;
    while (*s && *s != ' ') tok[i++] = *s++;
    tok[i] = 0;
    return s;
}

// --- Command: cat ---
static void cmd_cat(const char* path) {
    if (str_len(path) == 0) {
        term_print("Usage: cat /path/to/file");
        return;
    }

    uint8_t drive = 255;
    const char* filename = NULL;

    // 1. Check for /dev/diskN/FILE or /dev/sataN/FILE
    if (str_ncmp(path, "/dev/disk", 9) == 0) {
        drive = path[9] - '0';
        if (drive > 1) { term_print("Invalid disk."); return; }
        if (path[10] == '/') filename = &path[11];
        else { term_print("Usage: cat /dev/diskN/filename"); return; }
    } else if (str_ncmp(path, "/dev/sata", 9) == 0) {
        int d = 0;
        if (path[10] >= '0' && path[10] <= '9') {
            d = (path[9]-'0')*10 + (path[10]-'0');
            if (path[11] == '/') filename = &path[12];
        } else {
            d = path[9]-'0';
            if (path[10] == '/') filename = &path[11];
        }
        if (d < 0 || d > 31) { term_print("Invalid SATA."); return; }
        drive = d + 2;
    }
    // 2. Check mount table
    else {
        for (int i = 0; i < mount_count; i++) {
            int mlen = str_len(mounts[i].path);
            if (str_ncmp(path, mounts[i].path, mlen) == 0 && path[mlen] == '/') {
                drive = mounts[i].drive;
                filename = &path[mlen + 1];
                break;
            }
        }
    }

    if (drive == 255 || filename == NULL || str_len(filename) == 0) {
        term_print("File not found or invalid path.");
        return;
    }

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
        char name[14]; format_fat_name(entries[i].name, name);
        if (str_case_cmp(name, filename) == 0) {
            if (entries[i].attr & 0x10) { term_print("Is a directory."); return; }
            found_idx = i;
            break;
        }
    }

    if (found_idx == -1) {
        term_print("File not found on disk.");
        return;
    }

    // Read and print (max 4KB for terminal display)
    static uint8_t file_buf[4096];
    if (fs == 32) fat32_read_file(&entries[found_idx], file_buf);
    else fat16_read_file((FAT16Entry*)&entries[found_idx], file_buf);

    uint32_t size = entries[found_idx].size;
    if (size > 4095) size = 4095;
    file_buf[size] = 0;

    // Split by lines
    char line[TERM_COLS + 1];
    int lp = 0;
    for (uint32_t i = 0; i < size; i++) {
        if (file_buf[i] == '\n' || lp >= TERM_COLS) {
            line[lp] = 0;
            term_print(line);
            lp = 0;
        } else if (file_buf[i] != '\r') {
            line[lp++] = file_buf[i];
        }
    }
    if (lp > 0) {
        line[lp] = 0;
        term_print(line);
    }
}

static void execute_command(char* cmd_str) {
    // Echo the command
    char prompt[TERM_COLS + 1];
    int p = 0;
    prompt[p++] = '$'; prompt[p++] = ' ';
    int cl = str_len(cmd_str);
    for (int i = 0; i < cl && p < TERM_COLS; i++) prompt[p++] = cmd_str[i];
    prompt[p] = 0;
    term_print(prompt);

    char tok1[64], tok2[64], tok3[64];
    char* rest = next_token(cmd_str, tok1);
    rest = next_token(rest, tok2);
    next_token(rest, tok3);

    if (str_case_cmp(tok1, "lsdisk") == 0) {
        cmd_lsdisk();
    } else if (str_case_cmp(tok1, "ls") == 0) {
        cmd_ls(tok2);
    } else if (str_case_cmp(tok1, "mount") == 0) {
        cmd_mount(tok2, tok3);
    } else if (str_case_cmp(tok1, "cat") == 0) {
        cmd_cat(tok2);
    } else if (str_case_cmp(tok1, "clear") == 0) {
        cmd_clear();
    } else if (str_len(tok1) > 0) {
        char errmsg[80] = "Unknown command: ";
        int el = str_len(errmsg);
        int tl = str_len(tok1);
        for (int i = 0; i < tl && el < 78; i++) errmsg[el++] = tok1[i];
        errmsg[el] = 0;
        term_print(errmsg);
    }
}

// --- Public: Handle Keyboard Input ---
void terminal_handle_key(char c) {
    if (c == '\r' || c == '\n') {
        input_buf[input_len] = 0;
        execute_command(input_buf);
        input_len = 0;
        input_buf[0] = 0;
        force_render_frame = 1;
    } else if (c == '\b') {
        if (input_len > 0) input_len--;
        force_render_frame = 1;
    } else {
        if (input_len < 127) {
            input_buf[input_len++] = c;
            input_buf[input_len] = 0;
            force_render_frame = 1;
        }
    }
}

// --- Public: Draw Terminal ---
void draw_terminal() {
    if (!win_terminal.open || win_terminal.minimized) return;
    draw_window_frame(&win_terminal);

    int cx = win_terminal.x + 8;
    int cy = win_terminal.y + 22;
    int line_h = 14;
    int max_visible = (win_terminal.h - 50) / line_h;

    // Fill background
    draw_rect(win_terminal.x, win_terminal.y + 20, win_terminal.w, win_terminal.h - 20, 0x0D1117);

    // Calculate scroll: show the last max_visible lines
    int start = line_count - max_visible;
    if (start < 0) start = 0;

    for (int i = start; i < line_count; i++) {
        draw_string(lines[i], cx, cy, 0xAAFFAA);
        cy += line_h;
    }

    // Draw input line
    int iy = win_terminal.y + win_terminal.h - 22;
    draw_rect(win_terminal.x, iy - 2, win_terminal.w, 20, 0x161B22);
    // Prompt
    draw_string("$ ", cx, iy, 0x58A6FF);
    draw_string(input_buf, cx + 14, iy, 0xFFFFFF);
    // Blinking cursor effect (always on for simplicity)
    int cursor_x = cx + 14 + input_len * 8;
    draw_rect(cursor_x, iy, 7, 12, 0xFFFFFF);
}
