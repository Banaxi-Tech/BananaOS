#include "apps.h"

Window win_calc = {100, 100, 200, 310, 0, 0, 0, 100, 100, 200, 310, "Calculator"};

long calc_acc = 0;
long calc_current = 0;
char calc_op = 0;
char calc_display[16] = "0";

void calculate() {
    if (calc_op == '+') calc_acc += calc_current;
    else if (calc_op == '-') calc_acc -= calc_current;
    else if (calc_op == '*') calc_acc *= calc_current;
    else if (calc_op == '/') { if (calc_current != 0) calc_acc /= calc_current; }
    else calc_acc = calc_current;
    itoa(calc_acc, calc_display);
    calc_current = 0;
}

void draw_calculator() {
    if (!win_calc.open || win_calc.minimized) return;
    draw_window_frame(&win_calc);
    
    int calc_x = win_calc.x;
    int calc_y = win_calc.y;
    int calc_w = win_calc.w;
    
    draw_rect(calc_x + 10, calc_y + 30, calc_w - 20, 30, 0x333333);
    draw_string(calc_display, calc_x + 20, calc_y + 40, 0xFFFFFF);
    
    int btn_size = 40; int gap = 6;
    int start_x = calc_x + 10; int start_y = calc_y + 70;
    const char* labels[5][4] = {
        {"AC", "+/-", "%", "/"},
        {"7", "8", "9", "*"},
        {"4", "5", "6", "-"},
        {"1", "2", "3", "+"},
        {"0", "", ".", "="}
    };
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 4; col++) {
            uint32_t btn_col = 0x505050;
            if (col == 3) btn_col = 0xFF9F0A; 
            if (row == 0 && col < 3) btn_col = 0xA5A5A5; 
            if (row == 4 && col == 1) continue; 
            int cur_w = (row == 4 && col == 0) ? (btn_size * 2 + gap) : btn_size;
            int bx = start_x + (col * (btn_size + gap));
            int by = start_y + (row * (btn_size + gap));
            draw_rect(bx, by, cur_w, btn_size, btn_col);
            draw_string(labels[row][col], bx + (cur_w/2) - 8, by + (btn_size/2) - 4, (btn_col == 0xA5A5A5) ? 0x00 : 0xFFFFFF);
        }
    }
}
