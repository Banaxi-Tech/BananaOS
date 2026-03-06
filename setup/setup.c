#include <stdint.h>
#include <stddef.h>
#include "../font.h"
#include "../drivers/disk.h"
#include "../drivers/pci.h"
#include "../drivers/ahci.h"

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

/* ===== Cursor ===== */
static uint32_t cursor_backup[64];
static int last_mx = -1, last_my = -1;

/* ===== Image Data ===== */
static uint8_t* img_data = NULL;
static uint32_t img_size = 0;

/* ===== Setup State Machine ===== */
#define STATE_WELCOME    0
#define STATE_LANGUAGE   1
#define STATE_SELECT     2
#define STATE_CONFIRM    3
#define STATE_INSTALLING 4
#define STATE_DONE       5
#define STATE_ERROR      6

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
static const char* str_restarting[2]       = { "Your computer will restart now...", "Ihr Computer wird jetzt neu gestartet..." };
static const char* str_error[2]            = { "Error!", "Fehler!" };
static const char* str_no_image[2]         = { "No OS image found in setup media.", "Kein OS-Image im Setup-Medium gefunden." };
static const char* str_rebuild[2]          = { "Please rebuild with: make buildSetup", "Bitte neu erstellen mit: make buildSetup" };
static const char* str_setup_title[2]      = { "BananaOS Setup", "BananaOS Setup" };

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

void draw_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha) {
    if (alpha == 255) { draw_rect(x, y, w, h, color); return; }
    if (alpha == 0) return;
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

static void draw_button(int x, int y, int w, int h, const char* label, uint32_t bg, uint32_t fg) {
    draw_rect(x, y, w, h, bg);
    draw_rect(x, y, w, 1, 0x888888);
    int len = 0;
    const char* s = label;
    while (*s++) len++;
    draw_string(label, x + (w - len * 8) / 2, y + (h - 8) / 2, fg);
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

/* ===== UI Rendering ===== */
static void render(void) {
    draw_rect(0, 0, scr_width, scr_height, 0x1A1A2E);

    int pw = 600, ph = 420;
    int px = (scr_width - pw) / 2;
    int py = (scr_height - ph) / 2;

    /* Panel */
    draw_rect(px + 4, py + 4, pw, ph, 0x0A0A1A); /* shadow */
    draw_rect(px, py, pw, ph, 0x16213E);
    draw_rect(px, py, pw, 40, 0x0F3460);

    /* Title */
    const char* title = str_setup_title[current_lang];
    draw_string(title, px + (pw - str_len(title) * 8) / 2, py + 14, 0xFFFFFF);
    draw_rect(px, py + 40, pw, 1, 0x1A6FB5);

    int cy = py + 60;

    switch (setup_state) {
    case STATE_WELCOME: {
        /* Big title */
        const char* main_title = is_beta ? str_welcome_beta[current_lang] : str_welcome_title[current_lang];
        draw_string(main_title, px + (pw - str_len(main_title) * 8) / 2, cy + 60, 0xFFFFFF);
        
        /* Subtitle */
        const char* sub = str_press_next[current_lang];
        draw_string(sub, px + (pw - str_len(sub) * 8) / 2, cy + 100, 0xAAAAAA);

        int btn_x = px + pw - 130;
        int btn_y = py + ph - 50;
        draw_button(btn_x, btn_y, 100, 30, str_next[current_lang], 0x533483, 0xFFFFFF);
        break;
    }

    case STATE_LANGUAGE: {
        const char* prompt = str_select_lang[current_lang];
        draw_string(prompt, px + (pw - str_len(prompt) * 8) / 2, cy, 0xCCCCCC);
        cy += 50;

        /* English button */
        uint32_t en_bg = (current_lang == LANG_EN) ? 0x0F3460 : 0x1E2D4A;
        uint32_t en_fg = (current_lang == LANG_EN) ? 0xFFFFFF : 0xAAAAAA;
        draw_rect(px + 100, cy, pw - 200, 40, en_bg);
        draw_string("English", px + 100 + (pw - 200 - 7*8)/2, cy + 16, en_fg);

        cy += 55;

        /* German button */
        uint32_t de_bg = (current_lang == LANG_DE) ? 0x0F3460 : 0x1E2D4A;
        uint32_t de_fg = (current_lang == LANG_DE) ? 0xFFFFFF : 0xAAAAAA;
        draw_rect(px + 100, cy, pw - 200, 40, de_bg);
        draw_string("Deutsch", px + 100 + (pw - 200 - 7*8)/2, cy + 16, de_fg);

        int btn_y = py + ph - 50;
        draw_button(px + 30, btn_y, 100, 30, str_back[current_lang], 0x444444, 0xFFFFFF);
        draw_button(px + pw - 130, btn_y, 100, 30, str_next[current_lang], 0x533483, 0xFFFFFF);
        break;
    }

    case STATE_SELECT: {
        draw_string(str_select_drive[current_lang], px + 30, cy, 0xCCCCCC);
        cy += 30;

        if (drive_count == 0) {
            draw_string(str_no_drives[current_lang], px + 30, cy, 0xFF6666);
        } else {
            for (int i = 0; i < drive_count && i < 10; i++) {
                int ey = cy + i * 32;
                uint32_t bg = (i == selected_drive) ? 0x0F3460 : 0x1E2D4A;
                uint32_t fg = (i == selected_drive) ? 0xFFFFFF : 0xAAAAAA;
                draw_rect(px + 30, ey, pw - 60, 26, bg);
                /* radio indicator */
                draw_rect(px + 38, ey + 8, 10, 10, 0x444444);
                if (i == selected_drive)
                    draw_rect(px + 40, ey + 10, 6, 6, 0x00AAFF);
                draw_string(drives[i].name, px + 58, ey + 9, fg);

                /* Show capacity */
                if (drives[i].size_mb > 0) {
                    char size_str[20];
                    format_size(drives[i].size_mb, size_str);
                    draw_string(size_str, px + pw - 120, ey + 9, 0x888888);
                }
            }
        }

        int btn_y = py + ph - 50;
        draw_button(px + 30, btn_y, 100, 30, str_back[current_lang], 0x444444, 0xFFFFFF);
        uint32_t bbg = (selected_drive >= 0) ? 0x533483 : 0x333333;
        uint32_t bfg = (selected_drive >= 0) ? 0xFFFFFF : 0x666666;
        draw_button(px + pw - 130, btn_y, 100, 30, str_next[current_lang], bbg, bfg);
        break;
    }

    case STATE_CONFIRM: {
        draw_string(str_are_you_sure[current_lang], px + 30, cy, 0xFFFFFF);
        cy += 35;
        draw_string(str_all_data_on[current_lang], px + 30, cy, 0xCCCCCC);
        cy += 22;
        draw_string(drives[selected_drive].name, px + 50, cy, 0x00AAFF);
        cy += 30;
        draw_string(str_will_be_erased[current_lang], px + 30, cy, 0xFF8888);
        cy += 18;
        draw_string(str_cannot_undo[current_lang], px + 30, cy, 0xFF8888);

        int btn_y = py + ph - 50;
        draw_button(px + 30, btn_y, 120, 30, str_back[current_lang], 0x444444, 0xFFFFFF);
        draw_button(px + pw - 150, btn_y, 120, 30, str_install[current_lang], 0xCC3333, 0xFFFFFF);
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
        draw_string(str_complete[current_lang], px + 30, cy, 0x00FF88);
        cy += 35;
        draw_string(str_installed_to[current_lang], px + 30, cy, 0xCCCCCC);
        cy += 22;
        draw_string(drives[selected_drive].name, px + 50, cy, 0x00AAFF);
        cy += 40;
        draw_string(str_restarting[current_lang], px + 30, cy, 0xFFFFFF);
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
    int pw = 600, ph = 420;
    int px = (scr_width - pw) / 2;
    int py = (scr_height - ph) / 2;

    switch (setup_state) {
    case STATE_WELCOME: {
        int btn_x = px + pw - 130;
        int btn_y = py + ph - 50;
        if (in_rect(mouse_x, mouse_y, btn_x, btn_y, 100, 30)) {
            setup_state = STATE_LANGUAGE;
            force_render_frame = 1;
        }
        break;
    }

    case STATE_LANGUAGE: {
        int cy = py + 60 + 50;
        /* English button */
        if (in_rect(mouse_x, mouse_y, px + 100, cy, pw - 200, 40)) {
            current_lang = LANG_EN;
            force_render_frame = 1;
            return;
        }
        cy += 55;
        /* German button */
        if (in_rect(mouse_x, mouse_y, px + 100, cy, pw - 200, 40)) {
            current_lang = LANG_DE;
            force_render_frame = 1;
            return;
        }

        int btn_y = py + ph - 50;
        /* Back */
        if (in_rect(mouse_x, mouse_y, px + 30, btn_y, 100, 30)) {
            setup_state = STATE_WELCOME;
            force_render_frame = 1;
        }
        /* Next */
        if (in_rect(mouse_x, mouse_y, px + pw - 130, btn_y, 100, 30)) {
            setup_state = STATE_SELECT;
            force_render_frame = 1;
        }
        break;
    }

    case STATE_SELECT: {
        int cy = py + 60 + 30;
        for (int i = 0; i < drive_count && i < 10; i++) {
            int ey = cy + i * 32;
            if (in_rect(mouse_x, mouse_y, px + 30, ey, pw - 60, 26)) {
                selected_drive = i;
                force_render_frame = 1;
                return;
            }
        }
        int btn_y = py + ph - 50;
        /* Back */
        if (in_rect(mouse_x, mouse_y, px + 30, btn_y, 100, 30)) {
            setup_state = STATE_LANGUAGE;
            force_render_frame = 1;
        }
        /* Next */
        if (selected_drive >= 0 && in_rect(mouse_x, mouse_y, px + pw - 130, btn_y, 100, 30)) {
            setup_state = STATE_CONFIRM;
            force_render_frame = 1;
        }
        break;
    }

    case STATE_CONFIRM: {
        int btn_y = py + ph - 50;
        if (in_rect(mouse_x, mouse_y, px + 30, btn_y, 120, 30)) {
            setup_state = STATE_SELECT;
            force_render_frame = 1;
        }
        if (in_rect(mouse_x, mouse_y, px + pw - 150, btn_y, 120, 30)) {
            setup_state = STATE_INSTALLING;
            force_render_frame = 1;
        }
        break;
    }
    }
}

/* ===== Installation Logic ===== */
static uint8_t last_sector_buf[512];

static void do_install(void) {
    if (!img_data || img_size == 0) {
        setup_state = STATE_ERROR;
        return;
    }

    install_total = (img_size + 511) / 512;
    install_current = 0;

    int target = drives[selected_drive].id;
    uint32_t chunk_size = 128; // 64KB at a time

    for (uint32_t lba = 0; lba < install_total; lba += chunk_size) {
        if (lba + chunk_size > install_total)
            chunk_size = install_total - lba;

        const uint8_t* src = &img_data[lba * 512];

        /* Handle padding for the very last chunk if it's partial and doesn't align to 512 */
        /* Actually img_data is a module in memory, so we can just write the sectors. */
        /* If the last sector of the image is partial, disk_write_sectors will handle it if we pad it. */
        
        if (lba + chunk_size == install_total && (img_size & 511)) {
            // Very last sector might need padding
            for (uint32_t i = 0; i < chunk_size - 1; i++) {
                disk_write_sector((uint8_t)target, lba + i, src + (i * 512));
            }
            // Final sector
            for (int i = 0; i < 512; i++) last_sector_buf[i] = 0;
            uint32_t remaining = img_size - (install_total - 1) * 512;
            const uint8_t* last_src = &img_data[(install_total - 1) * 512];
            for (uint32_t i = 0; i < remaining; i++) last_sector_buf[i] = last_src[i];
            disk_write_sector((uint8_t)target, install_total - 1, last_sector_buf);
        } else {
            disk_write_sectors((uint8_t)target, lba, chunk_size, src);
        }

        install_current = lba + chunk_size;

        if ((lba % 512) == 0 || install_current >= install_total) {
            poll_mouse();
            render();
        }
    }

    disk_flush((uint8_t)target);

    /* Clear OS image from RAM to free memory before reboot */
    if (img_data && img_size > 0) {
        uint32_t dwords = img_size / 4;
        uint32_t* p = (uint32_t*)img_data;
        for (uint32_t i = 0; i < dwords; i++) p[i] = 0;
        for (uint32_t i = dwords * 4; i < img_size; i++) img_data[i] = 0;
        img_data = NULL;
        img_size = 0;
    }

    setup_state = STATE_DONE;
}

/* ===== Entry Point ===== */
void kernel_main(uint32_t magic, struct multiboot_info* mbd) {
    if (magic != 0x2BADB002) return;

    gdt_install();
    idt_install();
    mouse_install();

    /* Parse modules before AHCI init (AHCI uses memory at 0x600000) */
    uint32_t mod_src = 0, mod_len = 0;
    uint32_t highest_mod_end = 0x400000;

    if (mbd->flags & (1 << 3)) {
        if (mbd->mods_count > 0) {
            struct multiboot_mod_list* mods = (struct multiboot_mod_list*)mbd->mods_addr;
            for (uint32_t i = 0; i < mbd->mods_count; i++) {
                if (mods[i].mod_end > highest_mod_end)
                    highest_mod_end = mods[i].mod_end;
            }
            mod_src = mods[0].mod_start;
            mod_len = mods[0].mod_end - mods[0].mod_start;
        }
    }

    /*
     * Copy the OS image to safe memory above all modules so AHCI init
     * (which uses 0x600000) doesn't corrupt it.
     */
    uint32_t copy_addr = (highest_mod_end + 0xFFF) & ~0xFFF;
    if (mod_len > 0) {
        uint8_t* dst = (uint8_t*)copy_addr;
        uint8_t* src_ptr = (uint8_t*)mod_src;
        uint32_t dwords = mod_len / 4;
        void* d = dst;
        void* s = src_ptr;
        asm volatile("rep movsl" : "+D"(d), "+S"(s), "+c"(dwords) : : "memory");
        for (uint32_t i = dwords * 4; i < mod_len; i++) dst[i] = src_ptr[i];
        img_data = dst;
        img_size = mod_len;
    }

    ahci_init();

    /* Framebuffer setup */
    if (mbd->flags & (1 << 12)) {
        fb = (uint32_t*)(uint32_t)mbd->framebuffer_addr;
        scr_width = mbd->framebuffer_width;
        scr_height = mbd->framebuffer_height;
        pitch = mbd->framebuffer_pitch;
        bpp = mbd->framebuffer_bpp;

        uint32_t total_mem = 0;
        if (mbd->flags & 1) total_mem = (mbd->mem_upper + 1024) * 1024;
        else total_mem = 16 * 1024 * 1024;

        uint32_t bb_size = scr_height * pitch;
        uint32_t bb_start = ((copy_addr + mod_len + 0x10000) + 0xFFF) & ~0xFFF;

        if (bb_start + bb_size <= total_mem)
            backbuffer = (uint32_t*)bb_start;
        else
            backbuffer = fb;
    }

    if (!fb) {
        uint16_t* vga = (uint16_t*)0xB8000;
        const char* msg = "ERROR: No framebuffer";
        for (int i = 0; msg[i]; i++) vga[i] = 0x4F00 | msg[i];
        while (1) asm("hlt");
    }

    if (!img_data || img_size == 0)
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
            force_render_frame = 1;
        }

        if (setup_state == STATE_DONE) {
            render();
            
            /* Wait for user to see the completion message */
            for (volatile int i = 0; i < 50000000; i++);
            
            /* Clear backbuffer to reduce memory pressure on reboot */
            if (backbuffer && backbuffer != fb) {
                uint32_t bb_dwords = (scr_height * pitch) / 4;
                for (uint32_t i = 0; i < bb_dwords; i++)
                    ((uint32_t*)backbuffer)[i] = 0;
            }
            
            /* Small delay to ensure all writes complete */
            for (volatile int i = 0; i < 10000000; i++);
            
            reboot();
        }

        if (force_render_frame) {
            render();
            force_render_frame = 0;
        } else if (mouse_x != last_mx || mouse_y != last_my) {
            restore_cursor_fb(last_mx, last_my);
            draw_cursor(mouse_x, mouse_y);
            last_mx = mouse_x;
            last_my = mouse_y;
        }

        while (inb(0x3DA) & 0x08);
        while (!(inb(0x3DA) & 0x08));
    }
}
