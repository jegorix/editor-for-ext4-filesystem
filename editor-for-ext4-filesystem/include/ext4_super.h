#ifndef EXT4_SUPER_H
#define EXT4_SUPER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ext4_io.h"

#define EXT4_SUPERBLOCK_OFFSET 1024ULL
#define EXT4_SUPERBLOCK_SIZE 1024U
#define EXT4_SUPER_MAGIC 0xEF53U

typedef struct {
    bool valid;
    uint64_t offset;
    uint16_t magic;
    uint32_t block_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t inode_size;
    uint32_t desc_size;
    uint32_t inodes_count;
    uint64_t blocks_count;
    uint32_t first_data_block;
    uint32_t first_inode;
    uint16_t mount_count;
    int16_t max_mount_count;
    uint32_t last_check;
    uint32_t check_interval;
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;
    bool sparse_super;
    bool is_64bit;
    bool metadata_csum;
    char volume_name[17];
} Ext4SuperView;

typedef struct {
    uint32_t group_index;
    uint64_t offset;
    uint64_t inode_table_block;
    uint32_t free_blocks_count;
    uint32_t free_inodes_count;
    uint32_t used_dirs_count;
} Ext4GroupDescView;

int ext4_read_superblock(const Ext4Context *ctx, uint64_t offset, Ext4SuperView *out, char *err, size_t err_size);
int ext4_read_primary_super(const Ext4Context *ctx, Ext4SuperView *out, char *err, size_t err_size);
int ext4_find_any_valid_super(const Ext4Context *ctx, Ext4SuperView *out, char *err, size_t err_size);

uint32_t ext4_group_count(const Ext4SuperView *super);
int ext4_read_group_desc(const Ext4Context *ctx, const Ext4SuperView *super, uint32_t group_index,
                         Ext4GroupDescView *out, char *err, size_t err_size);
size_t ext4_collect_backup_super_offsets(const Ext4SuperView *super, uint64_t image_size,
                                         uint64_t *offsets, size_t max_offsets);
int ext4_check_feature_support(const Ext4SuperView *super, bool *requires_readonly, char *warn, size_t warn_size);

#endif
