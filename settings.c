#include "apps.h"

Window win_settings = {300, 150, 400, 370, 0, 0, 0, 300, 150, 400, 370, "Settings"};

int current_theme = 0; // 0 = Dark, 1 = Light
int settings_page = 0; // 0 = Personalization, 1 = About
int rounded_dock = 1; // 1 = Rounded, 0 = Square
int rounded_win = 1;  // 1 = Rounded, 0 = Square
uint32_t total_ram_mb = 0;
char cpu_brand[49] = "Detecting...";

void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    asm volatile("cpuid"
                 : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                 : "a"(leaf));
}

void get_cpu_info() {
    uint32_t eax, ebx, ecx, edx;
    uint32_t *brand_ptr = (uint32_t*)cpu_brand;
    
    // Check if extended CPUID brand string is supported
    cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
    if (eax < 0x80000004) {
        const char* gen = "Generic x86 CPU";
        int i = 0;
        for(; gen[i] && i < 47; i++) cpu_brand[i] = gen[i];
        cpu_brand[i] = '\0';
        return;
    }

    cpuid(0x80000002, &brand_ptr[0], &brand_ptr[1], &brand_ptr[2], &brand_ptr[3]);
    cpuid(0x80000003, &brand_ptr[4], &brand_ptr[5], &brand_ptr[6], &brand_ptr[7]);
    cpuid(0x80000004, &brand_ptr[8], &brand_ptr[9], &brand_ptr[10], &brand_ptr[11]);
    cpu_brand[48] = '\0';
}

void draw_settings() {
    if (!win_settings.open || win_settings.minimized) return;
    draw_window_frame(&win_settings);
    int set_x = win_settings.x;
    int set_y = win_settings.y;
    int set_w = win_settings.w;
    int set_h = win_settings.h;
    
    // Sidebar Background
    draw_rect(set_x, set_y + 20, 120, set_h - 20, current_theme ? 0xEEEEEE : 0x252525);
    // Divider
    draw_rect(set_x + 120, set_y + 20, 2, set_h - 20, 0x444444);
    
    // Sidebar Items
    uint32_t sel_col = 0xFF9F0A;
    uint32_t txt_col = get_text_color();
    
    // Item 1: Personalization
    if (settings_page == 0) draw_rect(set_x + 5, set_y + 30, 110, 30, sel_col);
    draw_string("Personal", set_x + 15, set_y + 40, (settings_page == 0) ? 0xFFFFFF : txt_col);
    
    // Item 2: About
    if (settings_page == 1) draw_rect(set_x + 5, set_y + 65, 110, 30, sel_col);
    draw_string("About", set_x + 15, set_y + 75, (settings_page == 1) ? 0xFFFFFF : txt_col);

    // Content Area (Right side)
    int content_x = set_x + 140;
    
    if (settings_page == 0) {
        draw_string("Desktop Theme", content_x, set_y + 40, txt_col);
        
        uint32_t dbtnc = (current_theme == 0) ? 0xFF9F0A : 0x505050;
        draw_rect(content_x, set_y + 65, 100, 30, dbtnc);
        draw_string("Dark", content_x + 30, set_y + 75, 0xFFFFFF);
        
        uint32_t lbtnc = (current_theme == 1) ? 0xFF9F0A : 0x505050;
        draw_rect(content_x + 110, set_y + 65, 100, 30, lbtnc);
        draw_string("Light", content_x + 140, set_y + 75, 0xFFFFFF);

        draw_string("Dock Style", content_x, set_y + 105, txt_col);
        uint32_t rbtnc = (rounded_dock == 1) ? 0xFF9F0A : 0x505050;
        draw_rect(content_x, set_y + 125, 100, 30, rbtnc);
        draw_string("Rounded", content_x + 20, set_y + 135, 0xFFFFFF);

        uint32_t sbtnc = (rounded_dock == 0) ? 0xFF9F0A : 0x505050;
        draw_rect(content_x + 110, set_y + 125, 100, 30, sbtnc);
        draw_string("Square", content_x + 136, set_y + 135, 0xFFFFFF);

        draw_string("Window Style", content_x, set_y + 165, txt_col);
        uint32_t wrbtnc = (rounded_win == 1) ? 0xFF9F0A : 0x505050;
        draw_rect(content_x, set_y + 185, 100, 30, wrbtnc);
        draw_string("Rounded", content_x + 20, set_y + 195, 0xFFFFFF);

        uint32_t wsbtnc = (rounded_win == 0) ? 0xFF9F0A : 0x505050;
        draw_rect(content_x + 110, set_y + 185, 100, 30, wsbtnc);
        draw_string("Square", content_x + 136, set_y + 195, 0xFFFFFF);
        
    } else if (settings_page == 1) {
        draw_string("About This System", content_x, set_y + 40, 0xFF9F0A);
        
        draw_string("Model: BananaDesktop v1.0", content_x, set_y + 70, txt_col);
        draw_string("OS: BananaOS (32-bit)", content_x, set_y + 90, txt_col);
        
        char ram_msg[32] = "Memory: ";
        char ram_val[16];
        itoa(total_ram_mb, ram_val);
        int ri = 8; for(int j=0; ram_val[j]; j++) ram_msg[ri++] = ram_val[j];
        ram_msg[ri++] = ' '; ram_msg[ri++] = 'M'; ram_msg[ri++] = 'B'; ram_msg[ri++] = '\0';
        draw_string(ram_msg, content_x, set_y + 110, txt_col);

        draw_string("Processor:", content_x, set_y + 130, txt_col);
        draw_string(cpu_brand, content_x, set_y + 145, 0xAAAAAA);
        
        char res_msg[32] = "Resolution: ";
        char res_w[16], res_h[16];
        itoa(scr_width, res_w); itoa(scr_height, res_h);
        int rsi = 12; 
        for(int j=0; res_w[j]; j++) res_msg[rsi++] = res_w[j];
        res_msg[rsi++] = 'x';
        for(int j=0; res_h[j]; j++) res_msg[rsi++] = res_h[j];
        res_msg[rsi] = '\0';
        draw_string(res_msg, content_x, set_y + 175, txt_col);
    }
}
