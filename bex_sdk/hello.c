#include "bex.h"

#define WIDTH 200
#define HEIGHT 150

uint32_t my_canvas[WIDTH * HEIGHT];

void fill_rect(int x, int y, int w, int h, uint32_t color) {
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            if (x+j >= 0 && x+j < WIDTH && y+i >= 0 && y+i < HEIGHT) {
                my_canvas[(y+i) * WIDTH + (x+j)] = color;
            }
        }
    }
}

int main() {
    sys_print("Starting GUI app...\n");
    sys_window_create(WIDTH, HEIGHT, "My BEX App", my_canvas);
    
    int btn_x = 50, btn_y = 50, btn_w = 100, btn_h = 40;
    uint32_t bg_color = 0x222222;
    uint32_t btn_color = 0xAA0000;

    int mx, my, clicked;
    int running = 1;

    while (running) {
        // Redraw Background
        fill_rect(0, 0, WIDTH, HEIGHT, bg_color);

        // Fetch events
        sys_get_event(&mx, &my, &clicked);

        // Check if mouse is over button
        int hover = (mx >= btn_x && mx <= btn_x + btn_w && my >= btn_y && my <= btn_y + btn_h);

        if (hover) {
            fill_rect(btn_x, btn_y, btn_w, btn_h, 0xFF0000); // Hover color
            if (clicked) {
                char msg[] = "You clicked the GUI Button!";
                sys_popup(msg);
                running = 0; // Exit after clicking
            }
        } else {
            fill_rect(btn_x, btn_y, btn_w, btn_h, btn_color);
        }

        // Pass control back to OS to render screen
        sys_window_update();
    }

    sys_exit();
    return 0;
}