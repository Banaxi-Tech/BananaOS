#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ( "inb %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

// Wait until the mouse controller is ready
void mouse_wait(uint8_t a_type) {
    uint32_t _time_out = 100000;
    if(a_type == 0) {
        while(_time_out--) {
            if((inb(0x64) & 1) == 1) return;
        }
    } else {
        while(_time_out--) {
            if((inb(0x64) & 2) == 0) return;
        }
    }
}

void mouse_write(uint8_t a_write) {
    mouse_wait(1);
    outb(0x64, 0xD4); // Command pointing device
    mouse_wait(1);
    outb(0x60, a_write);
    mouse_wait(0);
    inb(0x60); // Acknowledge
}

uint8_t mouse_read() {
    mouse_wait(0);
    return inb(0x60);
}

void mouse_install() {
    // Enable auxiliary device
    mouse_wait(1);
    outb(0x64, 0xA8); 
    
    // Enable interrupts for the mouse
    mouse_wait(1);
    outb(0x64, 0x20); 
    mouse_wait(0);
    uint8_t status = inb(0x60) | 2;
    
    mouse_wait(1);
    outb(0x64, 0x60);
    mouse_wait(1);
    outb(0x60, status);
    
    // Set default settings
    mouse_write(0xF6);
    mouse_read();
    
    // Enable Data Reporting
    mouse_write(0xF4);
    mouse_read();
}
