#include "apps.h"

Window win_notepad = {400, 100, 400, 300, 0, 0, 0, 400, 100, 400, 300, "Notepad.txt"};

char notepad_buf[1024] = {0};
int notepad_len = 0;

void notepad_set_content(char* content, int len) {
    if (len > 1024) len = 1024;
    for (int i = 0; i < len; i++) {
        notepad_buf[i] = content[i];
    }
    notepad_len = len;
}

void draw_notepad() {
    if (!win_notepad.open || win_notepad.minimized) return;
    draw_window_frame(&win_notepad);

    int tx = win_notepad.x + 10;
    int ty = win_notepad.y + 30;
    for (int i = 0; i < notepad_len; i++) {
        if (notepad_buf[i] == '\n') {
            tx = win_notepad.x + 10;
            ty += 12;
        } else {
            draw_char(notepad_buf[i], tx, ty, get_text_color());
            tx += 9;
            if (tx > win_notepad.x + win_notepad.w - 20) { // wrap border
                tx = win_notepad.x + 10;
                ty += 12;
            }
        }
    }
    draw_rect(tx, ty, 8, 2, get_text_color()); 
}
