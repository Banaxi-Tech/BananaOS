#include <stdint.h>
#include <stddef.h>
#include "font.h"
#include "apps.h"
#include "ahci.h"
#include "pci.h"


// ===== Forward Declarations =====
void kstrcpy(char* dest, const char* src);

void acpi_shutdown();
void safe_power_message();
void reboot_system();

static inline void outw(uint16_t port, uint16_t val);

// --- I/O Ports ---
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ( "inb %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

// --- Multiboot Info ---
struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint8_t color_info[6];
} __attribute__((packed));

struct multiboot_mod_list {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;
    uint32_t pad;
};



int acpi_supported = 0;
void* wallpaper_ptr = NULL;

// --- VESA Graphics ---
uint32_t* fb = NULL;          
uint32_t* backbuffer = NULL;  
uint32_t scr_width = 1024;
uint32_t scr_height = 768;
uint32_t pitch = 0;
uint8_t bpp = 32;


void draw_pixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= (int)scr_width || y < 0 || y >= (int)scr_height || !backbuffer) return;
    uint32_t offset = (y * pitch) + (x * (bpp / 8));
    *(uint32_t*)((uint8_t*)backbuffer + offset) = color;
}

uint32_t get_pixel(int x, int y) {
    if (x < 0 || x >= (int)scr_width || y < 0 || y >= (int)scr_height || !backbuffer) return 0;
    uint32_t offset = (y * pitch) + (x * 4);
    return *(uint32_t*)((uint8_t*)backbuffer + offset);
}

void draw_rect(int x, int y, int w, int h, uint32_t color) {
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            draw_pixel(x + j, y + i, color);
        }
    }
}

int detect_acpi() {
    char* rsdp;

    // Search EBDA (0x000E0000 - 0x000FFFFF)
    for (rsdp = (char*)0x000E0000; rsdp < (char*)0x00100000; rsdp += 16) {
        if (rsdp[0] == 'R' && rsdp[1] == 'S' && rsdp[2] == 'D' &&
            rsdp[3] == ' ' && rsdp[4] == 'P' && rsdp[5] == 'T' &&
            rsdp[6] == 'R' && rsdp[7] == ' ') {
            return 1;
        }
    }

    return 0;
}

void draw_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha) {
    if (alpha == 255) { draw_rect(x, y, w, h, color); return; }
    if (alpha == 0) return;
    
    uint32_t src_r = (color >> 16) & 0xFF;
    uint32_t src_g = (color >> 8) & 0xFF;
    uint32_t src_b = color & 0xFF;
    
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            int px = x + j;
            int py = y + i;
            if (px < 0 || px >= (int)scr_width || py < 0 || py >= (int)scr_height) continue;
            
            uint32_t dest_col = get_pixel(px, py);
            uint32_t dest_r = (dest_col >> 16) & 0xFF;
            uint32_t dest_g = (dest_col >> 8) & 0xFF;
            uint32_t dest_b = dest_col & 0xFF;
            
            uint32_t res_r = (src_r * alpha + dest_r * (255 - alpha)) >> 8;
            uint32_t res_g = (src_g * alpha + dest_g * (255 - alpha)) >> 8;
            uint32_t res_b = (src_b * alpha + dest_b * (255 - alpha)) >> 8;
            
            draw_pixel(px, py, (res_r << 16) | (res_g << 8) | res_b);
        }
    }
}

void draw_rounded_rect_alpha(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha) {
    uint32_t src_r = (color >> 16) & 0xFF;
    uint32_t src_g = (color >> 8) & 0xFF;
    uint32_t src_b = color & 0xFF;

    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            int px = x + j;
            int py = y + i;
            if (px < 0 || px >= (int)scr_width || py < 0 || py >= (int)scr_height) continue;

            // Corner Clipping Logic
            int is_corner = 0;
            int cx = -1, cy = -1;

            if (j < r && i < r) { cx = x + r; cy = y + r; is_corner = 1; }
            else if (j >= w - r && i < r) { cx = x + w - r - 1; cy = y + r; is_corner = 1; }
            else if (j < r && i >= h - r) { cx = x + r; cy = y + h - r - 1; is_corner = 1; }
            else if (j >= w - r && i >= h - r) { cx = x + w - r - 1; cy = y + h - r - 1; is_corner = 1; }

            if (is_corner) {
                int dx = px - cx;
                int dy = py - cy;
                if (dx * dx + dy * dy > r * r) continue; // Clip pixel
            }

            if (alpha == 255) {
                draw_pixel(px, py, color);
            } else {
                uint32_t dest_col = get_pixel(px, py);
                uint32_t dest_r = (dest_col >> 16) & 0xFF;
                uint32_t dest_g = (dest_col >> 8) & 0xFF;
                uint32_t dest_b = dest_col & 0xFF;

                uint32_t res_r = (src_r * alpha + dest_r * (255 - alpha)) >> 8;
                uint32_t res_g = (src_g * alpha + dest_g * (255 - alpha)) >> 8;
                uint32_t res_b = (src_b * alpha + dest_b * (255 - alpha)) >> 8;

                draw_pixel(px, py, (res_r << 16) | (res_g << 8) | res_b);
            }
        }
    }
}

void clear_screen(uint32_t color);
uint32_t get_wallpaper_color();

void draw_wallpaper() {
    if (!wallpaper_ptr) {
        clear_screen(get_wallpaper_color());
        return;
    }
    
    uint8_t* bmp8 = (uint8_t*)wallpaper_ptr;
    if (bmp8[0] != 'B' || bmp8[1] != 'M') {
        clear_screen(get_wallpaper_color());
        return;
    }
    
    uint32_t offset = *(uint32_t*)&bmp8[10];
    uint32_t w = *(uint32_t*)&bmp8[18];
    uint32_t h = *(uint32_t*)&bmp8[22];
    uint16_t bpp_img = *(uint16_t*)&bmp8[28];
    
    if (bpp_img != 24 && bpp_img != 32) {
        clear_screen(get_wallpaper_color());
        return;
    }
    
    uint8_t* pixel_data = bmp8 + offset;
    int row_stride = (w * (bpp_img / 8) + 3) & ~3;
    
    for (int y = 0; y < (int)h; y++) {
        int draw_y = scr_height - 1 - y;
        if (draw_y < 0 || draw_y >= (int)scr_height) continue;
        
        uint8_t* row = pixel_data + (y * row_stride);
        for (int x = 0; x < (int)w; x++) {
            if (x >= (int)scr_width) break;
            
            uint32_t color = 0;
            if (bpp_img == 24) {
                uint8_t b_col = row[x*3];
                uint8_t g_col = row[x*3+1];
                uint8_t r_col = row[x*3+2];
                color = (r_col << 16) | (g_col << 8) | b_col;
            } else {
                uint8_t b_col = row[x*4];
                uint8_t g_col = row[x*4+1];
                uint8_t r_col = row[x*4+2];
                color = (r_col << 16) | (g_col << 8) | b_col;
            }
            draw_pixel(x, draw_y, color);
        }
    }
}

void clear_screen(uint32_t color) {
    if (!backbuffer) return;
    draw_rect(0, 0, scr_width, scr_height, color);
}

void swap_buffers() {
    if (!fb || !backbuffer) return;
    uint32_t dwords = (scr_height * pitch) / 4;
    void* dest = fb;
    void* src = backbuffer;
    asm volatile("rep movsl" : "+D"(dest), "+S"(src), "+c"(dwords) : : "memory");
}

void swap_rect(int rx, int ry, int rw, int rh) {
    if (!fb || !backbuffer) return;
    for (int y = ry; y < ry + rh; y++) {
        if (y < 0 || y >= (int)scr_height) continue;
        int cx = rx;
        int copy_w = rw;
        if (cx < 0) { copy_w += cx; cx = 0; }
        if (cx + copy_w > (int)scr_width) copy_w = scr_width - cx;
        if (copy_w <= 0) continue;
        
        uint32_t offset = (y * pitch) + (cx * (bpp / 8));
        void* dest = (uint8_t*)fb + offset;
        void* src = (uint8_t*)backbuffer + offset;
        uint32_t bytes_to_copy = copy_w * (bpp / 8);
        asm volatile("rep movsb" : "+D"(dest), "+S"(src), "+c"(bytes_to_copy) : : "memory");
    }
}

// --- Text Drawing ---
void draw_char(char c, int x, int y, uint32_t fg_color) {
    if (c < 0) return;
    const uint8_t *glyph = font8x8[(int)c];
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (glyph[row] & (1 << (7 - col))) {
                draw_pixel(x + col, y + row, fg_color);
            }
        }
    }
}

void draw_string(const char* str, int x, int y, uint32_t fg) {
    int cur_x = x;
    while (*str) {
        draw_char(*str, cur_x, y, fg);
        cur_x += 8;
        str++;
    }
}

uint8_t read_cmos(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

int is_updating_rtc() {
    outb(0x70, 0x0A);
    return (inb(0x71) & 0x80);
}

void get_rtc_time(int* h, int* m, int* s) {
    while (is_updating_rtc());

    *s = read_cmos(0x00);
    *m = read_cmos(0x02);
    *h = read_cmos(0x04);

    uint8_t registerB = read_cmos(0x0B);

    // Convert BCD to binary if necessary
    if (!(registerB & 0x04)) {
        *s = (*s & 0x0F) + ((*s / 16) * 10);
        *m = (*m & 0x0F) + ((*m / 16) * 10);
        *h = ((*h & 0x0F) + (((*h & 0x70) / 16) * 10)) | (*h & 0x80);
    }

    // Convert 12h to 24h if necessary
    if (!(registerB & 0x02) && (*h & 0x80)) {
        *h = ((*h & 0x7F) + 12) % 24;
    }
}

void itoa(int val, char* buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    int i = 0;
    int is_neg = val < 0;
    if (is_neg) val = -val;
    do { buf[i++] = (val % 10) + '0'; } while ((val /= 10) > 0);
    if (is_neg) buf[i++] = '-';
    for (int j = 0, k = i - 1; j < k; j++, k--) {
        char temp = buf[j]; buf[j] = buf[k]; buf[k] = temp;
    }
    buf[i] = '\0';
}

// --- App State (moved to apps.h/individual files) ---
int topbar_menu_open = 0;

int dialog_mode = 0;
char system_version[] = "1.1.1";
char system_build[] = "Build 109";
int mouse_x = 512;
int mouse_y = 384;
uint8_t mouse_cycle = 0;
int8_t mouse_byte[3];
int mouse_clicked = 0;
int mouse_down = 0;
int force_render_frame = 1;
uint32_t timer_ticks = 0;
uint32_t last_click_tick = 0;

int is_dragging = 0;
Window* dragged_window = NULL;
int drag_offset_x = 0;
int drag_offset_y = 0;

// CPU and App-specific state moved to separate files

// --- Keyboard Scancode Translation ---
int shift_active = 0;
const char scancode_ascii[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0,
    '*',  0, ' ',   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0, '-',   0,   5,   0, '+',   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

const char scancode_ascii_shift[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',   0,
    '*',  0, ' ',   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0, '-',   0,   5,   0, '+',   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

// --- Theme Utility ---
uint32_t get_wallpaper_color() { return current_theme ? 0xD3D3D3 : 0x282C34; }
uint32_t get_window_color() { return current_theme ? 0xFFFFFF : 0x1E1E1E; }
uint32_t get_text_color() { return current_theme ? 0x000000 : 0xFFFFFF; }

// --- UI Interaction Logic ---
int get_dock_hover_index() {
    int dock_w = 400;
    int dock_h = 60;
    int dock_x = (scr_width - dock_w) / 2;
    int dock_y = scr_height - dock_h - 10;
    int spacing = 60;
    int base_size = 30;
    int start_x = dock_x + (dock_w - (4*spacing)) / 2 - base_size/2;

    for (int i=0; i<5; i++) {
        int bx = start_x + (i * spacing);
        int by = dock_y + 15;
        // Expand hit box gracefully
        if (mouse_x >= bx - 10 && mouse_x <= bx + base_size + 10 &&
            mouse_y >= by - 20 && mouse_y <= by + base_size + 20) {
            return i;
        }
    }
    return -1;
}

Window* get_window_at_pos(int x, int y, int titlebar_only) {
    Window* windows[5] = {&win_terminal, &win_settings, &win_explorer, &win_notepad, &win_calc};
    for (int i=0; i<5; i++) {
        Window* w = windows[i];
        if (w->open && !w->minimized) {
            int h = titlebar_only ? 20 : w->h;
            if (x >= w->x && x <= w->x + w->w && y >= w->y && y <= w->y + h) return w;
        }
    }
    return NULL;
}

int handle_window_controls(Window* win) {
    if (mouse_y >= win->y + 5 && mouse_y <= win->y + 15) {
        if (mouse_x >= win->x + 10 && mouse_x <= win->x + 20) {
            win->open = 0; return 1;
        }
        if (mouse_x >= win->x + 25 && mouse_x <= win->x + 35) {
            win->minimized = 1; return 1;
        }
        if (mouse_x >= win->x + 40 && mouse_x <= win->x + 50) {
            if (win->maximized) {
                win->x = win->old_x; win->y = win->old_y; win->w = win->old_w; win->h = win->old_h;
                win->maximized = 0;
            } else {
                win->old_x = win->x; win->old_y = win->y; win->old_w = win->w; win->old_h = win->h;
                win->x = 0; win->y = 0; win->w = scr_width; win->h = scr_height - 70;
                win->maximized = 1;
            }
            return 1;
        }
    }
    return 0;
}

void check_click() {
    // 1. Core Click Timing
    if (timer_ticks - last_click_tick < 60) { // ~600ms
        // Note: is_double_click removed as per single-click requirement
    }
    last_click_tick = timer_ticks;


    // --- Topbar BananaOS menu ---
if (mouse_y >= 0 && mouse_y <= 25) {
    // BananaOS text hitbox (approximate width 80px)
    if (mouse_x >= 10 && mouse_x <= 100) {
        topbar_menu_open = !topbar_menu_open;
        force_render_frame = 1;
        return;
    }
    }

    // --- About click inside dropdown ---
    if (topbar_menu_open) {
        int menu_x = 10;
        int menu_y = 25;

        // About
        if (mouse_x >= menu_x && mouse_x <= menu_x + 170 &&
            mouse_y >= menu_y && mouse_y <= menu_y + 30) {

            topbar_menu_open = 0;
            dialog_mode = 1;
            kstrcpy(win_dialog.title, "About BananaOS");
            win_dialog.open = 1;
            force_render_frame = 1;
            return;
        }

        // Shutdown
        if (mouse_x >= menu_x && mouse_x <= menu_x + 170 &&
            mouse_y >= menu_y + 30 && mouse_y <= menu_y + 60) {

            topbar_menu_open = 0;

            if (acpi_supported)
                acpi_shutdown();
            else
                safe_power_message();

            return;
        }

        // Reboot
        if (mouse_x >= menu_x && mouse_x <= menu_x + 170 &&
            mouse_y >= menu_y + 60 && mouse_y <= menu_y + 90) {

            topbar_menu_open = 0;
            reboot_system();
            return;
        }

        topbar_menu_open = 0;

        dialog_mode = 1;
        kstrcpy(win_dialog.title, "About BananaOS");
        win_dialog.open = 1;
        win_dialog.w = 400;
        win_dialog.h = 220;
        win_dialog.x = (scr_width - 400) / 2;
        win_dialog.y = (scr_height - 220) / 2;

        force_render_frame = 1;
        return;
    }
    // 2. Dock Checks
    int hover_idx = get_dock_hover_index();
    if (hover_idx == 0) { 
        win_terminal.open = !win_terminal.open; win_terminal.minimized = 0; force_render_frame = 1; return; 
    }
    if (hover_idx == 1) { 
        win_calc.open = !win_calc.open; win_calc.minimized = 0; force_render_frame = 1; return; 
    }
    if (hover_idx == 2) { 
        win_notepad.open = !win_notepad.open; win_notepad.minimized = 0; force_render_frame = 1; return; 
    }
    if (hover_idx == 3) { 
        win_explorer.open = !win_explorer.open; win_explorer.minimized = 0; force_render_frame = 1; return; 
    }
    if (hover_idx == 4) { 
        win_settings.open = !win_settings.open; win_settings.minimized = 0; force_render_frame = 1; return; 
    }
    
    Window* clicked_win = get_window_at_pos(mouse_x, mouse_y, 0);

    if (clicked_win == &win_explorer) {
        // sidebar click?
        int sidebar_w = 100;
        if (mouse_x >= win_explorer.x && mouse_x <= win_explorer.x + sidebar_w) {
            extern int drives_present[34];
            int btn_count = 0;
            // Scan IDE
            for (int d = 0; d < 2; d++) {
                if (!drives_present[d]) continue;
                int by = win_explorer.y + 40 + (btn_count * 30);
                if (mouse_y >= by && mouse_y <= by + 25) {
                    explorer_init(d);
                    force_render_frame = 1;
                    return;
                }
                btn_count++;
            }
            // Scan SATA
            for (int d = 0; d < 4; d++) {
                if (!drives_present[d+2]) continue;
                int by = win_explorer.y + 40 + (btn_count * 30);
                if (mouse_y >= by && mouse_y <= by + 25) {
                    explorer_init(d+2);
                    force_render_frame = 1;
                    return;
                }
                btn_count++;
            }
        }

        // 3. Explorer File Integration
        int ex = win_explorer.x + sidebar_w + 20;
        int ey = win_explorer.y + 40;
        for (int i = 0; i < 16; i++) {
            int row_y = ey + (i * 22);
            if (mouse_x >= ex - 5 && mouse_x <= ex + 245 && mouse_y >= row_y - 12 && mouse_y <= row_y + 10) {
                explorer_open_file(i);
                return;
            }
        }
    }
    if (clicked_win) {
        if (handle_window_controls(clicked_win)) {
            force_render_frame = 1;
            return;
        }
        if (mouse_y <= clicked_win->y + 20) {
            if (!clicked_win->maximized) {
                is_dragging = 1;
                dragged_window = clicked_win;
                drag_offset_x = mouse_x - clicked_win->x;
                drag_offset_y = mouse_y - clicked_win->y;
            }
            return;
        }
        if (clicked_win == &win_settings) {
            int set_x = win_settings.x; int set_y = win_settings.y;
            
            // Sidebar Hitboxes (X: 10 to 110 inside window)
            if (mouse_x >= set_x && mouse_x <= set_x + 120) {
                if (mouse_y >= set_y + 30 && mouse_y <= set_y + 60) {
                    settings_page = 0; force_render_frame = 1; return;
                }
                if (mouse_y >= set_y + 65 && mouse_y <= set_y + 95) {
                    settings_page = 1; force_render_frame = 1; return;
                }
            }
            
            // Content Hitboxes (Right Side)
            if (settings_page == 0) { // Personalization
                // Dark Mode Hitbox
                if (mouse_x >= set_x + 140 && mouse_x <= set_x + 240 && mouse_y >= set_y + 65 && mouse_y <= set_y + 95) {
                    current_theme = 0; force_render_frame = 1; return;
                }
                // Light Mode Hitbox
                if (mouse_x >= set_x + 250 && mouse_x <= set_x + 350 && mouse_y >= set_y + 65 && mouse_y <= set_y + 95) {
                    current_theme = 1; force_render_frame = 1; return;
                }
                // Rounded Toggle Hitbox
                if (mouse_x >= set_x + 140 && mouse_x <= set_x + 240 && mouse_y >= set_y + 125 && mouse_y <= set_y + 155) {
                    rounded_dock = 1; force_render_frame = 1; return;
                }
                // Square Toggle Hitbox (Dock)
                if (mouse_x >= set_x + 250 && mouse_x <= set_x + 350 && mouse_y >= set_y + 125 && mouse_y <= set_y + 155) {
                    rounded_dock = 0; force_render_frame = 1; return;
                }
                // Rounded Toggle Hitbox (Windows)
                if (mouse_x >= set_x + 140 && mouse_x <= set_x + 240 && mouse_y >= set_y + 185 && mouse_y <= set_y + 215) {
                    rounded_win = 1; force_render_frame = 1; return;
                }
                // Square Toggle Hitbox (Windows)
                if (mouse_x >= set_x + 250 && mouse_x <= set_x + 350 && mouse_y >= set_y + 185 && mouse_y <= set_y + 215) {
                    rounded_win = 0; force_render_frame = 1; return;
                }
            }
        } else if (clicked_win == &win_calc) {
            int calc_x = win_calc.x; int calc_y = win_calc.y;
            int btn_size = 40; int gap = 6;
            int start_x = calc_x + 10; int start_y = calc_y + 70;
            const char* labels[5][4] = {
                {"AC", "+/-", "%", "/"},
                {"7",  "8",   "9", "*"},
                {"4",  "5",   "6", "-"},
                {"1",  "2",   "3", "+"},
                {"0",  "",    ".", "="}
            };
            for (int row = 0; row < 5; row++) {
                for (int col = 0; col < 4; col++) {
                    if (row == 4 && col == 1) continue;
                    int cur_w = (row == 4 && col == 0) ? (btn_size * 2 + gap) : btn_size;
                    int bx = start_x + (col * (btn_size + gap));
                    int by = start_y + (row * (btn_size + gap));
                    if (mouse_x >= bx && mouse_x <= bx + cur_w && mouse_y >= by && mouse_y <= by + btn_size) {
                        const char* key = labels[row][col];
                        if (key[0] >= '0' && key[0] <= '9') {
                            calc_current = (calc_current * 10) + (key[0] - '0');
                            itoa(calc_current, calc_display);
                        } else if (key[0] == 'A') { // AC
                            calc_display[0] = '0'; calc_display[1] = '\0';
                            calc_current = 0; calc_acc = 0; calc_op = 0;
                        } else if (key[0] == '=') {
                            calculate(); calc_op = 0;
                        } else if (key[0] != '.' && key[0] != '%' && key[0] != '+') { // basic ops
                            if (calc_op != 0) calculate(); else calc_acc = calc_current;
                            calc_op = key[0]; calc_current = 0;
                        } else if (key[0] == '+') { 
                            if (key[1] == '\0') {
                                if (calc_op != 0) calculate(); else calc_acc = calc_current;
                                calc_op = '+'; calc_current = 0;
                            }
                        }
                        force_render_frame = 1; return;
                    }
                }
            }
        }
    }

    if (win_dialog.open) {
        int btn_w = 80;
        int btn_h = 30;
        int btn_x = win_dialog.x + (win_dialog.w - btn_w) / 2;
        int btn_y = win_dialog.y + 150;
        if (mouse_x >= btn_x && mouse_x <= btn_x + btn_w && mouse_y >= btn_y && mouse_y <= btn_y + btn_h) {
            win_dialog.open = 0;
            force_render_frame = 1;
            return;
        }
    }
}

// --- View Rendering ---
void draw_pixel_fb(int x, int y, uint32_t color) {
    if (x < 0 || x >= (int)scr_width || y < 0 || y >= (int)scr_height || !fb) return;
    uint32_t offset = (y * pitch) + (x * 4);
    *(uint32_t*)((uint8_t*)fb + offset) = color;
}



void kstrcpy(char* dest, const char* src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

void draw_cursor_direct(int x, int y) {
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            draw_pixel_fb(x + j, y + i, 0xFFFFFF);
        }
    }
    draw_pixel_fb(x-1, y-1, 0x000000);
    draw_pixel_fb(x+8, y+8, 0x000000);
}

void draw_dock() {
    int dock_w = 400;
    int dock_h = 60;
    int dock_x = (scr_width - dock_w) / 2;
    int dock_y = scr_height - dock_h - 10;
    
    // Glassy dock
    if (rounded_dock) {
        draw_rounded_rect_alpha(dock_x, dock_y, dock_w, dock_h, 20, 0x303030, 160);
        // Subtle glass border top (shortened for rounding)
        draw_rect(dock_x + 20, dock_y, dock_w - 40, 1, 0x888888);
    } else {
        draw_rect_alpha(dock_x, dock_y, dock_w, dock_h, 0x303030, 160);
        // Full width border for square
        draw_rect(dock_x, dock_y, dock_w, 1, 0x888888);
        draw_rect(dock_x, dock_y + dock_h - 1, dock_w, 1, 0x222222);
    }
    
    int spacing = 60;
    int base_size = 30;
    int hover_size = 46;
    int start_x = dock_x + (dock_w - (4*spacing)) / 2 - base_size/2;

    uint32_t colors[5] = {0x000000, 0xFF9F0A, 0xFFFFFF, 0x5856D6, 0x8E8E93};
    const char* labels[5] = {"T", "C", "N", "E", "S"};
    int hover_idx = get_dock_hover_index();

    for (int i=0; i<5; i++) {
        int size = base_size;
        int y_offset = 0;
        int bx = start_x + (i * spacing);
        int by = dock_y + 15;

        if (i == hover_idx) {
            size = hover_size;
            y_offset = -12; // Animate upwards
            bx -= (hover_size - base_size) / 2; // Keep horizontally centered
        }

        draw_rect(bx, by + y_offset, size, size, colors[i]);
        uint32_t fg = (colors[i] == 0xFFFFFF) ? 0x000000 : 0xFFFFFF;
        draw_string(labels[i], bx + (size/2) - 4, by + y_offset + (size/2) - 4, fg);
    }
}

static inline uint32_t blur_pixel(uint32_t a, uint32_t b, uint32_t c,
                                  uint32_t d, uint32_t e) {
    uint32_t r = ((a>>16&255)+(b>>16&255)+(c>>16&255)+(d>>16&255)+(e>>16&255))/5;
    uint32_t g = ((a>>8&255)+(b>>8&255)+(c>>8&255)+(d>>8&255)+(e>>8&255))/5;
    uint32_t b2= ((a&255)+(b&255)+(c&255)+(d&255)+(e&255))/5;
    return (r<<16)|(g<<8)|b2;
}

void blur_rect(int x, int y, int w, int h) {
    for (int j = y+1; j < y+h-1; j++) {
        for (int i = x+1; i < x+w-1; i++) {
            uint32_t* p = (uint32_t*)(backbuffer + j*pitch + i*4);

            uint32_t up    = *(uint32_t*)(backbuffer + (j-1)*pitch + i*4);
            uint32_t down  = *(uint32_t*)(backbuffer + (j+1)*pitch + i*4);
            uint32_t left  = *(uint32_t*)(backbuffer + j*pitch + (i-1)*4);
            uint32_t right = *(uint32_t*)(backbuffer + j*pitch + (i+1)*4);
            uint32_t mid   = *p;

            *p = blur_pixel(up, down, left, right, mid);
        }
    }
}

void draw_topbar() {
    int bar_h = 25;

    // Blur what is already rendered
    blur_rect(0, 0, scr_width, bar_h);

    // Subtle glass tint (optional)
    draw_rect_alpha(0, 0, scr_width, bar_h, 0xFFFFFF, 40);

    // Bottom divider
    draw_rect(0, bar_h - 1, scr_width, 1, 0x888888);

    // Left label
    draw_string("BananaOS", 10, 6, 0xFFFFFF);

    // Clock
    int h, m, s;
    get_rtc_time(&h, &m, &s);

    char time_str[6];
    time_str[0] = (h / 10) + '0';
    time_str[1] = (h % 10) + '0';
    time_str[2] = ':';
    time_str[3] = (m / 10) + '0';
    time_str[4] = (m % 10) + '0';
    time_str[5] = '\0';

    draw_string(time_str, scr_width - 50, 6, 0xFFFFFF);
    
    // --- BananaOS Dropdown Menu ---
    if (topbar_menu_open) {
        int menu_x = 10;
        int menu_y = 25;
        int menu_w = 170;
        int menu_h = 90;

        draw_rect_alpha(menu_x, menu_y, menu_w, menu_h, 0x2E2E2E, 230);
        draw_rect(menu_x, menu_y, menu_w, 1, 0x888888);

        draw_string("About BananaOS", menu_x + 10, menu_y + 10, 0xFFFFFF);
        draw_string("Shutdown", menu_x + 10, menu_y + 35, 0xFFFFFF);
        draw_string("Reboot", menu_x + 10, menu_y + 60, 0xFFFFFF);
    }
}

void draw_window_frame(Window* win) {
    if (!win->open || win->minimized) return;

    if (rounded_win) {
        // Shadow / Glow
        draw_rounded_rect_alpha(win->x + 5, win->y + 5, win->w, win->h, 15, 0x1A1C23, 100);
        // Background
        draw_rounded_rect_alpha(win->x, win->y, win->w, win->h, 15, get_window_color(), 255);
        // Title bar
        draw_rounded_rect_alpha(win->x, win->y, win->w, 20, 15, 0x333333, 255);
        // Straight bottom for the title bar if needed, but 15px radius at top is enough
        draw_rect(win->x, win->y + 10, win->w, 10, 0x333333);
    } else {
        draw_rect(win->x + 5, win->y + 5, win->w, win->h, 0x1A1C23); 
        draw_rect(win->x, win->y, win->w, win->h, get_window_color()); 
        draw_rect(win->x, win->y, win->w, 20, 0x333333);   
    }
    
    // Control Buttons
    draw_rect(win->x + 10, win->y + 5, 10, 10, 0xFF5F56); // Close
    draw_rect(win->x + 25, win->y + 5, 10, 10, 0xFFBD2E); // Minimize
    draw_rect(win->x + 40, win->y + 5, 10, 10, 0x27C93F); // Maximize
    
    draw_string(win->title, win->x + 60, win->y + 6, 0xFFFFFF);
}

// draw_notepad, draw_calculator, draw_settings moved to separate files

// --- GDT & IDT ---
struct gdt_entry { uint16_t l; uint16_t bl; uint8_t bm; uint8_t a; uint8_t g; uint8_t bh; } __attribute__((packed));
struct gdt_ptr { uint16_t limit; uint32_t base; } __attribute__((packed));
struct gdt_entry gdt[3];
struct gdt_ptr gp;

void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].bl = (base & 0xFFFF); gdt[num].bm = (base >> 16) & 0xFF; gdt[num].bh = (base >> 24) & 0xFF;
    gdt[num].l = (limit & 0xFFFF); gdt[num].g = ((limit >> 16) & 0x0F) | (gran & 0xF0); gdt[num].a = access;
}

void gdt_install() {
    gp.limit = (sizeof(struct gdt_entry) * 3) - 1; gp.base = (uint32_t)&gdt;
    gdt_set_gate(0, 0, 0, 0, 0); gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    asm volatile("lgdt %0" : : "m" (gp));
    asm volatile( "pushl $0x08\n pushl $1f\n lret\n 1:\n mov $0x10, %%ax\n mov %%ax, %%ds\n mov %%ax, %%es\n mov %%ax, %%fs\n mov %%ax, %%gs\n mov %%ax, %%ss\n" : : : "memory");
}

struct idt_entry { uint16_t bl; uint16_t s; uint8_t a; uint8_t f; uint16_t bh; } __attribute__((packed));
struct idt_ptr { uint16_t limit; uint32_t base; } __attribute__((packed));
struct idt_entry idt[256];
struct idt_ptr idtp;

// --- CMOV Emulation (ISR 6) ---
typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t eip, cs, eflags;
} __attribute__((packed)) Registers;

extern void as_isr6();

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].bl = base & 0xFFFF; idt[num].bh = (base >> 16) & 0xFFFF; idt[num].s = sel; idt[num].a = 0; idt[num].f = flags;
}

int has_cmov() {
    uint32_t edx;
    asm volatile("cpuid" : "=d"(edx) : "a"(1) : "ebx", "ecx");
    return (edx >> 15) & 1;
}

uint32_t get_reg_val(Registers* regs, int r) {
    switch(r) {
        case 0: return regs->eax; case 1: return regs->ecx;
        case 2: return regs->edx; case 3: return regs->ebx;
        case 4: return (uint32_t)&regs->eip + 12; // Actual ESP at trap
        case 5: return regs->ebp; case 6: return regs->esi;
        case 7: return regs->edi;
    }
    return 0;
}

void set_reg_val(Registers* regs, int r, uint32_t val) {
    switch(r) {
        case 0: regs->eax = val; break; case 1: regs->ecx = val; break;
        case 2: regs->edx = val; break; case 3: regs->ebx = val; break;
        case 4: /* ESP is usually not a CMOV target */; break;
        case 5: regs->ebp = val; break; case 6: regs->esi = val; break;
        case 7: regs->edi = val; break;
    }
}

void isr6_handler(Registers* regs) {
    uint8_t* ip = (uint8_t*)regs->eip;
    
    // Check for CMOV (0x0F 0x4X)
    if (ip[0] == 0x0F && (ip[1] & 0xF0) == 0x40) {
        uint8_t cond = ip[1] & 0x0F;
        uint8_t modrm = ip[2];
        uint8_t mod = (modrm >> 6) & 3;
        uint8_t reg = (modrm >> 3) & 7;
        uint8_t rm = modrm & 7;
        
        int instr_len = 3; // 0F 4X ModRM
        uint32_t src_val = 0;

        if (mod == 3) {
            src_val = get_reg_val(regs, rm);
        } else {
            uint32_t addr = 0;
            int has_sib = (rm == 4);
            uint32_t base_val = 0;

            if (has_sib) {
                uint8_t sib = ip[3];
                uint8_t ss = (sib >> 6) & 3;
                uint8_t index = (sib >> 3) & 7;
                uint8_t base = sib & 7;
                
                if (base == 5 && mod == 0) {
                    base_val = *(uint32_t*)&ip[4];
                    instr_len += 4;
                } else {
                    base_val = get_reg_val(regs, base);
                }
                
                if (index != 4) {
                    base_val += get_reg_val(regs, index) << ss;
                }
                instr_len += 1; // SIB byte
            } else {
                if (rm == 5 && mod == 0) {
                    addr = *(uint32_t*)&ip[3];
                    instr_len += 4; // disp32
                } else {
                    base_val = get_reg_val(regs, rm);
                }
            }

            if (mod == 1) { // disp8
                addr = base_val + (int8_t)ip[has_sib ? 4 : 3];
                instr_len += 1;
            } else if (mod == 2) { // disp32
                addr = base_val + *(int32_t*)&ip[has_sib ? 4 : 3];
                instr_len += 4;
            } else if (mod == 0 && !addr) {
                addr = base_val;
            }
            src_val = *(uint32_t*)addr;
        }

        int condition_met = 0;
        uint32_t f = regs->eflags;
        int cf = (f >> 0) & 1;
        int pf = (f >> 2) & 1;
        int zf = (f >> 6) & 1;
        int sf = (f >> 7) & 1;
        int of = (f >> 11) & 1;

        switch (cond) {
            case 0x0: if (of) condition_met = 1; break;
            case 0x1: if (!of) condition_met = 1; break;
            case 0x2: if (cf) condition_met = 1; break;
            case 0x3: if (!cf) condition_met = 1; break;
            case 0x4: if (zf) condition_met = 1; break;
            case 0x5: if (!zf) condition_met = 1; break;
            case 0x6: if (cf || zf) condition_met = 1; break;
            case 0x7: if (!cf && !zf) condition_met = 1; break;
            case 0x8: if (sf) condition_met = 1; break;
            case 0x9: if (!sf) condition_met = 1; break;
            case 0xA: if (pf) condition_met = 1; break;
            case 0xB: if (!pf) condition_met = 1; break;
            case 0xC: if (sf != of) condition_met = 1; break;
            case 0xD: if (sf == of) condition_met = 1; break;
            case 0xE: if (zf || (sf != of)) condition_met = 1; break;
            case 0xF: if (!zf && (sf == of)) condition_met = 1; break;
        }

        if (condition_met) {
            set_reg_val(regs, reg, src_val);
        }
        regs->eip += instr_len;
        return;
    }

    // Default: Panic for real invalid opcodes
    draw_rect(0, 0, scr_width, scr_height, 0xAA0000);
    draw_string("CRITICAL: Invalid Opcode Exception", 40, 40, 0xFFFFFF);
    swap_buffers();
    while(1) asm("hlt");
}

void idt_install() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1; idtp.base = (uint32_t)&idt; 
    asm volatile("lidt %0" : : "m" (idtp));
    idt_set_gate(6, (uint32_t)as_isr6, 0x08, 0x8E);
}

void acpi_shutdown() {
    if (!acpi_supported) return;

    // Standard ACPI poweroff (works in QEMU/Bochs/VirtualBox)
    outw(0x604, 0x2000);

    // Fallback
    while (1) asm("hlt");
}

static inline void outw(uint16_t port, uint16_t val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}


void safe_power_message() {
    draw_rect(0, 0, scr_width, scr_height, 0x000000);
    draw_string("It is now safe to turn off your computer.", 40, 40, 0xFFFFFF);
    swap_buffers();
    while (1) asm("hlt");
}

// --- PS/2 Input Handling ---
void poll_ps2() {
    uint8_t status = inb(0x64);
    while (status & 0x01) { 
        uint8_t is_mouse = status & 0x20;
        uint8_t port_data = inb(0x60);
        
        if (is_mouse) {
            switch(mouse_cycle) {
                case 0:
                    if (port_data & 0x08) { mouse_byte[0] = port_data; mouse_cycle++; }
                    break;
                case 1:
                    mouse_byte[1] = port_data; mouse_cycle++;
                    break;
                case 2:
                    mouse_byte[2] = port_data; mouse_cycle = 0;
                    
                    int dx = (int8_t)mouse_byte[1];
                    int dy = (int8_t)mouse_byte[2];
                    mouse_x += dx;
                    mouse_y -= dy; 
                    
                    int new_btn = mouse_byte[0] & 0x01;
                    if (new_btn && !mouse_down) {
                        mouse_clicked = 1;
                    }
                    mouse_down = new_btn;
                    
                    if (mouse_x < 0) mouse_x = 0;
                    if (mouse_y < 0) mouse_y = 0;
                    if (mouse_x >= (int)scr_width) mouse_x = scr_width - 8;
                    if (mouse_y >= (int)scr_height) mouse_y = scr_height - 8;
                    
                    break;
            }
        } else {
            // Keyboard Input
            if (port_data & 0x80) { // Break code (release)
                uint8_t released = port_data & 0x7F;
                if (released == 0x2A || released == 0x36) shift_active = 0;
            } else { // Make code (press)
                if (port_data == 0x2A || port_data == 0x36) {
                    shift_active = 1;
                } else if (port_data < 128) {
                    char c = shift_active ? scancode_ascii_shift[port_data] : scancode_ascii[port_data];
                    if (c && win_terminal.open && !win_terminal.minimized) {
                        terminal_handle_key(c);
                    } else if (c && win_notepad.open && !win_notepad.minimized) {
                        if (c == '\b') {
                            if (notepad_len > 0) notepad_len--;
                        } else {
                            if (notepad_len < 1023) {
                                notepad_buf[notepad_len++] = c;
                            }
                        }
                        force_render_frame = 1; // Update screen instantly as we type
                    }
                }
            }
        }
        status = inb(0x64); 
    }
}
// Implemented in Build 107
void reboot_system() {

    // Try ACPI reset first
    if (acpi_supported) {
        outb(0xCF9, 0x06);
    }

    // Fallback: keyboard controller reset
    uint8_t good = 0x02;
    while (good & 0x02)
        good = inb(0x64);

    outb(0x64, 0xFE);

    while (1) asm("hlt");
}

extern void mouse_install();
void kernel_main(uint32_t magic, struct multiboot_info* mbd) {
    if (magic != 0x2BADB002) return;
    
    gdt_install();
    idt_install();
    mouse_install();
    ahci_init();
    acpi_supported = detect_acpi();
    
    uint32_t highest_mod_end = 0x400000; 
    int has_wallpaper = 0;
    if (mbd->flags & (1 << 3)) {
        if (mbd->mods_count > 0) {
            struct multiboot_mod_list* mods = (struct multiboot_mod_list*)mbd->mods_addr;
            for (uint32_t i = 0; i < mbd->mods_count; i++) {
                if (mods[i].mod_end > highest_mod_end) highest_mod_end = mods[i].mod_end;
            }
            wallpaper_ptr = (void*)mods[0].mod_start;
            has_wallpaper = 1;
        }
    }

    if (mbd->flags & (1 << 12)) {
        fb = (uint32_t*)(uint32_t)mbd->framebuffer_addr;
        scr_width = mbd->framebuffer_width;
        scr_height = mbd->framebuffer_height;
        pitch = mbd->framebuffer_pitch;
        bpp = mbd->framebuffer_bpp;
        
        uint32_t total_mem_bytes = 0;
        if (mbd->flags & 1) {
            total_mem_bytes = (mbd->mem_upper + 1024) * 1024;
        } else {
            total_mem_bytes = 16 * 1024 * 1024; // Fallback assumption
        }
        
        uint32_t bb_size = scr_width * scr_height * (bpp / 8);
        uint32_t bb_addr = 0;
        
        // Priority: Smoothness over Aesthetics on low-RAM systems.
        // If < 32MB, we ALWAYS sacrifice wallpaper to guarantee 100% stable double-buffering.
        if (total_mem_bytes < 32 * 1024 * 1024) {
            wallpaper_ptr = NULL;
            // Place backbuffer at the 2.5MB mark (safely after kernel/stack/data)
            // This allows up to a 9.5MB buffer on a 12MB machine.
            bb_addr = 0x280000; 
        } else {
            // High-RAM math: place at top of memory minus 2MB safety margin
            bb_addr = total_mem_bytes - bb_size - (2 * 1024 * 1024);
            // Collision Check for High-RAM (ensure we don't hit modules)
            if (bb_addr < highest_mod_end + 0x10000) {
                 bb_addr = highest_mod_end + 0x10000;
            }
        }

        // Final Allocation Check
        if (bb_addr + bb_size > total_mem_bytes || bb_addr == 0) {
            backbuffer = fb; // Safe Mode (Flickery)
        } else {
            backbuffer = (uint32_t*)bb_addr;
        }

        if (!backbuffer) backbuffer = fb;
    }
    
    total_ram_mb = (1024 + mbd->mem_upper) / 1024;
    
    if (total_ram_mb < 11) {
        // Critical System Halt
        draw_rect(0, 0, scr_width, scr_height, 0x000000);
        draw_string("Not enough Physical Memory is avaible for the OS loader or OS", 40, 40, 0xFFFFFF);
        swap_buffers();
        while(1) asm("hlt");
    }

    if (total_ram_mb < 64) {
        dialog_mode = 0;
        kstrcpy(win_dialog.title, "System Warning");
        win_dialog.open = 1;
    }

    if (has_cmov()) {
        // Log "CMOV: Hardware" (hidden or to debug)
    } else {
        // Log "CMOV: Emulated" (hidden or to debug)
    }

    get_cpu_info();
    explorer_init(0); // Initialize default drive for File Explorer
    int last_hover_idx = -1;
    int last_drawn_mouse_x = -1;
    int last_drawn_mouse_y = -1;

    while (1) {
        timer_ticks++;
        poll_ps2();
        
        // Trigger render if mouse hits a new hover target
        int current_hover_idx = get_dock_hover_index();
        if (current_hover_idx != last_hover_idx) {
            force_render_frame = 1;
            last_hover_idx = current_hover_idx;
        }

        if (mouse_clicked) {
            check_click();
            mouse_clicked = 0;
        }

        if (is_dragging) {
            if (!mouse_down) {
                is_dragging = 0;
                dragged_window = NULL;
            } else if (dragged_window) {
                int nx = mouse_x - drag_offset_x;
                int ny = mouse_y - drag_offset_y;
                if (nx != dragged_window->x || ny != dragged_window->y) {
                    dragged_window->x = nx;
                    dragged_window->y = ny;
                    force_render_frame = 1;
                }
            }
        }

        static int last_sec = -1;

        if (timer_ticks % 60 == 0) {
            int h, m, s;
            get_rtc_time(&h, &m, &s);
            if (s != last_sec) {
                force_render_frame = 1;
                last_sec = s;
               }
        }
        
        if (force_render_frame) {
            // Full 8MB rendering pipeline triggered by UI changes
            draw_wallpaper(); 
            draw_calculator();
            draw_explorer();
            draw_notepad();
            draw_settings();
            draw_terminal();
            draw_dialog();
            draw_dock(); 
            draw_topbar();
            
            swap_buffers();
            
            // Draw initial cursor directly to VRAM
            draw_cursor_direct(mouse_x, mouse_y);
            
            force_render_frame = 0;
            last_drawn_mouse_x = mouse_x;
            last_drawn_mouse_y = mouse_y;
            
        } else if (mouse_x != last_drawn_mouse_x || mouse_y != last_drawn_mouse_y) {
            // Mouse moved, but no UI changed! Dirty-Rectangle restore:
            // 1. Copy the 10x10 background square from the pristine backbuffer to VRAM where the cursor *used* to be
            swap_rect(last_drawn_mouse_x - 1, last_drawn_mouse_y - 1, 10, 10);
            
            // 2. Draw the new cursor straight to VRAM
            draw_cursor_direct(mouse_x, mouse_y);
            
            // 3. SAFE MODE: If we have no backbuffer, we MUST force a full redraw
            // to prevent mouse trails, as the dirty-rect restore from VRAM is impossible.
            if (backbuffer == fb) force_render_frame = 1;
            
            last_drawn_mouse_x = mouse_x;
            last_drawn_mouse_y = mouse_y;
        }

        // Lock rendering execution loop to simulated 60Hz VGA vblank
        // This stops the main thread from requesting 1 Million MMIO port polls per second
        while (inb(0x3DA) & 0x08); 
        while (!(inb(0x3DA) & 0x08));
    }
}
