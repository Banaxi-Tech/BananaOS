#include <stdint.h>
#include <stddef.h>
#include "../font.h"
#include "../drivers/disk.h"
#include "../drivers/pci.h"
#include "../drivers/ahci.h"
#include "../drivers/cdfs.h"

/* ===== I/O Port Access ===== */
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void outw(uint16_t port, uint16_t val) {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* ===== Multiboot Structures ===== */
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

/* ===== Framebuffer ===== */
uint32_t* fb = NULL;
uint32_t* backbuffer = NULL;
uint32_t scr_width = 1024;
uint32_t scr_height = 768;
uint32_t pitch = 0;
uint8_t bpp = 32;

/* ===== Mouse State ===== */
int mouse_x = 512, mouse_y = 384;
uint8_t mouse_cycle = 0;
int8_t mouse_byte[3];
int mouse_clicked = 0;
int mouse_down = 0;
int force_render_frame = 1;

/* Animation (frame counter; increment each vsync) */
static uint32_t anim_frame = 0;
static int anim_panel_off_y = 0;   /* panel slide-in: 0 = rest, negative = sliding down */
static int anim_prev_state = -1;
/* Auto: 1 if RAM >= 64MB and CPU Pentium+. User can override with toggle. */
static int animations_enabled = 0;
/* Effective value used for rendering (user toggle overrides auto). */
static int animations_on = 0;

/* ===== Cursor ===== */
static uint32_t cursor_backup[64];
static int last_mx = -1, last_my = -1;

/* ===== Image Data ===== */
static uint32_t cdfs_image_lba = 0;
static uint32_t cdfs_image_size = 0;
static int use_cdfs = 0;

/* ===== Setup State Machine ===== */
#define STATE_WELCOME       0
#define STATE_PRESS_INSTALL 1
#define STATE_LANGUAGE      2
#define STATE_SELECT        3
#define STATE_CONFIRM       4
#define STATE_INSTALLING    5
#define STATE_DONE         6
#define STATE_ERROR        7

static int setup_state = STATE_WELCOME;

/* ===== Beta/Developer Preview Flag ===== */
static const int is_beta = 1;

/* ===== Language ===== */
#define LANG_EN 0
#define LANG_DE 1
static int current_lang = LANG_EN;

/* ===== Localized Strings ===== */
static const char* str_welcome_title[2]    = { "BananaOS", "BananaOS" };
static const char* str_welcome_beta[2]     = { "BananaOS Developer Preview", "BananaOS Entwicklervorschau" };
static const char* str_press_next[2]       = { "Press Next to Install", "Klicken Sie auf Weiter zum Installieren" };
static const char* str_select_lang[2]      = { "Select Language", "Sprache waehlen" };
static const char* str_select_drive[2]     = { "Select a drive to install BananaOS:", "Waehlen Sie ein Laufwerk fuer BananaOS:" };
static const char* str_no_drives[2]        = { "No drives detected.", "Keine Laufwerke gefunden." };
static const char* str_next[2]             = { "Next", "Weiter" };
static const char* str_back[2]             = { "Go Back", "Zurueck" };
static const char* str_install[2]          = { "Install", "Installieren" };
static const char* str_are_you_sure[2]     = { "Are you sure?", "Sind Sie sicher?" };
static const char* str_all_data_on[2]      = { "All data on:", "Alle Daten auf:" };
static const char* str_will_be_erased[2]   = { "will be erased.", "werden geloescht." };
static const char* str_cannot_undo[2]      = { "This action cannot be undone.", "Diese Aktion kann nicht rueckgaengig gemacht werden." };
static const char* str_installing[2]       = { "Installing BananaOS...", "BananaOS wird installiert..." };
static const char* str_complete[2]         = { "Installation complete!", "Installation abgeschlossen!" };
static const char* str_installed_to[2]     = { "BananaOS has been installed to:", "BananaOS wurde installiert auf:" };
static const char* str_restart_prompt[2]  = { "Click Restart when you are ready.", "Klicken Sie auf Neustart, wenn Sie bereit sind." };
static const char* str_restart[2]         = { "Restart", "Neustart" };
static const char* str_error[2]            = { "Error!", "Fehler!" };
static const char* str_no_image[2]         = { "No OS image found in setup media.", "Kein OS-Image im Setup-Medium gefunden." };
static const char* str_rebuild[2]          = { "Please rebuild with: make buildSetup", "Bitte neu erstellen mit: make buildSetup" };
static const char* str_setup_title[2]      = { "BananaOS Setup", "BananaOS Setup" };
static const char* str_press_to_install[2] = { "Press to install", "Zum Installieren druecken" };
static const char* str_license[2]          = { "This software is licensed under the GNU GPL v3.0.", "Diese Software steht unter der GNU GPL v3.0." };
static const char* str_animations[2]       = { "Animations: On", "Animationen: An" };
static const char* str_animations_off[2]   = { "Animations: Off", "Animationen: Aus" };

/* Panel dimensions and colors (modern dark theme) */
#define PANEL_W      620
#define PANEL_H      440
#define GLASS_H      44
#define PANEL_BG    0x1a1f2e
#define PANEL_BORDER 0x2d3548
#define TITLEBAR_BG  0x252b3b
#define TITLEBAR_ACCENT 0x5b7cff
#define CONTENT_BG   0x1e2433
#define LICENSE_COLOR 0x6b7280

/* ===== Drive List ===== */
typedef struct {
    int id;
    char name[32];
    uint32_t size_mb;
} DriveEntry;

static DriveEntry drives[34];
static int drive_count = 0;
static int selected_drive = -1;

/* ===== Install Progress ===== */
static uint32_t install_current = 0;
static uint32_t install_total = 0;

/* ===== GDT ===== */
struct gdt_entry { uint16_t l; uint16_t bl; uint8_t bm; uint8_t a; uint8_t g; uint8_t bh; } __attribute__((packed));
struct gdt_ptr { uint16_t limit; uint32_t base; } __attribute__((packed));
static struct gdt_entry gdt[3];
static struct gdt_ptr gp;

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].bl = base & 0xFFFF; gdt[num].bm = (base >> 16) & 0xFF; gdt[num].bh = (base >> 24) & 0xFF;
    gdt[num].l = limit & 0xFFFF; gdt[num].g = ((limit >> 16) & 0x0F) | (gran & 0xF0); gdt[num].a = access;
}

static void gdt_install(void) {
    gp.limit = (sizeof(struct gdt_entry) * 3) - 1;
    gp.base = (uint32_t)&gdt;
    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    asm volatile("lgdt %0" : : "m"(gp));
    asm volatile(
        "pushl $0x08\n pushl $1f\n lret\n 1:\n"
        "mov $0x10, %%ax\n mov %%ax, %%ds\n mov %%ax, %%es\n"
        "mov %%ax, %%fs\n mov %%ax, %%gs\n mov %%ax, %%ss\n"
        : : : "memory"
    );
}

/* ===== IDT ===== */
struct idt_entry { uint16_t bl; uint16_t s; uint8_t a; uint8_t f; uint16_t bh; } __attribute__((packed));
struct idt_ptr { uint16_t limit; uint32_t base; } __attribute__((packed));
static struct idt_entry idt[256];
static struct idt_ptr idtp;

extern void as_isr6(void);
extern void as_isr128(void);

static void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].bl = base & 0xFFFF; idt[num].bh = (base >> 16) & 0xFFFF;
    idt[num].s = sel; idt[num].a = 0; idt[num].f = flags;
}

static void idt_install(void) {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (uint32_t)&idt;
    asm volatile("lidt %0" : : "m"(idtp));
    idt_set_gate(6, (uint32_t)as_isr6, 0x08, 0x8E);
    idt_set_gate(128, (uint32_t)as_isr128, 0x08, 0x8E);
}

/* ===== ISR6 Handler - CMOV Emulation (called from boot.s) ===== */
typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t eip, cs, eflags;
} __attribute__((packed)) Registers;

static uint32_t get_reg_val(Registers* regs, int r) {
    switch(r) {
        case 0: return regs->eax; case 1: return regs->ecx;
        case 2: return regs->edx; case 3: return regs->ebx;
        case 4: return (uint32_t)&regs->eip + 12;
        case 5: return regs->ebp; case 6: return regs->esi;
        case 7: return regs->edi;
    }
    return 0;
}

static void set_reg_val(Registers* regs, int r, uint32_t val) {
    switch(r) {
        case 0: regs->eax = val; break; case 1: regs->ecx = val; break;
        case 2: regs->edx = val; break; case 3: regs->ebx = val; break;
        case 5: regs->ebp = val; break; case 6: regs->esi = val; break;
        case 7: regs->edi = val; break;
    }
}

void isr6_handler(Registers* regs) {
    uint8_t* ip = (uint8_t*)regs->eip;

    if (ip[0] == 0x0F && (ip[1] & 0xF0) == 0x40) {
        uint8_t cond = ip[1] & 0x0F;
        uint8_t modrm = ip[2];
        uint8_t mod = (modrm >> 6) & 3;
        uint8_t reg = (modrm >> 3) & 7;
        uint8_t rm = modrm & 7;
        int instr_len = 3;
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
                uint8_t idx = (sib >> 3) & 7;
                uint8_t base = sib & 7;
                if (base == 5 && mod == 0) { base_val = *(uint32_t*)&ip[4]; instr_len += 4; }
                else base_val = get_reg_val(regs, base);
                if (idx != 4) base_val += get_reg_val(regs, idx) << ss;
                instr_len += 1;
            } else {
                if (rm == 5 && mod == 0) { addr = *(uint32_t*)&ip[3]; instr_len += 4; }
                else base_val = get_reg_val(regs, rm);
            }
            if (mod == 1) { addr = base_val + (int8_t)ip[has_sib ? 4 : 3]; instr_len += 1; }
            else if (mod == 2) { addr = base_val + *(int32_t*)&ip[has_sib ? 4 : 3]; instr_len += 4; }
            else if (mod == 0 && !addr) addr = base_val;
            src_val = *(uint32_t*)addr;
        }

        int met = 0;
        uint32_t f = regs->eflags;
        int cf=(f>>0)&1, pf=(f>>2)&1, zf=(f>>6)&1, sf=(f>>7)&1, of=(f>>11)&1;
        switch (cond) {
            case 0x0: met=of; break;       case 0x1: met=!of; break;
            case 0x2: met=cf; break;       case 0x3: met=!cf; break;
            case 0x4: met=zf; break;       case 0x5: met=!zf; break;
            case 0x6: met=cf||zf; break;   case 0x7: met=!cf&&!zf; break;
            case 0x8: met=sf; break;       case 0x9: met=!sf; break;
            case 0xA: met=pf; break;       case 0xB: met=!pf; break;
            case 0xC: met=sf!=of; break;   case 0xD: met=sf==of; break;
            case 0xE: met=zf||(sf!=of); break;
            case 0xF: met=!zf&&(sf==of); break;
        }
        if (met) set_reg_val(regs, reg, src_val);
        regs->eip += instr_len;
        return;
    }

    /* Real invalid opcode - show error on framebuffer */
    if (fb) {
        uint16_t* vga = (uint16_t*)0xB8000;
        const char* msg = "Invalid Opcode";
        for (int i = 0; msg[i]; i++) vga[i] = 0x4F00 | msg[i];
    }
    while(1) asm("hlt");
}

void isr128_handler(Registers* regs) {
    (void)regs;
}

/* ===== Drawing Primitives ===== */
void draw_char(char c, int x, int y, uint32_t fg);
void draw_string(const char* str, int x, int y, uint32_t fg);

void draw_pixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= (int)scr_width || y < 0 || y >= (int)scr_height || !backbuffer) return;
    *(uint32_t*)((uint8_t*)backbuffer + (y * pitch) + (x * (bpp / 8))) = color;
}

uint32_t get_pixel(int x, int y) {
    if (x < 0 || x >= (int)scr_width || y < 0 || y >= (int)scr_height || !backbuffer) return 0;
    return *(uint32_t*)((uint8_t*)backbuffer + (y * pitch) + (x * 4));
}

void draw_rect(int x, int y, int w, int h, uint32_t color) {
    for (int i = 0; i < h; i++)
        for (int j = 0; j < w; j++)
            draw_pixel(x + j, y + i, color);
}

/* Interpolate between two RGB colors (t in 0..256) */
static uint32_t lerp_color(uint32_t c1, uint32_t c2, int t) {
    if (t <= 0) return c1;
    if (t >= 256) return c2;
    int r = ((c1 >> 16) & 0xFF) * (256 - t) + ((c2 >> 16) & 0xFF) * t;
    int g = ((c1 >> 8) & 0xFF) * (256 - t) + ((c2 >> 8) & 0xFF) * t;
    int b = (c1 & 0xFF) * (256 - t) + (c2 & 0xFF) * t;
    return ((r >> 8) << 16) | ((g >> 8) << 8) | (b >> 8);
}

/* Animated gradient background (dark blue/purple shift) */
static void draw_animated_bg(void) {
    uint32_t c1 = 0x0a0e14;
    uint32_t c2 = 0x12182a;
    uint32_t c3 = 0x0d1117;
    int phase = (int)(anim_frame * 2) % 1024;
    if (phase < 0) phase += 1024;
    for (int y = 0; y < (int)scr_height; y++) {
        int t = (y + phase) % 1024;
        if (t < 512)
            draw_rect(0, y, (int)scr_width, 1, lerp_color(c1, c2, t * 256 / 512));
        else
            draw_rect(0, y, (int)scr_width, 1, lerp_color(c2, c3, (t - 512) * 256 / 512));
    }
}

/* Draw string with soft shadow for depth */
static void draw_string_shadow(int x, int y, const char* str, uint32_t fg) {
    int len = 0;
    const char* s = str;
    while (*s++) len++;
    for (int dy = 0; dy <= 1; dy++)
        for (int dx = 0; dx <= 1; dx++)
            if (dx || dy) {
                int i = 0;
                int nx = x + dx;
                int ny = y + dy;
                while (str[i]) { draw_char(str[i], nx + i * 8, ny, 0x0a0e14); i++; }
            }
    draw_string(str, x, y, fg);
}

void draw_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha) {
    if (alpha == 255) { draw_rect(x, y, w, h, color); return; }
    if (alpha == 0) return;
    if (backbuffer == fb) {
        /* Single-buffer mode: Alpha blending is too slow on old CPUs.
           Draw solid color instead to avoid flicker/lag. */
        draw_rect(x, y, w, h, color);
        return;
    }
    uint32_t sr = (color >> 16) & 0xFF, sg = (color >> 8) & 0xFF, sb = color & 0xFF;
    for (int i = 0; i < h; i++)
        for (int j = 0; j < w; j++) {
            int px = x + j, py = y + i;
            uint32_t d = get_pixel(px, py);
            uint32_t dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
            draw_pixel(px, py, (((sr * alpha + dr * (255 - alpha)) >> 8) << 16) |
                               (((sg * alpha + dg * (255 - alpha)) >> 8) << 8) |
                               ((sb * alpha + db * (255 - alpha)) >> 8));
        }
}

void draw_rounded_rect_alpha(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha) {
    (void)r;
    draw_rect_alpha(x, y, w, h, color, alpha);
}

/* Simple 5-tap blur (used for frosted glass effect) */
static inline uint32_t blur_pixel(uint32_t a, uint32_t b, uint32_t c,
                                  uint32_t d, uint32_t e) {
    uint32_t r = ((a >> 16 & 255) + (b >> 16 & 255) + (c >> 16 & 255) + (d >> 16 & 255) + (e >> 16 & 255)) / 5;
    uint32_t g = ((a >> 8  & 255) + (b >> 8  & 255) + (c >> 8  & 255) + (d >> 8  & 255) + (e >> 8  & 255)) / 5;
    uint32_t b2= ((a       & 255) + (b       & 255) + (c       & 255) + (d       & 255) + (e       & 255)) / 5;
    return (r << 16) | (g << 8) | b2;
}

static void blur_rect(int x, int y, int w, int h) {
    if (!backbuffer || backbuffer == fb) return;
    uint8_t* base = (uint8_t*)backbuffer;
    for (int j = y + 1; j < y + h - 1; j++) {
        for (int i = x + 1; i < x + w - 1; i++) {
            uint32_t offset = j * pitch + i * 4;
            uint32_t* p = (uint32_t*)(base + offset);

            uint32_t up    = *(uint32_t*)(base + (j - 1) * pitch + i * 4);
            uint32_t down  = *(uint32_t*)(base + (j + 1) * pitch + i * 4);
            uint32_t left  = *(uint32_t*)(base + j * pitch + (i - 1) * 4);
            uint32_t right = *(uint32_t*)(base + j * pitch + (i + 1) * 4);
            uint32_t mid   = *p;

            *p = blur_pixel(up, down, left, right, mid);
        }
    }
}

void draw_char(char c, int x, int y, uint32_t fg) {
    if (c < 0) return;
    const uint8_t* glyph = font8x8[(int)c];
    for (int row = 0; row < 8; row++)
        for (int col = 0; col < 8; col++)
            if (glyph[row] & (1 << (7 - col)))
                draw_pixel(x + col, y + row, fg);
}

void draw_string(const char* str, int x, int y, uint32_t fg) {
    while (*str) { draw_char(*str, x, y, fg); x += 8; str++; }
}

void itoa(int val, char* buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    int i = 0, neg = val < 0;
    if (neg) val = -val;
    do { buf[i++] = (val % 10) + '0'; } while ((val /= 10) > 0);
    if (neg) buf[i++] = '-';
    for (int j = 0, k = i - 1; j < k; j++, k--) { char t = buf[j]; buf[j] = buf[k]; buf[k] = t; }
    buf[i] = '\0';
}

static void swap_buffers(void) {
    if (!fb || !backbuffer) return;
    if (backbuffer == fb) return; /* Single-buffer mode: already drawing to visible fb */
    uint32_t dwords = (scr_height * pitch) / 4;
    void* d = fb;
    void* s = backbuffer;
    asm volatile("rep movsl" : "+D"(d), "+S"(s), "+c"(dwords) : : "memory");
}

/* ===== Cursor (direct to VRAM for flicker-free movement) ===== */
static void draw_cursor(int x, int y) {
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++) {
            int px = x + j, py = y + i;
            if (px >= 0 && px < (int)scr_width && py >= 0 && py < (int)scr_height)
                cursor_backup[i * 8 + j] = *(uint32_t*)((uint8_t*)fb + (py * pitch) + (px * 4));
        }
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++) {
            int px = x + j, py = y + i;
            if (px >= 0 && px < (int)scr_width && py >= 0 && py < (int)scr_height)
                *(uint32_t*)((uint8_t*)fb + (py * pitch) + (px * 4)) = 0xFFFFFF;
        }
}

static void restore_cursor_fb(int x, int y) {
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++) {
            int px = x + j, py = y + i;
            if (px >= 0 && px < (int)scr_width && py >= 0 && py < (int)scr_height)
                *(uint32_t*)((uint8_t*)fb + (py * pitch) + (px * 4)) = cursor_backup[i * 8 + j];
        }
}

/* ===== Mouse Polling ===== */
extern void mouse_install(void);

static void poll_mouse(void) {
    uint8_t status = inb(0x64);
    while (status & 0x01) {
        uint8_t is_mouse = status & 0x20;
        uint8_t data = inb(0x60);
        if (is_mouse) {
            switch (mouse_cycle) {
                case 0:
                    if (!(data & 0x08)) { mouse_cycle = 0; break; }
                    mouse_byte[0] = data; mouse_cycle++; break;
                case 1:
                    mouse_byte[1] = data; mouse_cycle++; break;
                case 2:
                    mouse_byte[2] = data; mouse_cycle = 0;
                    if (mouse_byte[0] & 0xC0) break;
                    mouse_x += (int8_t)mouse_byte[1];
                    mouse_y -= (int8_t)mouse_byte[2];
                    int btn = mouse_byte[0] & 0x01;
                    if (btn && !mouse_down) mouse_clicked = 1;
                    mouse_down = btn;
                    if (mouse_x < 0) mouse_x = 0;
                    if (mouse_y < 0) mouse_y = 0;
                    if (mouse_x >= (int)scr_width) mouse_x = scr_width - 8;
                    if (mouse_y >= (int)scr_height) mouse_y = scr_height - 8;
                    break;
            }
        }
        status = inb(0x64);
    }
}

/* ===== Reboot ===== */
static void reboot(void) {
    outb(0xCF9, 0x06);
    uint8_t good = 0x02;
    while (good & 0x02) good = inb(0x64);
    outb(0x64, 0xFE);
    while (1) asm("hlt");
}

/* ===== Drive Detection with Capacity ===== */
static uint32_t ata_get_size_mb(uint8_t drive) {
    uint8_t status = inb(0x1F7);
    if (status == 0xFF || status == 0x00) return 0;

    outb(0x1F6, 0xA0 | (drive << 4));
    for (int i = 0; i < 1000; i++) inb(0x1F7);

    outb(0x1F2, 0);
    outb(0x1F3, 0);
    outb(0x1F4, 0);
    outb(0x1F5, 0);
    outb(0x1F7, 0xEC); // IDENTIFY

    int timeout = 100000;
    while (timeout--) {
        status = inb(0x1F7);
        if (!(status & 0x80)) break;
    }
    if (timeout <= 0) return 0;
    if (!(status & 0x08)) return 0;

    uint16_t ident[256];
    for (int i = 0; i < 256; i++) {
        ident[i] = inw(0x1F0);
    }

    uint32_t sectors = ident[60] | ((uint32_t)ident[61] << 16);
    if (sectors == 0) return 0;
    return sectors / 2048; // sectors * 512 / (1024*1024)
}

static void detect_drives(void) {
    drive_count = 0;
    for (int d = 0; d < 2; d++) {
        if (ata_drive_exists(d)) {
            drives[drive_count].id = d;
            char* n = drives[drive_count].name;
            const char* pfx = "/dev/disk";
            int p = 0;
            while (*pfx) n[p++] = *pfx++;
            n[p++] = '0' + d;
            n[p] = 0;
            drives[drive_count].size_mb = ata_get_size_mb((uint8_t)d);
            drive_count++;
        }
    }
    for (int d = 0; d < 32; d++) {
        if (ahci_drive_exists(d)) {
            drives[drive_count].id = d + 2;
            char* n = drives[drive_count].name;
            const char* pfx = "/dev/sata";
            int p = 0;
            while (*pfx) n[p++] = *pfx++;
            if (d >= 10) { n[p++] = '0' + (d / 10); n[p++] = '0' + (d % 10); }
            else n[p++] = '0' + d;
            n[p] = 0;
            drives[drive_count].size_mb = 0; // AHCI capacity detection would need IDENTIFY via AHCI
            drive_count++;
        }
    }
}

/* ===== UI Helpers ===== */
static int in_rect(int mx, int my, int rx, int ry, int rw, int rh) {
    return mx >= rx && mx < rx + rw && my >= ry && my < ry + rh;
}


static int str_len(const char* s) { int i = 0; while (s[i]) i++; return i; }

/* ===== Format size helper ===== */
static void format_size(uint32_t mb, char* out) {
    if (mb >= 1024) {
        int gb = mb / 1024;
        int frac = (mb % 1024) * 10 / 1024;
        char num[12];
        itoa(gb, num);
        int p = 0;
        for (int k = 0; num[k]; k++) out[p++] = num[k];
        out[p++] = '.';
        out[p++] = '0' + frac;
        out[p++] = ' '; out[p++] = 'G'; out[p++] = 'B';
        out[p] = 0;
    } else {
        char num[12];
        itoa((int)mb, num);
        int p = 0;
        for (int k = 0; num[k]; k++) out[p++] = num[k];
        out[p++] = ' '; out[p++] = 'M'; out[p++] = 'B';
        out[p] = 0;
    }
}

/* ===== Progress-only update (single-buffer mode: avoids full-screen flicker) ===== */
static int last_drawn_pct = -1;
static int last_drawn_filled = 0;

static void render_install_progress_only(void) {
    if (install_total == 0) return;

    uint32_t pct = install_current * 100 / install_total;
    if ((int)pct == last_drawn_pct) return;

    int pw = PANEL_W;
    int px = (scr_width - pw) / 2;
    int py = (scr_height - PANEL_H) / 2;
    int bar_x = px + 30;
    int bar_y = py + 108;
    int bar_w = pw - 60;
    int bar_h = 24;

    int filled = (int)(bar_w * pct / 100);
    if (filled > bar_w) filled = bar_w;

    if (filled > last_drawn_filled) {
        draw_rect(bar_x + last_drawn_filled, bar_y, filled - last_drawn_filled, bar_h, 0x00CC44);
        last_drawn_filled = filled;
    }

    draw_rect(px + 250, bar_y + bar_h + 15, 100, 10, CONTENT_BG);
    char pct_str[8];
    itoa((int)pct, pct_str);
    int sl = str_len(pct_str);
    pct_str[sl] = '%'; pct_str[sl + 1] = 0;
    draw_string(pct_str, px + (pw - str_len(pct_str) * 8) / 2, bar_y + bar_h + 15, 0xFFFFFF);

    last_drawn_pct = (int)pct;
}

/* ===== UI Rendering ===== */
static int background_drawn = 0;

static void draw_button_hover(int x, int y, int w, int h, const char* label, uint32_t bg, uint32_t fg, int hover) {
    uint32_t use_bg = hover ? TITLEBAR_ACCENT : bg;
    if (hover && bg != TITLEBAR_ACCENT) {
        int r = ((use_bg >> 16) & 0xFF) + 20;
        int g = ((use_bg >> 8) & 0xFF) + 20;
        int b = (use_bg & 0xFF) + 20;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        use_bg = (r << 16) | (g << 8) | b;
    }
    draw_rect(x, y, w, h, use_bg);
    draw_rect(x, y, w, 1, 0x4a5568);
    int len = 0;
    const char* s = label;
    while (*s++) len++;
    draw_string(label, x + (w - len * 8) / 2, y + (h - 8) / 2, fg);
}

static void render(void) {
    if (backbuffer != fb && animations_on) {
        draw_animated_bg();
    } else if (backbuffer == fb || !background_drawn) {
        draw_rect(0, 0, scr_width, scr_height, 0x0d1117);
        background_drawn = 1;
    }

    int pw = PANEL_W, ph = PANEL_H;
    int px = (scr_width - pw) / 2;
    int py = (scr_height - ph) / 2 + (animations_on ? anim_panel_off_y : 0);

    /* Panel shadow (deeper when sliding, only if animations on) */
    int shadow_off = 8;
    if (animations_on && anim_panel_off_y < 0) {
        shadow_off += -anim_panel_off_y / 4;
        if (shadow_off > 12) shadow_off = 12;
    }
    draw_rect_alpha(px + shadow_off, py + shadow_off, pw, ph, 0x000000, 100);

    /* Panel outer border */
    draw_rect(px - 1, py - 1, pw + 2, ph + 2, PANEL_BORDER);
    draw_rect(px, py, pw, 1, 0x3d4451);

    /* Content area */
    draw_rect(px, py + GLASS_H, pw, ph - GLASS_H, CONTENT_BG);

    /* Titlebar */
    blur_rect(px, py, pw, GLASS_H);
    draw_rect_alpha(px, py, pw, GLASS_H, TITLEBAR_BG, 200);
    draw_rect(px, py + GLASS_H - 2, pw, 2, TITLEBAR_ACCENT);

    /* Titlebar shine (animated sweep, only when animations on) */
    if (animations_on && backbuffer != fb) {
        int shine_x = px + ((int)(anim_frame * 5) % (pw + 100)) - 50;
        if (shine_x < px - 50) shine_x += pw + 100;
        if (shine_x < px + pw) {
            int shine_w = 60;
            if (shine_x + shine_w > px + pw) shine_w = (px + pw) - shine_x;
            if (shine_w > 0 && shine_x >= px)
                draw_rect_alpha(shine_x, py + 2, shine_w, GLASS_H - 4, 0xffffff, 35);
        }
    }

    /* Title */
    const char* title = str_setup_title[current_lang];
    draw_string_shadow(px + (pw - str_len(title) * 8) / 2, py + 16, title, 0xFFFFFF);

    int cy = py + 68;

    switch (setup_state) {
    case STATE_WELCOME: {
        const char* main_title = is_beta ? str_welcome_beta[current_lang] : str_welcome_title[current_lang];
        draw_string_shadow(px + (pw - str_len(main_title) * 8) / 2, cy + 55, main_title, 0xFFFFFF);
        const char* sub = str_press_next[current_lang];
        draw_string(sub, px + (pw - str_len(sub) * 8) / 2, cy + 95, 0xAAAAAA);
        int btn_y = py + ph - 54;
        draw_button_hover(px + pw - 130, btn_y, 100, 32, str_next[current_lang], TITLEBAR_ACCENT, 0xFFFFFF,
            in_rect(mouse_x, mouse_y, px + pw - 130, btn_y, 100, 32));
        break;
    }

    case STATE_PRESS_INSTALL: {
        const char* tit = str_setup_title[current_lang];
        draw_string_shadow(px + (pw - str_len(tit) * 8) / 2, cy + 70, tit, 0xFFFFFF);
        const char* sub = str_press_to_install[current_lang];
        draw_string(sub, px + (pw - str_len(sub) * 8) / 2, cy + 110, 0xAAAAAA);
        /* Animations toggle (click to turn on/off) */
        int toggle_y = cy + 148;
        const char* anim_label = animations_on ? str_animations[current_lang] : str_animations_off[current_lang];
        int tw = str_len(anim_label) * 8 + 24;
        int tx = px + (pw - tw) / 2;
        draw_rect(tx, toggle_y, tw, 22, animations_on ? 0x2d3548 : 0x1e2433);
        draw_rect(tx, toggle_y, tw, 1, 0x4a5568);
        draw_string(anim_label, tx + 12, toggle_y + 7, animations_on ? 0xAAAAAA : 0x888888);
        int btn_y = py + ph - 54;
        draw_button_hover(px + 30, btn_y, 100, 32, str_back[current_lang], 0x2d3548, 0xCCCCCC,
            in_rect(mouse_x, mouse_y, px + 30, btn_y, 100, 32));
        draw_button_hover(px + pw - 130, btn_y, 100, 32, str_next[current_lang], TITLEBAR_ACCENT, 0xFFFFFF,
            in_rect(mouse_x, mouse_y, px + pw - 130, btn_y, 100, 32));
        break;
    }

    case STATE_LANGUAGE: {
        const char* prompt = str_select_lang[current_lang];
        draw_string(prompt, px + (pw - str_len(prompt) * 8) / 2, cy, 0xCCCCCC);
        cy += 48;

        uint32_t en_bg = (current_lang == LANG_EN) ? TITLEBAR_ACCENT : PANEL_BORDER;
        uint32_t en_fg = (current_lang == LANG_EN) ? 0xFFFFFF : 0xAAAAAA;
        draw_rect(px + 80, cy, pw - 160, 44, en_bg);
        draw_string("English", px + 80 + (pw - 160 - 7*8)/2, cy + 18, en_fg);
        cy += 52;

        uint32_t de_bg = (current_lang == LANG_DE) ? TITLEBAR_ACCENT : PANEL_BORDER;
        uint32_t de_fg = (current_lang == LANG_DE) ? 0xFFFFFF : 0xAAAAAA;
        draw_rect(px + 80, cy, pw - 160, 44, de_bg);
        draw_string("Deutsch", px + 80 + (pw - 160 - 7*8)/2, cy + 18, de_fg);

        int btn_y = py + ph - 54;
        draw_button_hover(px + 30, btn_y, 100, 32, str_back[current_lang], 0x2d3548, 0xCCCCCC,
            in_rect(mouse_x, mouse_y, px + 30, btn_y, 100, 32));
        draw_button_hover(px + pw - 130, btn_y, 100, 32, str_next[current_lang], TITLEBAR_ACCENT, 0xFFFFFF,
            in_rect(mouse_x, mouse_y, px + pw - 130, btn_y, 100, 32));
        break;
    }

    case STATE_SELECT: {
        draw_string(str_select_drive[current_lang], px + 30, cy, 0xCCCCCC);
        cy += 28;

        if (drive_count == 0) {
            draw_string(str_no_drives[current_lang], px + 30, cy, 0xFF6666);
        } else {
            for (int i = 0; i < drive_count && i < 10; i++) {
                int ey = cy + i * 34;
                uint32_t bg = (i == selected_drive) ? TITLEBAR_ACCENT : PANEL_BORDER;
                uint32_t fg = (i == selected_drive) ? 0xFFFFFF : 0xAAAAAA;
                draw_rect(px + 30, ey, pw - 60, 28, bg);
                draw_rect(px + 38, ey + 10, 10, 10, 0x3d4451);
                if (i == selected_drive)
                    draw_rect(px + 41, ey + 13, 4, 4, 0xFFFFFF);
                draw_string(drives[i].name, px + 58, ey + 10, fg);
                if (drives[i].size_mb > 0) {
                    char size_str[20];
                    format_size(drives[i].size_mb, size_str);
                    draw_string(size_str, px + pw - 120, ey + 10, 0x888888);
                }
            }
        }

        int btn_y = py + ph - 54;
        draw_button_hover(px + 30, btn_y, 100, 32, str_back[current_lang], 0x2d3548, 0xCCCCCC,
            in_rect(mouse_x, mouse_y, px + 30, btn_y, 100, 32));
        uint32_t bbg = (selected_drive >= 0) ? TITLEBAR_ACCENT : 0x3d4451;
        uint32_t bfg = (selected_drive >= 0) ? 0xFFFFFF : 0x666666;
        draw_button_hover(px + pw - 130, btn_y, 100, 32, str_next[current_lang], bbg, bfg,
            in_rect(mouse_x, mouse_y, px + pw - 130, btn_y, 100, 32));
        break;
    }

    case STATE_CONFIRM: {
        draw_string(str_are_you_sure[current_lang], px + 30, cy, 0xFFFFFF);
        cy += 32;
        draw_string(str_all_data_on[current_lang], px + 30, cy, 0xCCCCCC);
        cy += 22;
        draw_string(drives[selected_drive].name, px + 50, cy, 0x5b9cff);
        cy += 28;
        draw_string(str_will_be_erased[current_lang], px + 30, cy, 0xFF8888);
        cy += 18;
        draw_string(str_cannot_undo[current_lang], px + 30, cy, 0xAA6666);

        int btn_y = py + ph - 54;
        draw_button_hover(px + 30, btn_y, 110, 32, str_back[current_lang], 0x2d3548, 0xCCCCCC,
            in_rect(mouse_x, mouse_y, px + 30, btn_y, 110, 32));
        draw_button_hover(px + pw - 140, btn_y, 110, 32, str_install[current_lang], 0xdc3545, 0xFFFFFF,
            in_rect(mouse_x, mouse_y, px + pw - 140, btn_y, 110, 32));
        break;
    }

    case STATE_INSTALLING: {
        draw_string(str_installing[current_lang], px + 30, cy, 0xFFFFFF);
        cy += 40;

        int bar_x = px + 30;
        int bar_w = pw - 60;
        int bar_h = 24;
        draw_rect(bar_x, cy, bar_w, bar_h, 0x333333);

        uint32_t pct = 0;
        if (install_total > 0)
            pct = install_current * 100 / install_total;
        int filled = (int)(bar_w * pct / 100);
        if (filled > bar_w) filled = bar_w;
        if (filled > 0)
            draw_rect(bar_x, cy, filled, bar_h, 0x00CC44);

        cy += bar_h + 15;

        char pct_str[8];
        itoa((int)pct, pct_str);
        int sl = str_len(pct_str);
        pct_str[sl] = '%'; pct_str[sl + 1] = 0;
        draw_string(pct_str, px + (pw - str_len(pct_str) * 8) / 2, cy, 0xFFFFFF);
        cy += 25;

        /* Sector info */
        char sec[64] = "Sector ";
        int sp = 7;
        char num[12];
        itoa((int)install_current, num);
        for (int k = 0; num[k]; k++) sec[sp++] = num[k];
        sec[sp++] = ' '; sec[sp++] = '/'; sec[sp++] = ' ';
        itoa((int)install_total, num);
        for (int k = 0; num[k]; k++) sec[sp++] = num[k];
        sec[sp] = 0;
        draw_string(sec, px + 30, cy, 0x888888);
        break;
    }

    case STATE_DONE: {
        draw_string(str_complete[current_lang], px + 30, cy, 0x22c55e);
        cy += 34;
        draw_string(str_installed_to[current_lang], px + 30, cy, 0xCCCCCC);
        cy += 22;
        draw_string(drives[selected_drive].name, px + 50, cy, 0x5b9cff);
        cy += 38;
        draw_string(str_restart_prompt[current_lang], px + 30, cy, 0xAAAAAA);
        int btn_y = py + ph - 54;
        int rx = px + (pw - 120) / 2;
        draw_button_hover(rx, btn_y, 120, 32, str_restart[current_lang], TITLEBAR_ACCENT, 0xFFFFFF,
            in_rect(mouse_x, mouse_y, rx, btn_y, 120, 32));
        break;
    }

    case STATE_ERROR: {
        draw_string(str_error[current_lang], px + 30, cy, 0xFF4444);
        cy += 35;
        draw_string(str_no_image[current_lang], px + 30, cy, 0xCCCCCC);
        cy += 22;
        draw_string(str_rebuild[current_lang], px + 30, cy, 0xCCCCCC);
        break;
    }
    }

    /* License footer (GPL v3.0) */
    {
        const char* lic = str_license[current_lang];
        int ly = py + ph - 68;
        draw_string(lic, px + (pw - str_len(lic) * 8) / 2, ly, LICENSE_COLOR);
    }

    /* Beta watermark */
    if (is_beta) {
        draw_string("Build 201", 10, scr_height - 20, 0x555555);
    }

    swap_buffers();
    draw_cursor(mouse_x, mouse_y);
    last_mx = mouse_x;
    last_my = mouse_y;
}

/* ===== Click Handling ===== */
static void handle_click(void) {
    int pw = PANEL_W, ph = PANEL_H;
    int px = (scr_width - pw) / 2;
    int py = (scr_height - ph) / 2;

    switch (setup_state) {
    case STATE_WELCOME: {
        int btn_y = py + ph - 54;
        if (in_rect(mouse_x, mouse_y, px + pw - 130, btn_y, 100, 32)) {
            setup_state = STATE_PRESS_INSTALL;
            background_drawn = 0;
            force_render_frame = 1;
        }
        break;
    }

    case STATE_PRESS_INSTALL: {
        int toggle_y = py + 68 + 148;
        int tw = 200;
        int tx = px + (pw - tw) / 2;
        if (in_rect(mouse_x, mouse_y, tx, toggle_y, tw, 22)) {
            animations_on = !animations_on;
            background_drawn = 0;
            force_render_frame = 1;
            return;
        }
        int btn_y = py + ph - 54;
        if (in_rect(mouse_x, mouse_y, px + 30, btn_y, 100, 32)) {
            setup_state = STATE_WELCOME;
            background_drawn = 0;
            force_render_frame = 1;
        }
        if (in_rect(mouse_x, mouse_y, px + pw - 130, btn_y, 100, 32)) {
            setup_state = STATE_LANGUAGE;
            background_drawn = 0;
            force_render_frame = 1;
        }
        break;
    }

    case STATE_LANGUAGE: {
        int cy = py + 68 + 48;
        if (in_rect(mouse_x, mouse_y, px + 80, cy, pw - 160, 44)) {
            current_lang = LANG_EN;
            background_drawn = 0;
            force_render_frame = 1;
            return;
        }
        cy += 52;
        if (in_rect(mouse_x, mouse_y, px + 80, cy, pw - 160, 44)) {
            current_lang = LANG_DE;
            background_drawn = 0;
            force_render_frame = 1;
            return;
        }
        int btn_y = py + ph - 54;
        if (in_rect(mouse_x, mouse_y, px + 30, btn_y, 100, 32)) {
            setup_state = STATE_PRESS_INSTALL;
            background_drawn = 0;
            force_render_frame = 1;
        }
        if (in_rect(mouse_x, mouse_y, px + pw - 130, btn_y, 100, 32)) {
            setup_state = STATE_SELECT;
            background_drawn = 0;
            force_render_frame = 1;
        }
        break;
    }

    case STATE_SELECT: {
        int cy = py + 68 + 28;
        for (int i = 0; i < drive_count && i < 10; i++) {
            int ey = cy + i * 34;
            if (in_rect(mouse_x, mouse_y, px + 30, ey, pw - 60, 28)) {
                selected_drive = i;
                background_drawn = 0;
                force_render_frame = 1;
                return;
            }
        }
        int btn_y = py + ph - 54;
        if (in_rect(mouse_x, mouse_y, px + 30, btn_y, 100, 32)) {
            setup_state = STATE_LANGUAGE;
            background_drawn = 0;
            force_render_frame = 1;
        }
        if (selected_drive >= 0 && in_rect(mouse_x, mouse_y, px + pw - 130, btn_y, 100, 32)) {
            setup_state = STATE_CONFIRM;
            background_drawn = 0;
            force_render_frame = 1;
        }
        break;
    }

    case STATE_CONFIRM: {
        int btn_y = py + ph - 54;
        if (in_rect(mouse_x, mouse_y, px + 30, btn_y, 110, 32)) {
            setup_state = STATE_SELECT;
            background_drawn = 0;
            force_render_frame = 1;
        }
        if (in_rect(mouse_x, mouse_y, px + pw - 140, btn_y, 110, 32)) {
            setup_state = STATE_INSTALLING;
            background_drawn = 0;
            force_render_frame = 1;
        }
        break;
    }

    case STATE_DONE: {
        int btn_y = py + ph - 54;
        int btn_w = 120;
        int btn_x = px + (pw - btn_w) / 2;
        if (in_rect(mouse_x, mouse_y, btn_x, btn_y, btn_w, 32)) {
            reboot();
        }
        break;
    }
    }
}

/* ===== Installation Logic ===== */
static uint8_t sector_buf[512];
static uint8_t chunk_buf[65536]; // 64KB buffer

static int install_started = 0;
static uint32_t install_next_lba = 0;
static int install_target = 0;

static void do_install(void) {
    if (!use_cdfs || cdfs_image_size == 0) {
        setup_state = STATE_ERROR;
        return;
    }

    if (!install_started) {
        install_total = (cdfs_image_size + 511) / 512;
        install_current = 0;
        install_next_lba = 0;
        install_target = drives[selected_drive].id;
        install_started = 1;
        last_drawn_pct = -1;
        last_drawn_filled = 0;
    }

    if (install_next_lba >= install_total) {
        disk_flush((uint8_t)install_target);
        setup_state = STATE_DONE;
        background_drawn = 0;
        force_render_frame = 1;
        install_started = 0;
        return;
    }

    uint32_t lba = install_next_lba;
    uint32_t chunk_size = 128; // 128 sectors = 64KB at a time
    if (lba + chunk_size > install_total)
        chunk_size = install_total - lba;

    uint32_t byte_offset = lba * 512;
    uint32_t bytes_to_read = chunk_size * 512;

    if (cdfs_read_file_chunk(cdfs_image_lba, byte_offset, chunk_buf, bytes_to_read) != 0) {
        setup_state = STATE_ERROR;
        background_drawn = 0;
        install_started = 0;
        return;
    }

    if (lba + chunk_size == install_total && (cdfs_image_size & 511)) {
        for (uint32_t i = 0; i < chunk_size - 1; i++)
            disk_write_sector((uint8_t)install_target, lba + i, chunk_buf + (i * 512));
        for (int i = 0; i < 512; i++) sector_buf[i] = 0;
        uint32_t remaining = cdfs_image_size - (install_total - 1) * 512;
        const uint8_t* last_src = chunk_buf + ((chunk_size - 1) * 512);
        for (uint32_t i = 0; i < remaining; i++) sector_buf[i] = last_src[i];
        disk_write_sector((uint8_t)install_target, install_total - 1, sector_buf);
    } else {
        disk_write_sectors((uint8_t)install_target, lba, chunk_size, chunk_buf);
    }

    install_current = lba + chunk_size;
    install_next_lba = install_current;

    uint32_t new_pct = install_current * 100 / install_total;
    if ((int)new_pct != last_drawn_pct)
        force_render_frame = 1;
}

/* Returns CPU family (4=486, 5=Pentium, ...). Returns 0 if CPUID not supported. */
static int cpu_family(void) {
    uint32_t id_orig, id_after;
    asm volatile("pushfl; popl %0" : "=r"(id_orig));
    asm volatile("pushl %0; popfl" : : "r"(id_orig ^ 0x200000));
    asm volatile("pushfl; popl %0" : "=r"(id_after));
    asm volatile("pushl %0; popfl" : : "r"(id_orig));
    if ((id_orig ^ id_after) == 0) return 0; /* CPUID not supported (e.g. 486) */
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    return (int)((eax >> 8) & 0xF);
}

/* ===== Entry Point ===== */
void kernel_main(uint32_t magic, struct multiboot_info* mbd) {
    if (magic != 0x2BADB002) return;

    gdt_install();
    idt_install();
    mouse_install();

    uint32_t total_mem = 0;
    /* Framebuffer setup */
    if (mbd->flags & (1 << 12)) {
        fb = (uint32_t*)(uint32_t)mbd->framebuffer_addr;
        scr_width = mbd->framebuffer_width;
        scr_height = mbd->framebuffer_height;
        pitch = mbd->framebuffer_pitch;
        bpp = mbd->framebuffer_bpp;

        if (mbd->flags & 1) total_mem = (mbd->mem_upper + 1024) * 1024;
        else total_mem = 16 * 1024 * 1024;

        uint32_t bb_size = scr_height * pitch;
        uint32_t bb_start = (0x400000 + 0x10000 + 0xFFF) & ~0xFFF;

        if (bb_start + bb_size <= total_mem)
            backbuffer = (uint32_t*)bb_start;
        else
            backbuffer = fb;
    }
    /* Enable animations if RAM >= 64MB and CPU is Pentium or newer */
    if (total_mem >= 64 * 1024 * 1024 && cpu_family() >= 5)
        animations_enabled = 1;
    animations_on = animations_enabled;

    if (!fb) {
        uint16_t* vga = (uint16_t*)0xB8000;
        const char* msg = "ERROR: No framebuffer";
        for (int i = 0; msg[i]; i++) vga[i] = 0x4F00 | msg[i];
        while (1) asm("hlt");
    }

    ahci_init();

    /* Try to init CDFS and find bananaos.img on CD */
    if (cdfs_init() == 0) {
        if (cdfs_find_file("BANANAOS.IMG", &cdfs_image_lba, &cdfs_image_size) == 0 ||
            cdfs_find_file("bananaos.img", &cdfs_image_lba, &cdfs_image_size) == 0 ||
            cdfs_find_file("BANANAO.IMG", &cdfs_image_lba, &cdfs_image_size) == 0 ||
            cdfs_find_file("BANANAOS", &cdfs_image_lba, &cdfs_image_size) == 0) {
            use_cdfs = 1;
        }
    }

    if (!use_cdfs)
        setup_state = STATE_ERROR;

    detect_drives();

    while (1) {
        poll_mouse();

        if (mouse_clicked) {
            handle_click();
            mouse_clicked = 0;
        }

        if (setup_state == STATE_INSTALLING) {
            do_install();
        }

        if (force_render_frame) {
            static int install_screen_drawn = 0;
            if (setup_state == STATE_INSTALLING && backbuffer == fb) {
                if (!install_screen_drawn) {
                    render();
                    install_screen_drawn = 1;
                } else {
                    render_install_progress_only();
                    swap_buffers();
                    draw_cursor(mouse_x, mouse_y);
                    last_mx = mouse_x;
                    last_my = mouse_y;
                }
            } else {
                render();
                if (setup_state != STATE_INSTALLING) install_screen_drawn = 0;
            }
            force_render_frame = 0;
        } else if (mouse_x != last_mx || mouse_y != last_my) {
            restore_cursor_fb(last_mx, last_my);
            draw_cursor(mouse_x, mouse_y);
            last_mx = mouse_x;
            last_my = mouse_y;
            if (backbuffer != fb) force_render_frame = 1;
        }

        anim_frame++;
        if (animations_on) {
            if (anim_prev_state != setup_state) {
                anim_prev_state = setup_state;
                anim_panel_off_y = -44;
            }
            if (anim_panel_off_y < 0) {
                anim_panel_off_y += 4;
                if (anim_panel_off_y > 0) anim_panel_off_y = 0;
                force_render_frame = 1;
            }
            if (backbuffer != fb)
                force_render_frame = 1;
        } else {
            anim_prev_state = setup_state;
            anim_panel_off_y = 0;
        }

        while (inb(0x3DA) & 0x08);
        while (!(inb(0x3DA) & 0x08));
    }
}
