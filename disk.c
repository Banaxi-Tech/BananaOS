#include "disk.h"
#include <stdint.h>

// --- I/O Ports ---
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ( "inb %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    asm volatile ( "outw %0, %1" : : "a"(val), "Nd"(port) );
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile ( "inw %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

int ata_wait_bsy() {
    uint32_t timeout = 100000;
    while (timeout--) {
        if (!(inb(0x1F7) & 0x80)) return 1;
    }
    return 0; // Timeout
}

int ata_wait_drq() {
    uint32_t timeout = 100000;
    while (timeout--) {
        if (inb(0x1F7) & 0x08) return 1;
    }
    return 0; // Timeout
}

// Helper function to wait for both busy and DRQ
static inline int ata_wait_ready() {
    if (!ata_wait_bsy()) return 0;
    if (!ata_wait_drq()) return 0;
    return 1;
}

// Helper function to read data word
static inline uint16_t ata_read_data() {
    return inw(0x1F0);
}

#include "ahci.h"

void disk_read_sector(uint8_t drive, uint32_t lba, uint8_t* buffer) {
    if (drive < 2) {
        ata_read_sector(drive, lba, buffer);
    } else {
        ahci_read(drive - 2, lba, 1, (uint16_t*)buffer);
    }
}

int disk_drive_exists(uint8_t drive) {
    if (drive < 2) return ata_drive_exists(drive);
    return ahci_drive_exists(drive - 2);
}

void ata_read_sector(uint8_t drive, uint32_t lba, uint8_t* buffer) {
    if (!ata_wait_bsy()) return; // Only wait for BSY before sending command
    outb(0x1F6, (uint8_t)(0xE0 | (drive << 4) | ((lba >> 24) & 0x0F))); 
    outb(0x1F2, 1);
    outb(0x1F3, (uint8_t)lba);
    outb(0x1F4, (uint8_t)(lba >> 8));
    outb(0x1F5, (uint8_t)(lba >> 16));
    outb(0x1F7, 0x20); // Read command

    if (!ata_wait_ready()) return; // Wait for BSY to clear AND DRQ to set after command
    for (int i = 0; i < 256; i++) {
        uint16_t data = ata_read_data();
        buffer[i * 2] = (uint8_t)data;
        buffer[i * 2 + 1] = (uint8_t)(data >> 8);
    }
}

int ata_drive_exists(uint8_t drive) {
    outb(0x1F6, (uint8_t)(0xE0 | (drive << 4)));
    // Wait a tiny bit for the drive to select
    for(int i=0; i<1000; i++) inb(0x1F7); 
    
    uint8_t status = inb(0x1F7);
    if (status == 0xFF) return 0; // Floating bus, no drive
    if (status == 0x00) return 0; // No drive
    return 1;
}
