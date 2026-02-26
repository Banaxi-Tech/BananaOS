#ifndef DISK_H
#define DISK_H

#include <stdint.h>

void disk_read_sector(uint8_t drive, uint32_t lba, uint8_t* buffer);
int disk_drive_exists(uint8_t drive);
void ata_read_sector(uint8_t drive, uint32_t lba, uint8_t* buffer);
int ata_drive_exists(uint8_t drive);

#endif
