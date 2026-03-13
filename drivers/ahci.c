#include "ahci.h"
#include "pci.h"
#include <stddef.h>

static HBA_MEM* abar = NULL;
static int ahci_ports[32]; // 0: none, 1: SATA

// Static memory for AHCI structures to avoid complex allocation
// Each port needs ~4KB for command list + FIS + command table(s)
// Using 8KB per port at 0x200000 (2MB) - safe for systems with 4MB+ RAM
#define AHCI_BASE_MEM   0x200000
#define AHCI_PORT_SIZE  8192 

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
                uint32_t sig = abar->ports[i].sig;
                if (sig == SATA_SIG_ATA)
                    ahci_ports[i] = AHCI_DEV_SATA;
                else if (sig == SATA_SIG_ATAPI)
                    ahci_ports[i] = AHCI_DEV_SATAPI;
                else
                    continue;

                // Basic initialization for the port (SATA and SATAPI)
                abar->ports[i].cmd &= ~((1 << 0) | (1 << 4)); // ST=0, FRE=0
                int timeout = 1000000;
                while ((abar->ports[i].cmd & ((1 << 15) | (1 << 14))) && timeout--) {
                    asm volatile("pause");
                }

                uint8_t* pmem = (uint8_t*)(AHCI_BASE_MEM + (i * AHCI_PORT_SIZE));
                for (int m = 0; m < AHCI_PORT_SIZE; m++) pmem[m] = 0;

                uint32_t port_base = AHCI_BASE_MEM + (i * AHCI_PORT_SIZE);
                abar->ports[i].clb = port_base;           // Command list at offset 0
                abar->ports[i].clbu = 0;
                abar->ports[i].fb = port_base + 1024;     // FIS at offset 1024
                abar->ports[i].fbu = 0;

                HBA_CMD_HEADER* hdr = (HBA_CMD_HEADER*)port_base;
                // Only initialize command slot 0 (we only use slot 0)
                hdr[0].ctba = port_base + 2048;           // Command table at offset 2048
                hdr[0].ctbau = 0;
                hdr[0].prdtl = 0;

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
    cmd_hdr->a = 0; // Not ATAPI
    cmd_hdr->w = 0; // Read
    cmd_hdr->prdtl = 1;

    HBA_CMD_TBL* cmd_tbl = (HBA_CMD_TBL*)cmd_hdr->ctba;
    for (int i = 0; i < 128; i++) ((uint8_t*)cmd_tbl)[i] = 0;

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

int ahci_write(int port, uint32_t lba, uint32_t count, const uint16_t* buffer) {
    if (!abar || ahci_ports[port] != AHCI_DEV_SATA) return 0;

    HBA_PORT* p = &abar->ports[port];
    p->is = 0xFFFFFFFF;
    p->serr = 0xFFFFFFFF;

    HBA_CMD_HEADER* cmd_hdr = (HBA_CMD_HEADER*)p->clb;
    cmd_hdr->cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
    cmd_hdr->a = 0; // Not ATAPI
    cmd_hdr->w = 1; // Write direction: host to device
    cmd_hdr->prdtl = 1;

    HBA_CMD_TBL* cmd_tbl = (HBA_CMD_TBL*)cmd_hdr->ctba;
    for (int i = 0; i < 128; i++) ((uint8_t*)cmd_tbl)[i] = 0;

    cmd_tbl->prdt_entry[0].dba = (uint32_t)buffer;
    cmd_tbl->prdt_entry[0].dbau = 0;
    cmd_tbl->prdt_entry[0].dbc = (count * 512) - 1;
    cmd_tbl->prdt_entry[0].i = 1;

    FIS_REG_H2D* fis = (FIS_REG_H2D*)(&cmd_tbl->cfis);
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = 0x35; // WRITE DMA EXT

    fis->lba0 = (uint8_t)lba;
    fis->lba1 = (uint8_t)(lba >> 8);
    fis->lba2 = (uint8_t)(lba >> 16);
    fis->device = 1 << 6;

    fis->lba3 = (uint8_t)(lba >> 24);
    fis->countl = (uint8_t)count;
    fis->counth = (uint8_t)(count >> 8);

    int spin = 0;
    while ((p->tfd & (0x80 | 0x08)) && spin < 1000000) spin++;
    if (spin == 1000000) return 0;

    p->ci = 1;

    int timeout = 1000000;
    while (timeout--) {
        if ((p->ci & 1) == 0) break;
        if (p->is & (uint32_t)(1 << 30)) return 0;
        asm volatile("pause");
    }

    if (timeout <= 0) return 0;
    return 1;
}

int ahci_get_satapi_port(void) {
    if (!abar) return -1;
    for (int i = 0; i < 32; i++) {
        if (ahci_ports[i] == AHCI_DEV_SATAPI)
            return i;
    }
    return -1;
}

/* Read one 2048-byte sector from SATAPI (CD) device via AHCI PACKET command */
int ahci_satapi_read_sector(int port, uint32_t lba, uint8_t* buffer) {
    if (!abar || port < 0 || port >= 32 || ahci_ports[port] != AHCI_DEV_SATAPI)
        return 0;

    HBA_PORT* p = &abar->ports[port];
    p->is = 0xFFFFFFFF;
    p->serr = 0xFFFFFFFF;

    HBA_CMD_HEADER* cmd_hdr = (HBA_CMD_HEADER*)p->clb;
    cmd_hdr->cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
    cmd_hdr->a = 1;   /* ATAPI */
    cmd_hdr->w = 0;   /* Read */
    cmd_hdr->prdtl = 1;

    HBA_CMD_TBL* cmd_tbl = (HBA_CMD_TBL*)cmd_hdr->ctba;
    for (int i = 0; i < 160; i++)
        ((uint8_t*)cmd_tbl)[i] = 0;

    /* ATAPI PACKET (0xA0): DMA data-in, 2048 bytes */
    FIS_REG_H2D* fis = (FIS_REG_H2D*)(&cmd_tbl->cfis);
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = 0xA0;   /* ATAPI PACKET */
    fis->featurel = 1;     /* DMA bit set (use DMA for data transfer) */
    fis->lba1 = 0x00;      /* byte count low: 2048 & 0xFF */
    fis->lba2 = 0x08;      /* byte count high: 2048 >> 8 */
    fis->device = 0;

    /* READ(10) CDB: opcode 0x28, LBA big-endian, transfer length 1 sector */
    uint8_t* acmd = cmd_tbl->acmd;
    acmd[0] = 0x28;                   /* READ(10) opcode */
    acmd[1] = 0;
    acmd[2] = (lba >> 24) & 0xFF;     /* LBA byte 3 (MSB) */
    acmd[3] = (lba >> 16) & 0xFF;     /* LBA byte 2 */
    acmd[4] = (lba >> 8) & 0xFF;      /* LBA byte 1 */
    acmd[5] = lba & 0xFF;             /* LBA byte 0 (LSB) */
    acmd[6] = 0;                      /* Reserved/Group */
    acmd[7] = 0;                      /* Transfer length MSB */
    acmd[8] = 1;                      /* Transfer length LSB = 1 sector */
    acmd[9] = 0;                      /* Control */
    acmd[10] = 0;
    acmd[11] = 0;

    cmd_tbl->prdt_entry[0].dba = (uint32_t)buffer;
    cmd_tbl->prdt_entry[0].dbau = 0;
    cmd_tbl->prdt_entry[0].dbc = 2048 - 1;
    cmd_tbl->prdt_entry[0].i = 1;

    int spin = 0;
    while ((p->tfd & (0x80 | 0x08)) && spin < 1000000) spin++;
    if (spin == 1000000) return 0;

    p->ci = 1;

    int timeout = 1000000;
    while (timeout--) {
        if ((p->ci & 1) == 0) break;
        if (p->is & (uint32_t)(1 << 30)) return 0;
        asm volatile("pause");
    }
    if (timeout <= 0) return 0;
    return 1;
}
