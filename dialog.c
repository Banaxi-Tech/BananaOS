#include "apps.h"

Window win_dialog = {0, 0, 400, 200, 0, 0, 0, 0, 0, 400, 200, "System Warning"};

void draw_dialog() {
    if (!win_dialog.open) return;

    // Center window
    win_dialog.x = (scr_width - win_dialog.w) / 2;
    win_dialog.y = (scr_height - win_dialog.h) / 2;

    draw_window_frame(&win_dialog);

    int wx = win_dialog.x;
    int wy = win_dialog.y;

    // Warning Text
    draw_string("You are Running This OS on a System Under", wx + 20, wy + 40, get_text_color());
    draw_string("the Reccomended Minimum Ram Requirement.", wx + 20, wy + 60, get_text_color());
    draw_string("This means that glitches may occur.", wx + 20, wy + 90, get_text_color());
    draw_string("You were automaticly put into Safe Mode.", wx + 20, wy + 110, get_text_color());

    // OK Button
    int btn_w = 80;
    int btn_h = 30;
    int btn_x = wx + (win_dialog.w - btn_w) / 2;
    int btn_y = wy + 150;

    // Simple button style
    draw_rect(btn_x, btn_y, btn_w, btn_h, 0x505050);
    draw_string("OK", btn_x + 30, btn_y + 8, 0xFFFFFF);
}
