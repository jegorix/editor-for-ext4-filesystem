#ifndef EXT4_DIR_H
#define EXT4_DIR_H

#include <stddef.h>
#include <stdint.h>

#include "ext4_inode.h"

typedef struct {
    uint32_t inode;
    uint8_t file_type;
    uint16_t rec_len;
    char name[256];
} DirEntryView;

int ext4_list_dir_entries(const Ext4Context *ctx, const Ext4SuperView *super, uint32_t dir_inode_no,
                          DirEntryView *entries, size_t max_entries, size_t *entry_count,
                          char *err, size_t err_size);
int ext4_lookup_path(const Ext4Context *ctx, const Ext4SuperView *super, const char *path,
                     uint32_t *out_inode, char *err, size_t err_size);
int ext4_find_by_inode(const Ext4Context *ctx, const Ext4SuperView *super, uint32_t target_inode,
                       char *out_path, size_t out_path_size, char *err, size_t err_size);
int ext4_find_by_name(const Ext4Context *ctx, const Ext4SuperView *super, const char *name,
                      uint32_t *out_inode, char *out_path, size_t out_path_size,
                      char *err, size_t err_size);

#endif
