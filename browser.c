#include "apps.h"
#include "net.h"
#include <stddef.h>

// --- Browser Window ---
Window win_browser = {80, 60, 700, 450, 0, 0, 0, 80, 60, 700, 450, "Browser"};

// --- Browser State ---
static char url_buf[128];
static int url_len = 0;
static int url_focused = 1;

static uint8_t page_buf[HTTP_BUF_SIZE];
static int page_len = 0;
static int page_scroll = 0;
static int browser_loading = 0;

static char status_msg[64] = "Type URL and press Enter";

// --- String Helpers ---
// static int b_strlen(const char* s) { int i = 0; while (s[i]) i++; return i; } (unused)
static void b_strcpy(char* dst, const char* src) { while (*src) *dst++ = *src++; *dst = 0; }

static int b_strncmp(const char* a, const char* b, int n) {
    while (n-- && *a && *b && *a == *b) { a++; b++; }
    return n < 0 ? 0 : *a - *b;
}

// --- Parse URL into host and path ---
static int parse_url(const char* url, char* host, int host_max, char* path, int path_max) {
    const char* p = url;

    // Skip "http://" if present
    if (b_strncmp(p, "http://", 7) == 0) {
        p += 7;
    }

    // Extract host
    int hi = 0;
    while (*p && *p != '/' && *p != ':' && hi < host_max - 1) {
        host[hi++] = *p++;
    }
    host[hi] = 0;

    // Skip port if present
    if (*p == ':') {
        while (*p && *p != '/') p++;
    }

    // Extract path
    if (*p == '/') {
        int pi = 0;
        while (*p && pi < path_max - 1) {
            path[pi++] = *p++;
        }
        path[pi] = 0;
    } else {
        path[0] = '/';
        path[1] = 0;
    }

    return hi > 0;
}

// --- Fetch URL ---
static void browser_fetch(void) {
    if (url_len == 0) return;

    char host[64];
    char path[128];
    url_buf[url_len] = 0;

    if (!parse_url(url_buf, host, sizeof(host), path, sizeof(path))) {
        b_strcpy(status_msg, "Invalid URL");
        return;
    }

    b_strcpy(status_msg, "Connecting...");
    force_render_frame = 1;
    // Force a render so user sees "Connecting..."
    // (caller should trigger redraw)

    page_len = 0;
    page_scroll = 0;
    browser_loading = 1;

    int result = net_http_get(host, path, page_buf, HTTP_BUF_SIZE);

    browser_loading = 0;

    if (result < 0) {
        b_strcpy(status_msg, "Connection failed");
        page_len = 0;
    } else {
        page_len = result;
        // Build status message with byte count
        char num[12];
        int v = result;
        int ni = 0;
        if (v == 0) { num[ni++] = '0'; }
        else {
            char tmp[12]; int ti = 0;
            while (v > 0) { tmp[ti++] = '0' + (v % 10); v /= 10; }
            while (ti > 0) num[ni++] = tmp[--ti];
        }
        num[ni] = 0;

        int si = 0;
        const char* done = "Received ";
        while (*done) status_msg[si++] = *done++;
        for (int i = 0; i < ni; i++) status_msg[si++] = num[i];
        const char* bytes = " bytes";
        while (*bytes) status_msg[si++] = *bytes++;
        status_msg[si] = 0;
    }

    force_render_frame = 1;
}

// --- Handle keyboard input for browser ---
void browser_handle_key(char c) {
    if (!win_browser.open || win_browser.minimized) return;

    if (c == '\r' || c == '\n') {
        if (url_focused && url_len > 0) {
            browser_fetch();
        }
    } else if (c == '\b') {
        if (url_focused && url_len > 0) {
            url_len--;
            url_buf[url_len] = 0;
            force_render_frame = 1;
        }
    } else if (c == '\t') {
        // Scroll down
        page_scroll += 5;
        force_render_frame = 1;
    } else {
        if (url_focused && url_len < 126) {
            url_buf[url_len++] = c;
            url_buf[url_len] = 0;
            force_render_frame = 1;
        }
    }
}

// --- Scroll handling (called from click detection) ---
void browser_scroll(int direction) {
    page_scroll += direction * 3;
    if (page_scroll < 0) page_scroll = 0;
    force_render_frame = 1;
}

// --- Draw the browser window ---
void draw_browser() {
    if (!win_browser.open || win_browser.minimized) return;
    draw_window_frame(&win_browser);

    int bx = win_browser.x;
    int by = win_browser.y;
    int bw = win_browser.w;
    int bh = win_browser.h;

    // Content background
    draw_rect(bx, by + 20, bw, bh - 20, 0x1A1A2E);

    // --- URL Bar ---
    int url_bar_y = by + 24;
    int url_bar_h = 22;
    draw_rect(bx + 6, url_bar_y, bw - 12, url_bar_h, 0x16213E);
    draw_rect(bx + 6, url_bar_y, bw - 12, 1, 0x3A506B);
    draw_rect(bx + 6, url_bar_y + url_bar_h - 1, bw - 12, 1, 0x3A506B);

    // URL prefix
    draw_string("Go:", bx + 10, url_bar_y + 6, 0x8899AA);
    // URL text
    int url_x = bx + 36;
    int max_url_chars = (bw - 52) / 8;
    int url_start = 0;
    if (url_len > max_url_chars) url_start = url_len - max_url_chars;
    for (int i = url_start; i < url_len; i++) {
        draw_char(url_buf[i], url_x + (i - url_start) * 8, url_bar_y + 6, 0xFFFFFF);
    }
    // Cursor
    if (url_focused) {
        int cursor_x = url_x + (url_len - url_start) * 8;
        draw_rect(cursor_x, url_bar_y + 4, 7, 14, 0x58A6FF);
    }

    // --- Status Bar ---
    int status_y = by + bh - 18;
    draw_rect(bx, status_y, bw, 18, 0x0D1117);
    draw_string(status_msg, bx + 8, status_y + 5, 0x8899AA);

    // Loading indicator
    if (browser_loading) {
        draw_string("Loading...", bx + bw - 90, status_y + 5, 0xFFAA00);
    }

    // --- Page Content Area ---
    int content_y = url_bar_y + url_bar_h + 4;
    int content_h = status_y - content_y - 2;
    int content_x = bx + 8;
    int content_w = bw - 16;

    // Clip region background
    draw_rect(bx + 4, content_y, bw - 8, content_h, 0x0D1117);

    if (page_len == 0) {
        // Empty state
        draw_string("No page loaded", content_x + 20, content_y + content_h / 2 - 4, 0x555555);
        return;
    }

    // Render raw text line by line
    int line_h = 10;
    int max_chars_per_line = content_w / 8;
    if (max_chars_per_line > 120) max_chars_per_line = 120;
    int max_visible_lines = content_h / line_h;

    // Count total lines and render visible ones
    int line = 0;
    int col = 0;
    // int draw_y = content_y + 2; (unused)

    for (int i = 0; i < page_len && page_buf[i]; i++) {
        char ch = (char)page_buf[i];

        if (ch == '\n' || col >= max_chars_per_line) {
            line++;
            col = 0;
            if (ch == '\n') continue;
        }

        if (ch == '\r') continue;

        // Only draw visible lines (accounting for scroll)
        int visible_line = line - page_scroll;
        if (visible_line >= 0 && visible_line < max_visible_lines) {
            int dx = content_x + col * 8;
            int dy = content_y + 2 + visible_line * line_h;

            // Color coding: HTML tags in blue, attributes in yellow
            uint32_t color = 0xC8D0D8; // Default: light gray
            if (ch == '<' || ch == '>') color = 0x58A6FF; // Blue for angle brackets
            else if (ch == '=' || ch == '"') color = 0xFFD866; // Yellow for attributes
            else if (ch == '&') color = 0xFF7B72; // Red for entities

            if (dx + 8 < bx + bw - 4) {
                draw_char(ch, dx, dy, color);
            }
        }

        col++;
    }

    // Scroll indicator
    int total_lines = line + 1;
    if (total_lines > max_visible_lines) {
        // Draw scrollbar track
        int sb_x = bx + bw - 10;
        draw_rect(sb_x, content_y, 6, content_h, 0x1A1A2E);

        // Draw scrollbar thumb
        int thumb_h = (max_visible_lines * content_h) / total_lines;
        if (thumb_h < 10) thumb_h = 10;
        int thumb_y = content_y + (page_scroll * (content_h - thumb_h)) / (total_lines - max_visible_lines);
        if (thumb_y + thumb_h > content_y + content_h) thumb_y = content_y + content_h - thumb_h;
        draw_rect(sb_x, thumb_y, 6, thumb_h, 0x3A506B);
    }
}
