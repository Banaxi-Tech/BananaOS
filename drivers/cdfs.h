#ifndef CDFS_H
#define CDFS_H

#include <stdint.h>

/* ISO 9660 Primary Volume Descriptor */
typedef struct {
    uint8_t type_code;
    char identifier[5];
    uint8_t version;
    uint8_t unused1;
    char system_id[32];
    char volume_id[32];
    uint8_t unused2[8];
    uint32_t volume_space_size_le;
    uint32_t volume_space_size_be;
    uint8_t unused3[32];
    uint16_t volume_set_size_le;
    uint16_t volume_set_size_be;
    uint16_t volume_sequence_number_le;
    uint16_t volume_sequence_number_be;
    uint16_t logical_block_size_le;
    uint16_t logical_block_size_be;
    uint32_t path_table_size_le;
    uint32_t path_table_size_be;
    uint32_t type_l_path_table_le;
    uint32_t opt_type_l_path_table_le;
    uint32_t type_m_path_table_be;
    uint32_t opt_type_m_path_table_be;
    uint8_t root_directory_record[34];
    char volume_set_id[128];
    char publisher_id[128];
    char preparer_id[128];
    char application_id[128];
    char copyright_file_id[37];
    char abstract_file_id[37];
    char bibliographic_file_id[37];
    char creation_date[17];
    char modification_date[17];
    char expiration_date[17];
    char effective_date[17];
    uint8_t file_structure_version;
    uint8_t unused4;
    uint8_t application_data[512];
    uint8_t reserved[653];
} __attribute__((packed)) ISO9660_PVD;

/* ISO 9660 Directory Record */
typedef struct {
    uint8_t length;
    uint8_t ext_attr_length;
    uint32_t extent_lba_le;
    uint32_t extent_lba_be;
    uint32_t data_length_le;
    uint32_t data_length_be;
    uint8_t recording_date[7];
    uint8_t flags;
    uint8_t file_unit_size;
    uint8_t interleave_gap_size;
    uint16_t volume_sequence_number_le;
    uint16_t volume_sequence_number_be;
    uint8_t name_length;
    char name[1]; // Variable length
} __attribute__((packed)) ISO9660_DirRecord;

/* CDFS Functions */
int cdfs_init(void);
int cdfs_find_file(const char* filename, uint32_t* lba_out, uint32_t* size_out);
int cdfs_read_file_chunk(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t length);

#endif
