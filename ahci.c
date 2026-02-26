#include "ahci.h"
#include "pci.h"
#include "apps.h" // for draw_string debugging if needed
#include <stddef.h>

static HBA_MEM* abar = NULL;
static int ahci_ports[32]; // 0: none, 1: SATA

// Static memory for AHCI structures to avoid complex allocation
// Using a 128KB block at a safe address for 32 ports
#define AHCI_BASE_MEM 0x600000 

void ahci_init() {
    // 1. Find AHCI Controller via PCI
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t vend_dev = pci_config_read(bus, slot, 0, 0);
            if (vend_dev == 0xFFFFFFFF) continue;

            uint32_t class_reg = pci_config_read(bus, slot, 0, 0x08);
            uint8_t class = (class_reg >> 24) & 0xFF;
            uint8_t subclass = (class_reg >> 16) & 0xFF;

            if (class == 0x01 && subclass == 0x06) {
                // Found AHCI Controller
                uint32_t bar5 = pci_config_read(bus, slot, 0, 0x24);
                abar = (HBA_MEM*)(bar5 & 0xFFFFFFF0);
                
                // Enable Bus Mastering and MMIO
                uint32_t pci_cmd = pci_config_read(bus, slot, 0, 0x04);
                pci_cmd |= (1 << 1) | (1 << 2); // Memory Space + Bus Master
                pci_config_write(bus, slot, 0, 0x04, pci_cmd);
                
                // Set AHCI Enable bit in GHC
                abar->ghc |= (uint32_t)(1 << 31);
                goto found;
            }
        }
    }
    return;

found:
    if (!abar) return;

    // 2. Identify implemented ports
    uint32_t pi = abar->pi;
    for (int i = 0; i < 32; i++) {
        ahci_ports[i] = AHCI_DEV_NULL;
        if (pi & (1 << i)) {
            uint32_t ssts = abar->ports[i].ssts;
            uint8_t det = ssts & 0x0F;
            uint8_t ipm = (ssts >> 8) & 0x0F;

            if (det == HBA_PORT_DET_PRESENT && ipm == HBA_PORT_IPM_ACTIVE) {
                if (abar->ports[i].sig == SATA_SIG_ATA) {
                    ahci_ports[i] = AHCI_DEV_SATA;
                    
                    // Basic initialization for the port
                    // Stop engine
                    abar->ports[i].cmd &= ~((1 << 0) | (1 << 4)); // ST=0, FRE=0
                    
                    // Wait for CR, FR to clear with timeout
                    int timeout = 1000000;
                    while ((abar->ports[i].cmd & ((1 << 15) | (1 << 14))) && timeout--) {
                        asm volatile("pause");
                    }

                    // Zero out the memory block for this port's structures
                    uint8_t* pmem = (uint8_t*)(AHCI_BASE_MEM + (i * 4096));
                    for (int m=0; m<4096; m++) pmem[m] = 0;

                    // Set addresses (using static block)
                    uint32_t port_base = AHCI_BASE_MEM + (i * 4096);
                    abar->ports[i].clb = port_base;
                    abar->ports[i].clbu = 0;
                    abar->ports[i].fb = port_base + 1024;
                    abar->ports[i].fbu = 0;
                    
                    // Command table base at +2KB
                    HBA_CMD_HEADER* hdr = (HBA_CMD_HEADER*)port_base;
                    for (int j=0; j<32; j++) {
                        hdr[j].ctba = port_base + 2048 + (j * 256);
                        hdr[j].ctbau = 0;
                    }

                    // Start engine
                    timeout = 1000000;
                    while ((abar->ports[i].cmd & (1 << 15)) && timeout--) {
                        asm volatile("pause");
                    }
                    abar->ports[i].cmd |= (1 << 4); // FRE=1
                    abar->ports[i].cmd |= (1 << 0); // ST=1
                }
            }
        }
    }
}

int ahci_drive_exists(int port) {
    if (port < 0 || port >= 32) return 0;
    return ahci_ports[port] == AHCI_DEV_SATA;
}

int ahci_read(int port, uint32_t lba, uint32_t count, uint16_t* buffer) {
    if (!abar || ahci_ports[port] != AHCI_DEV_SATA) return 0;

    HBA_PORT* p = &abar->ports[port];
    p->is = 0xFFFFFFFF; // Clear interrupts
    p->serr = 0xFFFFFFFF; // Clear errors

    HBA_CMD_HEADER* cmd_hdr = (HBA_CMD_HEADER*)p->clb;
    cmd_hdr->cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
    cmd_hdr->w = 0; // Read
    cmd_hdr->prdtl = 1;

    HBA_CMD_TBL* cmd_tbl = (HBA_CMD_TBL*)cmd_hdr->ctba;
    for (int i=0; i<(int)sizeof(HBA_CMD_TBL); i++) ((uint8_t*)cmd_tbl)[i] = 0;

    cmd_tbl->prdt_entry[0].dba = (uint32_t)buffer;
    cmd_tbl->prdt_entry[0].dbau = 0;
    cmd_tbl->prdt_entry[0].dbc = (count * 512) - 1;
    cmd_tbl->prdt_entry[0].i = 1;

    FIS_REG_H2D* fis = (FIS_REG_H2D*)(&cmd_tbl->cfis);
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1; // Command
    fis->command = 0x25; // READ DMA EXT (LBA48)

    fis->lba0 = (uint8_t)lba;
    fis->lba1 = (uint8_t)(lba >> 8);
    fis->lba2 = (uint8_t)(lba >> 16);
    fis->device = 1 << 6; // LBA mode

    fis->lba3 = (uint8_t)(lba >> 24);
    fis->countl = (uint8_t)count;
    fis->counth = (uint8_t)(count >> 8);

    // Wait for port not busy
    int spin = 0;
    while ((p->tfd & (0x80 | 0x08)) && spin < 1000000) spin++;
    if (spin == 1000000) return 0;

    p->ci = 1; // Issue command

    int timeout = 1000000;
    while (timeout--) {
        if ((p->ci & 1) == 0) break;
        if (p->is & (uint32_t)(1 << 30)) return 0; // Task file error
        asm volatile("pause");
    }

    if (timeout <= 0) return 0; // Timeout
    return 1;
}
