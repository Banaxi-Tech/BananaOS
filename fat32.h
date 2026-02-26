#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include "fat16.h" // Reuse entry structure if possible, but FAT32 has slight differences

typedef struct {
    char name[11];
    uint8_t attr;
    uint8_t reserved;
    uint8_t creation_time_ms;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t first_cluster_hi; // Used in FAT32
    uint16_t last_mod_time;
    uint16_t last_mod_date;
    uint16_t first_cluster_lo;
    uint32_t size;
} __attribute__((packed)) FAT32Entry;

typedef struct {
    uint8_t jmp[3];
    char oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t root_entries; // 0 for FAT32
    uint16_t total_sectors_small; // 0 for FAT32
    uint8_t media_type;
    uint16_t sectors_per_fat_16; // 0 for FAT32
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_large;
    
    // FAT32 Extended BPB
    uint32_t sectors_per_fat_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t reserved_ext[12];
    uint8_t drive_num;
    uint8_t reserved_nt;
    uint8_t boot_sig;
    uint32_t vol_id;
    char vol_label[11];
    char fs_type[8];
} __attribute__((packed)) FAT32BPB;

int fat32_init(uint8_t drive);
int fat32_list_root(FAT32Entry* entries, int max);
void fat32_read_file(FAT32Entry* entry, uint8_t* buffer);

#endif
