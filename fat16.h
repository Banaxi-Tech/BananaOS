#ifndef FAT16_H
#define FAT16_H

#include <stdint.h>

typedef struct {
    char name[11]; // 8.3 format
    uint8_t attr;
    uint8_t reserved;
    uint8_t creation_time_ms;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t first_cluster_hi;
    uint16_t last_mod_time;
    uint16_t last_mod_date;
    uint16_t first_cluster_lo;
    uint32_t size;
} __attribute__((packed)) FAT16Entry;

typedef struct {
    uint8_t jmp[3];
    char oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t root_entries;
    uint16_t total_sectors_small;
    uint8_t media_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_large;
    uint8_t drive_num;
    uint8_t reserved;
    uint8_t boot_sig;
    uint32_t vol_id;
    char vol_label[11];
    char fs_type[8];
} __attribute__((packed)) FAT16BPB;

void fat16_init(uint8_t drive);
int fat16_list_root(FAT16Entry* entries, int max);
void fat16_read_file(FAT16Entry* entry, uint8_t* buffer);

#endif
