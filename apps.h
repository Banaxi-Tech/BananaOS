#ifndef APPS_H
#define APPS_H

#include <stdint.h>

// --- Window Struct ---
typedef struct {
    int x, y, w, h;
    int open, minimized, maximized;
    int old_x, old_y, old_w, old_h;
    char title[32];
} Window;

// --- Shared Graphics Functions (defined in kernel.c) ---
extern uint32_t scr_width;
extern uint32_t scr_height;
extern int force_render_frame;

void draw_pixel(int x, int y, uint32_t color);
uint32_t get_pixel(int x, int y);
void draw_rect(int x, int y, int w, int h, uint32_t color);
void draw_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha);
void draw_rounded_rect_alpha(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha);
void draw_char(char c, int x, int y, uint32_t fg_color);
void draw_string(const char* str, int x, int y, uint32_t fg);
void itoa(int val, char* buf);

uint32_t get_window_color();
uint32_t get_text_color();
void draw_window_frame(Window* win);

// --- App States & Functions ---

// Calculator
extern Window win_calc;
extern long calc_acc;
extern long calc_current;
extern char calc_op;
extern char calc_display[16];
void draw_calculator();
void calculate();

// Notepad
extern Window win_notepad;
extern char notepad_buf[1024];
extern int notepad_len;
void draw_notepad();
void notepad_set_content(char* content, int len);

// Explorer
extern Window win_explorer;
void draw_explorer();
void explorer_init(uint8_t drive);
void explorer_open_file(int index);

// Settings
extern Window win_settings;
extern int current_theme;
extern int settings_page;
extern int rounded_dock;
extern int rounded_win;
extern uint32_t total_ram_mb;
extern char cpu_brand[49];
void draw_settings();
void get_cpu_info();

// Dialog
extern Window win_dialog;
void draw_dialog();

// Terminal
extern Window win_terminal;
void draw_terminal();
void terminal_handle_key(char key);

#endif
