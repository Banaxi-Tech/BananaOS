#include "cdfs.h"
#include "ahci.h"
#include <stddef.h>

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outsw(uint16_t port, const void* addr, uint32_t count) {
    asm volatile("cld; rep outsw" : "+S"(addr), "+c"(count) : "d"(port) : "memory");
}

static inline void insw(uint16_t port, void* addr, uint32_t count) {
    asm volatile("cld; rep insw" : "+D"(addr), "+c"(count) : "d"(port) : "memory");
}

#define ATA_PRIMARY_BASE   0x1F0
#define ATA_PRIMARY_CTRL   0x3F6
#define ATA_SECONDARY_BASE 0x170
#define ATA_SECONDARY_CTRL 0x376

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

static uint16_t cd_base = 0;
static uint16_t cd_ctrl = 0;
static uint8_t  cd_slave = 0;
static int      cd_found = 0;
static int      use_ahci_satapi = 0;  /* 1 = read CD via AHCI SATAPI instead of IDE ATAPI */
static int      ahci_satapi_port = -1;
static uint32_t root_lba = 0;
static uint32_t root_len = 0;

static void ata_400ns_delay(uint16_t ctrl) {
    inb(ctrl); inb(ctrl); inb(ctrl); inb(ctrl);
}

static int ata_poll_bsy(uint16_t base, int timeout) {
    while (timeout-- > 0) {
        if (!(inb(base + 7) & ATA_SR_BSY))
            return 0;
    }
    return -1;
}

static int ata_poll_drq(uint16_t base, int timeout) {
    while (timeout-- > 0) {
        uint8_t s = inb(base + 7);
        if (s & ATA_SR_ERR) return -1;
        if (s & ATA_SR_DRQ) return 0;
    }
    return -1;
}

static int detect_atapi(uint16_t base, uint16_t ctrl, int slave) {
    outb(base + 6, 0xA0 | (slave << 4));
    ata_400ns_delay(ctrl);

    outb(base + 2, 0);
    outb(base + 3, 0);
    outb(base + 4, 0);
    outb(base + 5, 0);
    outb(base + 7, 0xEC); // ATA IDENTIFY

    ata_400ns_delay(ctrl);

    uint8_t status = inb(base + 7);
    if (status == 0) return 0;

    if (ata_poll_bsy(base, 100000) != 0) return 0;

    uint8_t lba_mid = inb(base + 4);
    uint8_t lba_hi  = inb(base + 5);

    if (lba_mid == 0x14 && lba_hi == 0xEB)
        return 1; // ATAPI
    if (lba_mid == 0x69 && lba_hi == 0x96)
        return 1; // ATAPI (SATA bridged)

    return 0;
}

static int atapi_read_sector(uint32_t lba, uint8_t* buf) {
    if (!cd_found) return -1;

    if (use_ahci_satapi && ahci_satapi_port >= 0) {
        if (ahci_satapi_read_sector(ahci_satapi_port, lba, buf))
            return 0;
        return -1;
    }

    outb(cd_base + 6, 0xA0 | (cd_slave << 4));
    ata_400ns_delay(cd_ctrl);

    outb(cd_base + 1, 0);       // Features: no DMA, no overlap
    outb(cd_base + 4, 0x00);    // Byte count low  (2048 & 0xFF)
    outb(cd_base + 5, 0x08);    // Byte count high (2048 >> 8)
    outb(cd_base + 7, 0xA0);    // PACKET command

    ata_400ns_delay(cd_ctrl);

    if (ata_poll_drq(cd_base, 100000) != 0)
        return -1;

    uint8_t packet[12];
    packet[0]  = 0x28;  // READ(10)
    packet[1]  = 0;
    packet[2]  = (lba >> 24) & 0xFF;
    packet[3]  = (lba >> 16) & 0xFF;
    packet[4]  = (lba >> 8) & 0xFF;
    packet[5]  = lba & 0xFF;
    packet[6]  = 0;
    packet[7]  = 0;     // Transfer length MSB
    packet[8]  = 1;     // Transfer length LSB = 1 sector
    packet[9]  = 0;
    packet[10] = 0;
    packet[11] = 0;

    outsw(cd_base, packet, 6); // 12 bytes = 6 words

    if (ata_poll_bsy(cd_base, 200000) != 0)
        return -1;
    if (ata_poll_drq(cd_base, 200000) != 0)
        return -1;

    insw(cd_base, buf, 2048 / 2); // 2048 bytes = 1024 words

    // Wait for BSY to clear after transfer
    ata_poll_bsy(cd_base, 100000);

    return 0;
}

static int str_match_nocase(const char* a, const char* b, int len) {
    for (int i = 0; i < len; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return 0;
    }
    return 1;
}

int cdfs_init(void) {
    uint16_t bases[] = { ATA_PRIMARY_BASE, ATA_SECONDARY_BASE };
    uint16_t ctrls[] = { ATA_PRIMARY_CTRL, ATA_SECONDARY_CTRL };

    /* Try IDE/ATAPI first (primary and secondary bus) */
    for (int bus = 0; bus < 2; bus++) {
        for (int slave = 0; slave < 2; slave++) {
            if (detect_atapi(bases[bus], ctrls[bus], slave)) {
                cd_base  = bases[bus];
                cd_ctrl  = ctrls[bus];
                cd_slave = slave;
                cd_found = 1;
                use_ahci_satapi = 0;
                goto read_pvd;
            }
        }
    }

    /* No IDE ATAPI: try AHCI SATAPI (e.g. CD attached as SATA) */
    ahci_init();
    ahci_satapi_port = ahci_get_satapi_port();
    if (ahci_satapi_port >= 0) {
        cd_found = 1;
        use_ahci_satapi = 1;
        goto read_pvd;
    }

    return -1;

read_pvd:;
    static uint8_t pvd_buf[2048];
    if (atapi_read_sector(16, pvd_buf) != 0)
        return -1;

    ISO9660_PVD* pvd = (ISO9660_PVD*)pvd_buf;
    if (pvd->type_code != 1)
        return -1;
    if (pvd->identifier[0] != 'C' || pvd->identifier[1] != 'D' ||
        pvd->identifier[2] != '0' || pvd->identifier[3] != '0' ||
        pvd->identifier[4] != '1')
        return -1;

    ISO9660_DirRecord* root = (ISO9660_DirRecord*)pvd->root_directory_record;
    root_lba = root->extent_lba_le;
    root_len = root->data_length_le;

    return 0;
}

int cdfs_find_file(const char* filename, uint32_t* lba_out, uint32_t* size_out) {
    if (!cd_found || root_lba == 0)
        return -1;

    int fn_len = 0;
    while (filename[fn_len]) fn_len++;

    static uint8_t dir_buf[2048];
    uint32_t sectors = (root_len + 2047) / 2048;

    for (uint32_t s = 0; s < sectors; s++) {
        if (atapi_read_sector(root_lba + s, dir_buf) != 0)
            return -1;

        uint32_t off = 0;
        while (off < 2048) {
            ISO9660_DirRecord* rec = (ISO9660_DirRecord*)(dir_buf + off);
            if (rec->length == 0) break;

            if (rec->name_length > 0 && rec->name[0] > 1) {
                int nlen = rec->name_length;
                // Strip ";1" version suffix
                while (nlen > 0 && rec->name[nlen-1] == '1') {
                    if (nlen > 1 && rec->name[nlen-2] == ';') {
                        nlen -= 2;
                        break;
                    }
                    break;
                }
                // Strip trailing '.'
                if (nlen > 0 && rec->name[nlen-1] == '.' &&
                    fn_len > 0 && filename[fn_len-1] != '.')
                    nlen--;

                if (nlen == fn_len && str_match_nocase(rec->name, filename, nlen)) {
                    *lba_out  = rec->extent_lba_le;
                    *size_out = rec->data_length_le;
                    return 0;
                }
            }

            off += rec->length;
        }
    }

    return -1;
}

int cdfs_read_file_chunk(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t length) {
    if (!cd_found) return -1;

    static uint8_t sec_buf[2048];
    uint32_t sec   = offset / 2048;
    uint32_t soff  = offset % 2048;
    uint32_t done  = 0;

    while (done < length) {
        if (atapi_read_sector(lba + sec, sec_buf) != 0)
            return -1;

        uint32_t chunk = 2048 - soff;
        if (chunk > length - done) chunk = length - done;

        for (uint32_t i = 0; i < chunk; i++)
            buffer[done + i] = sec_buf[soff + i];

        done += chunk;
        soff = 0;
        sec++;
    }

    return 0;
}
