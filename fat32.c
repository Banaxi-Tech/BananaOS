#include "fat32.h"
#include "disk.h"
#include <stddef.h>

static FAT32BPB bpb;
static uint32_t volume_start_lba = 0;
static uint32_t fat_start_sector;
static uint32_t data_start_sector;

static uint8_t current_drive = 0;

int fat32_init(uint8_t drive) {
    current_drive = drive;
    uint8_t buf[512];
    volume_start_lba = 0;

    disk_read_sector(current_drive, 0, buf);

    // Check if Sector 0 is an MBR
    if (buf[0] != 0xEB && buf[0] != 0xE9) {
        uint32_t partition_start = *(uint32_t*)&buf[446 + 8];
        if (partition_start != 0) {
            volume_start_lba = partition_start;
            disk_read_sector(current_drive, volume_start_lba, buf);
        }
    }

    FAT32BPB* ptr = (FAT32BPB*)buf;
    
    // Quick check if it's actually FAT32
    if (ptr->sectors_per_fat_16 != 0) return 0; 
    
    bpb = *ptr;
    fat_start_sector = volume_start_lba + bpb.reserved_sectors;
    data_start_sector = volume_start_lba + bpb.reserved_sectors + (bpb.fat_count * bpb.sectors_per_fat_32);
    return 1;
}

uint32_t fat32_get_fat_entry(uint32_t cluster) {
    uint8_t buf[512];
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start_sector + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    disk_read_sector(current_drive, fat_sector, buf);
    return (*(uint32_t*)&buf[ent_offset]) & 0x0FFFFFFF;
}

int fat32_list_root(FAT32Entry* entries_out, int max) {
    uint8_t buf[512];
    int count = 0;
    uint32_t cluster = bpb.root_cluster;

    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32_t lba = data_start_sector + ((cluster - 2) * bpb.sectors_per_cluster);
        
        for (int s = 0; s < bpb.sectors_per_cluster; s++) {
            disk_read_sector(current_drive, lba + s, buf);
            FAT32Entry* entries = (FAT32Entry*)buf;

            for (int i = 0; i < 16; i++) {
                if (entries[i].name[0] == 0x00) return count;
                if (entries[i].name[0] == (char)0xE5) continue;
                if (entries[i].attr == 0x0F) continue; // Skip LFN

                entries_out[count] = entries[i];
                count++;
                if (count >= max) return count;
            }
        }
        cluster = fat32_get_fat_entry(cluster);
    }
    return count;
}

void fat32_read_file(FAT32Entry* entry, uint8_t* buffer) {
    uint32_t cluster = ((uint32_t)entry->first_cluster_hi << 16) | entry->first_cluster_lo;
    uint32_t bytes_remaining = entry->size;
    uint32_t buffer_offset = 0;
    uint8_t sector_buf[512];

    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32_t lba = data_start_sector + ((cluster - 2) * bpb.sectors_per_cluster);
        
        for (int s = 0; s < bpb.sectors_per_cluster; s++) {
            disk_read_sector(current_drive, lba + s, sector_buf);
            
            uint32_t to_copy = (bytes_remaining > 512) ? 512 : bytes_remaining;
            for (uint32_t i = 0; i < to_copy; i++) {
                buffer[buffer_offset++] = sector_buf[i];
            }
            bytes_remaining -= to_copy;
            if (bytes_remaining == 0) return;
        }
        
        cluster = fat32_get_fat_entry(cluster);
    }
}
