#ifndef EXT4_INODE_H
#define EXT4_INODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ext4_super.h"

typedef struct {
    bool valid;
    uint32_t inode_no;
    uint16_t mode;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t dtime;
    uint16_t links_count;
    uint32_t flags;
    uint32_t blocks_lo;
    bool uses_extents;
    uint32_t block[15];
    unsigned char block_raw[60];
    bool is_directory;
} Ext4InodeView;

typedef struct {
    bool set_mode;
    uint16_t mode;
    bool set_uid;
    uint32_t uid;
    bool set_gid;
    uint32_t gid;
    bool set_atime;
    uint32_t atime;
    bool set_ctime;
    uint32_t ctime;
    bool set_mtime;
    uint32_t mtime;
    bool set_flags;
    uint32_t flags;
} Ext4InodeEdit;

int ext4_inode_offset(const Ext4Context *ctx, const Ext4SuperView *super, uint32_t inode_no,
                      uint64_t *out_offset, char *err, size_t err_size);
int ext4_read_inode(const Ext4Context *ctx, const Ext4SuperView *super, uint32_t inode_no,
                    Ext4InodeView *out, char *err, size_t err_size);
int ext4_write_inode_fields(const Ext4Context *ctx, const Ext4SuperView *super, uint32_t inode_no,
                            const Ext4InodeEdit *edit, char *err, size_t err_size);

#endif
