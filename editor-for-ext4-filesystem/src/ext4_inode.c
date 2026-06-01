#include "ext4_inode.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#define EXT4_S_IFDIR 0x4000U
#define EXT4_EXTENTS_FL 0x00080000U
#define EXT4_EXTENT_MAGIC 0xF30AU

static void set_err(char *err, size_t err_size, const char *fmt, ...) {
    va_list ap;

    if (!err || err_size == 0 || !fmt) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(err, err_size, fmt, ap);
    va_end(ap);
}

int ext4_inode_offset(const Ext4Context *ctx, const Ext4SuperView *super, uint32_t inode_no,
                      uint64_t *out_offset, char *err, size_t err_size) {
    uint32_t inode_index;
    uint32_t group;
    uint32_t index_in_group;
    Ext4GroupDescView gd;
    uint64_t offset;

    if (!ctx || !super || !out_offset) {
        set_err(err, err_size, "invalid parameters");
        return -1;
    }

    if (inode_no == 0 || inode_no > super->inodes_count) {
        set_err(err, err_size, "inode %u out of range (1..%u)", inode_no, super->inodes_count);
        return -1;
    }

    inode_index = inode_no - 1;
    group = inode_index / super->inodes_per_group;
    index_in_group = inode_index % super->inodes_per_group;

    if (ext4_read_group_desc(ctx, super, group, &gd, err, err_size) != 0) {
        return -1;
    }

    offset = gd.inode_table_block * super->block_size +
             (uint64_t)index_in_group * super->inode_size;

    if (offset + super->inode_size > ctx->image_size) {
        set_err(err, err_size, "inode %u offset out of image bounds", inode_no);
        return -1;
    }

    *out_offset = offset;
    return 0;
}

int ext4_read_inode(const Ext4Context *ctx, const Ext4SuperView *super, uint32_t inode_no,
                    Ext4InodeView *out, char *err, size_t err_size) {
    unsigned char *raw;
    uint64_t offset;
    uint32_t uid_lo;
    uint32_t uid_hi;
    uint32_t gid_lo;
    uint32_t gid_hi;
    uint32_t size_lo;
    uint32_t size_hi;
    size_t i;

    if (!ctx || !super || !out) {
        set_err(err, err_size, "invalid parameters");
        return -1;
    }

    if (ext4_inode_offset(ctx, super, inode_no, &offset, err, err_size) != 0) {
        return -1;
    }

    raw = calloc(1, super->inode_size);
    if (!raw) {
        set_err(err, err_size, "out of memory");
        return -1;
    }

    if (ext4_read_bytes(ctx, offset, raw, super->inode_size, err, err_size) != 0) {
        free(raw);
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->valid = true;
    out->inode_no = inode_no;
    out->mode = read_le16(raw + 0);
    uid_lo = read_le16(raw + 2);
    gid_lo = read_le16(raw + 24);
    uid_hi = read_le16(raw + 120);
    gid_hi = read_le16(raw + 122);
    out->uid = uid_lo | (uid_hi << 16);
    out->gid = gid_lo | (gid_hi << 16);
    size_lo = read_le32(raw + 4);
    size_hi = read_le32(raw + 108);
    out->size = (uint64_t)size_lo | ((uint64_t)size_hi << 32);
    out->atime = read_le32(raw + 8);
    out->ctime = read_le32(raw + 12);
    out->mtime = read_le32(raw + 16);
    out->dtime = read_le32(raw + 20);
    out->links_count = read_le16(raw + 26);
    out->blocks_lo = read_le32(raw + 28);
    out->flags = read_le32(raw + 32);
    memcpy(out->block_raw, raw + 40, sizeof(out->block_raw));
    out->uses_extents = (out->flags & EXT4_EXTENTS_FL) != 0 ||
                        read_le16(out->block_raw) == EXT4_EXTENT_MAGIC;
    out->is_directory = (out->mode & 0xF000U) == EXT4_S_IFDIR;

    for (i = 0; i < 15; i++) {
        out->block[i] = read_le32(raw + 40 + i * 4);
    }

    free(raw);
    return 0;
}

int ext4_write_inode_fields(const Ext4Context *ctx, const Ext4SuperView *super, uint32_t inode_no,
                            const Ext4InodeEdit *edit, char *err, size_t err_size) {
    unsigned char *raw;
    uint64_t offset;

    if (!ctx || !super || !edit) {
        set_err(err, err_size, "invalid parameters");
        return -1;
    }

    if (ext4_inode_offset(ctx, super, inode_no, &offset, err, err_size) != 0) {
        return -1;
    }

    raw = calloc(1, super->inode_size);
    if (!raw) {
        set_err(err, err_size, "out of memory");
        return -1;
    }

    if (ext4_read_bytes(ctx, offset, raw, super->inode_size, err, err_size) != 0) {
        free(raw);
        return -1;
    }

    if (edit->set_mode) {
        write_le16(raw + 0, edit->mode);
    }

    if (edit->set_uid) {
        write_le16(raw + 2, (uint16_t)(edit->uid & 0xffffU));
        write_le16(raw + 120, (uint16_t)((edit->uid >> 16) & 0xffffU));
    }

    if (edit->set_gid) {
        write_le16(raw + 24, (uint16_t)(edit->gid & 0xffffU));
        write_le16(raw + 122, (uint16_t)((edit->gid >> 16) & 0xffffU));
    }

    if (edit->set_atime) {
        write_le32(raw + 8, edit->atime);
    }

    if (edit->set_ctime) {
        write_le32(raw + 12, edit->ctime);
    }

    if (edit->set_mtime) {
        write_le32(raw + 16, edit->mtime);
    }

    if (edit->set_flags) {
        write_le32(raw + 32, edit->flags);
    }

    if (ext4_write_bytes(ctx, offset, raw, super->inode_size, err, err_size) != 0) {
        free(raw);
        return -1;
    }

    free(raw);
    return 0;
}
