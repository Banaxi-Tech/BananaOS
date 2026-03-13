#include "apps.h"

#define PAINT_CANVAS_W 320
#define PAINT_CANVAS_H 200
#define PAINT_TOOLBAR_H 28
#define PAINT_TITLEBAR_H 20
#define PAINT_TOP_PADDING 6
#define PAINT_AFTER_TOOLBAR_GAP 6
#define PAINT_LEFT_PADDING 10
#define PAINT_BOTTOM_PADDING 10
#define PAINT_TOOLBAR_TOP_OFFSET (PAINT_TITLEBAR_H + PAINT_TOP_PADDING)
#define PAINT_CANVAS_TOP_OFFSET (PAINT_TOOLBAR_TOP_OFFSET + PAINT_TOOLBAR_H + PAINT_AFTER_TOOLBAR_GAP)

Window win_paint = {180, 80, PAINT_CANVAS_W + (PAINT_LEFT_PADDING * 2), PAINT_CANVAS_H + PAINT_CANVAS_TOP_OFFSET + PAINT_BOTTOM_PADDING, 0, 0, 0, 180, 80, PAINT_CANVAS_W + (PAINT_LEFT_PADDING * 2), PAINT_CANVAS_H + PAINT_CANVAS_TOP_OFFSET + PAINT_BOTTOM_PADDING, "Paint"};

static uint32_t paint_canvas[PAINT_CANVAS_W * PAINT_CANVAS_H];
static uint32_t paint_color = 0x000000;
static int paint_brush_size = 2;
static int paint_cleared = 0;

static void paint_clear_canvas(void) {
    int n = PAINT_CANVAS_W * PAINT_CANVAS_H;
    for (int i = 0; i < n; i++)
        paint_canvas[i] = 0xFFFFFF;
    paint_cleared = 1;
}

#define PAINT_COLOR_LEFT   (win_paint.x + PAINT_LEFT_PADDING - 2)
#define PAINT_COLOR_TOP    (win_paint.y + PAINT_TOOLBAR_TOP_OFFSET + (PAINT_TOOLBAR_H - 24) / 2)
#define PAINT_SWATCH_W     24
#define PAINT_SWATCH_GAP   4
#define PAINT_CLEAR_LEFT   (PAINT_COLOR_LEFT + 8 * (PAINT_SWATCH_W + PAINT_SWATCH_GAP) + 12)
#define PAINT_CLEAR_TOP    PAINT_COLOR_TOP
#define PAINT_CLEAR_W      40
#define PAINT_CLEAR_H      24

int paint_handle_click(int mx, int my) {
    if (!win_paint.open || win_paint.minimized) return 0;
    int toolbar_y = win_paint.y + PAINT_TOOLBAR_TOP_OFFSET;
    if (my < toolbar_y || my > toolbar_y + PAINT_TOOLBAR_H) return 0;

    uint32_t colors[] = {0x000000, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF, 0xFFFFFF};
    int start_x = PAINT_COLOR_LEFT;
    int sy = toolbar_y + (PAINT_TOOLBAR_H - PAINT_SWATCH_W) / 2;
    for (int i = 0; i < 8; i++) {
        int sx = start_x + i * (PAINT_SWATCH_W + PAINT_SWATCH_GAP);
        if (mx >= sx && mx < sx + PAINT_SWATCH_W && my >= sy && my < sy + PAINT_SWATCH_W) {
            paint_color = colors[i];
            force_render_frame = 1;
            return 1;
        }
    }
    if (mx >= PAINT_CLEAR_LEFT && mx < PAINT_CLEAR_LEFT + PAINT_CLEAR_W &&
        my >= PAINT_CLEAR_TOP && my < PAINT_CLEAR_TOP + PAINT_CLEAR_H) {
        paint_clear_canvas();
        force_render_frame = 1;
        return 1;
    }
    return 0;
}

void paint_handle_mouse(int mx, int my, int is_down) {
    if (!win_paint.open || win_paint.minimized) return;

    int canvas_left = win_paint.x + PAINT_LEFT_PADDING;
    int canvas_top = win_paint.y + PAINT_CANVAS_TOP_OFFSET;
    int cx = mx - canvas_left;
    int cy = my - canvas_top;

    if (cx < 0 || cx >= PAINT_CANVAS_W || cy < 0 || cy >= PAINT_CANVAS_H)
        return;

    if (!is_down) return;

    int r = paint_brush_size;
    if (r < 1) r = 1;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int px = cx + dx;
            int py = cy + dy;
            if (px < 0 || px >= PAINT_CANVAS_W || py < 0 || py >= PAINT_CANVAS_H)
                continue;
            if (dx * dx + dy * dy <= r * r)
                paint_canvas[py * PAINT_CANVAS_W + px] = paint_color;
        }
    }
    force_render_frame = 1;
}

void draw_paint(void) {
    if (!win_paint.open || win_paint.minimized) return;
    draw_window_frame(&win_paint);

    int px = win_paint.x;
    int py = win_paint.y;
    int pw = win_paint.w;
    int ph = win_paint.h;

    if (!paint_cleared) paint_clear_canvas();

    int client_x = px + 2;
    int client_y = py + PAINT_TITLEBAR_H + 1;
    int client_w = pw - 4;
    int client_h = ph - PAINT_TITLEBAR_H - 3;
    draw_rect(client_x, client_y, client_w, client_h, current_theme ? 0xF3F3F3 : 0x202020);

    int toolbar_y = py + PAINT_TOOLBAR_TOP_OFFSET;
    int toolbar_h = PAINT_TOOLBAR_H;
    draw_rect(px + 2, toolbar_y, pw - 4, toolbar_h, current_theme ? 0xE8E8E8 : 0x2A2A2A);

    uint32_t colors[] = {0x000000, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF, 0xFFFFFF};
    int num_colors = 8;
    int sw = 24;
    int gap = 4;
    int start_x = PAINT_COLOR_LEFT;
    int sy = toolbar_y + (toolbar_h - sw) / 2;

    for (int i = 0; i < num_colors; i++) {
        int sx = start_x + i * (sw + gap);
        draw_rect(sx, sy, sw, sw, colors[i]);
        if (paint_color == colors[i])
            draw_rect(sx + 2, sy + 2, sw - 4, sw - 4, 0x000000);
    }

    int clear_x = start_x + num_colors * (sw + gap) + 12;
    draw_rect(clear_x, sy, 40, sw, 0x666666);
    draw_string("Clear", clear_x + 4, sy + 6, 0xFFFFFF);

    int canvas_x = px + PAINT_LEFT_PADDING;
    int canvas_y = py + PAINT_CANVAS_TOP_OFFSET;
    draw_rect(canvas_x - 1, canvas_y - 1, PAINT_CANVAS_W + 2, PAINT_CANVAS_H + 2, 0x444444);
    for (int y = 0; y < PAINT_CANVAS_H; y++) {
        for (int x = 0; x < PAINT_CANVAS_W; x++) {
            draw_pixel(canvas_x + x, canvas_y + y, paint_canvas[y * PAINT_CANVAS_W + x]);
        }
    }
}
