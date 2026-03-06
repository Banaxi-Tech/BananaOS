#ifndef DISK_H
#define DISK_H

#include <stdint.h>

void disk_read_sector(uint8_t drive, uint32_t lba, uint8_t* buffer);
void disk_write_sector(uint8_t drive, uint32_t lba, const uint8_t* buffer);
void disk_write_sectors(uint8_t drive, uint32_t lba, uint32_t count, const uint8_t* buffer);
void disk_flush(uint8_t drive);
int disk_drive_exists(uint8_t drive);
void ata_read_sector(uint8_t drive, uint32_t lba, uint8_t* buffer);
void ata_write_sector(uint8_t drive, uint32_t lba, const uint8_t* buffer);
void ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const uint8_t* buffer);
void ata_flush(uint8_t drive);
int ata_drive_exists(uint8_t drive);

#endif
