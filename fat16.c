#include "fat16.h"
#include "disk.h"
#include <stddef.h>

static FAT16BPB bpb;
static uint32_t volume_start_lba = 0;
static uint32_t root_dir_start_sector;
static uint32_t root_dir_sectors;
static uint32_t data_start_sector;

static uint8_t current_drive = 0;

void fat16_init(uint8_t drive) {
    current_drive = drive;
    uint8_t buf[512];
    volume_start_lba = 0;
    
    disk_read_sector(current_drive, 0, buf);
    
    // Check if Sector 0 is an MBR or a BPB
    // BPB starts with 0xEB or 0xE9
    if (buf[0] != 0xEB && buf[0] != 0xE9) {
        // Potential MBR, look for partition 1 at offset 446
        uint32_t partition_start = *(uint32_t*)&buf[446 + 8];
        if (partition_start != 0) {
            volume_start_lba = partition_start;
            disk_read_sector(current_drive, volume_start_lba, buf);
        }
    }

    FAT16BPB* ptr = (FAT16BPB*)buf;
    bpb = *ptr;

    root_dir_start_sector = volume_start_lba + bpb.reserved_sectors + (bpb.fat_count * bpb.sectors_per_fat);
    root_dir_sectors = (bpb.root_entries * 32) / 512;
    data_start_sector = root_dir_start_sector + root_dir_sectors;
}

int fat16_list_root(FAT16Entry* entries_out, int max) {
    uint8_t buf[512];
    int count = 0;

    for (uint32_t s = 0; s < root_dir_sectors; s++) {
        disk_read_sector(current_drive, root_dir_start_sector + s, buf);
        FAT16Entry* entries = (FAT16Entry*)buf;

        for (int i = 0; i < 16; i++) {
            if (entries[i].name[0] == 0x00) return count;
            if (entries[i].name[0] == (char)0xE5) continue;
            if (entries[i].attr == 0x0F) continue; // Skip LFN

            entries_out[count] = entries[i];
            count++;
            if (count >= max) return count;
        }
    }
    return count;
}

uint16_t fat16_get_fat_entry(uint16_t cluster) {
    uint8_t buf[512];
    uint32_t fat_offset = cluster * 2;
    uint32_t fat_sector = volume_start_lba + bpb.reserved_sectors + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    disk_read_sector(current_drive, fat_sector, buf);
    return *(uint16_t*)&buf[ent_offset];
}

void fat16_read_file(FAT16Entry* entry, uint8_t* buffer) {
    uint16_t cluster = entry->first_cluster_lo;
    uint32_t bytes_remaining = entry->size;
    uint32_t buffer_offset = 0;
    uint8_t sector_buf[512];

    while (cluster >= 2 && cluster < 0xFFF8) {
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
        
        cluster = fat16_get_fat_entry(cluster);
    }
}