#include "ext4_super.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "util.h"

#define EXT4_RO_COMPAT_SPARSE_SUPER 0x0001U
#define EXT4_RO_COMPAT_METADATA_CSUM 0x0400U
#define EXT4_INCOMPAT_64BIT 0x0080U

static void set_err(char *err, size_t err_size, const char *fmt, ...) {
    va_list ap;

    if (!err || err_size == 0 || !fmt) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(err, err_size, fmt, ap);
    va_end(ap);
}

static int parse_super(const unsigned char *raw, uint64_t offset, Ext4SuperView *out,
                       char *err, size_t err_size) {
    uint32_t log_block_size;
    uint32_t blocks_lo;
    uint32_t blocks_hi;

    memset(out, 0, sizeof(*out));
    out->offset = offset;

    out->magic = read_le16(raw + 0x38);
    if (out->magic != EXT4_SUPER_MAGIC) {
        set_err(err, err_size, "invalid superblock magic 0x%04x at off=%llu",
                out->magic, (unsigned long long)offset);
        return -1;
    }

    out->inodes_count = read_le32(raw + 0x00);
    blocks_lo = read_le32(raw + 0x04);
    out->first_data_block = read_le32(raw + 0x14);
    log_block_size = read_le32(raw + 0x18);
    out->blocks_per_group = read_le32(raw + 0x20);
    out->inodes_per_group = read_le32(raw + 0x28);
    out->mount_count = read_le16(raw + 0x34);
    out->max_mount_count = (int16_t)read_le16(raw + 0x36);
    out->last_check = read_le32(raw + 0x40);
    out->check_interval = read_le32(raw + 0x44);
    out->feature_compat = read_le32(raw + 0x5c);
    out->feature_incompat = read_le32(raw + 0x60);
    out->feature_ro_compat = read_le32(raw + 0x64);
    out->first_inode = read_le32(raw + 0x54);
    out->inode_size = read_le16(raw + 0x58);
    out->desc_size = read_le16(raw + 0xfe);
    memcpy(out->volume_name, raw + 0x78, 16);
    out->volume_name[16] = '\0';

    if (out->inode_size == 0) {
        out->inode_size = 128;
    }
    if (out->desc_size == 0) {
        out->desc_size = 32;
    }

    if (log_block_size > 6) {
        set_err(err, err_size, "unsupported log_block_size=%u", log_block_size);
        return -1;
    }

    out->block_size = 1024U << log_block_size;
    out->is_64bit = (out->feature_incompat & EXT4_INCOMPAT_64BIT) != 0;
    if (out->is_64bit) {
        blocks_hi = read_le32(raw + 0x150);
        out->blocks_count = (uint64_t)blocks_lo | ((uint64_t)blocks_hi << 32);
    } else {
        out->blocks_count = blocks_lo;
    }

    if (out->blocks_per_group == 0 || out->inodes_per_group == 0) {
        set_err(err, err_size, "superblock has zero blocks_per_group or inodes_per_group");
        return -1;
    }

    out->sparse_super = (out->feature_ro_compat & EXT4_RO_COMPAT_SPARSE_SUPER) != 0;
    out->metadata_csum = (out->feature_ro_compat & EXT4_RO_COMPAT_METADATA_CSUM) != 0;
    out->valid = true;
    return 0;
}

int ext4_read_superblock(const Ext4Context *ctx, uint64_t offset, Ext4SuperView *out,
                         char *err, size_t err_size) {
    unsigned char raw[EXT4_SUPERBLOCK_SIZE];

    if (!ctx || !out) {
        set_err(err, err_size, "invalid parameters");
        return -1;
    }

    if (ext4_read_bytes(ctx, offset, raw, sizeof(raw), err, err_size) != 0) {
        return -1;
    }

    return parse_super(raw, offset, out, err, err_size);
}

int ext4_read_primary_super(const Ext4Context *ctx, Ext4SuperView *out, char *err, size_t err_size) {
    return ext4_read_superblock(ctx, EXT4_SUPERBLOCK_OFFSET, out, err, err_size);
}

int ext4_find_any_valid_super(const Ext4Context *ctx, Ext4SuperView *out, char *err, size_t err_size) {
    uint64_t limit;
    uint64_t offset;
    char local_err[128];

    if (!ctx || !out) {
        set_err(err, err_size, "invalid parameters");
        return -1;
    }

    limit = ctx->image_size;
    if (limit > (128ULL << 20)) {
        limit = (128ULL << 20);
    }

    for (offset = EXT4_SUPERBLOCK_OFFSET + 1024; offset + EXT4_SUPERBLOCK_SIZE <= limit; offset += 1024) {
        if (ext4_read_superblock(ctx, offset, out, local_err, sizeof(local_err)) == 0) {
            return 0;
        }
    }

    set_err(err, err_size, "no valid superblock found in scan range");
    return -1;
}

uint32_t ext4_group_count(const Ext4SuperView *super) {
    uint64_t data_blocks;

    if (!super || super->blocks_per_group == 0 || super->blocks_count == 0) {
        return 0;
    }

    data_blocks = super->blocks_count;
    if (data_blocks > super->first_data_block) {
        data_blocks -= super->first_data_block;
    }

    return (uint32_t)((data_blocks + super->blocks_per_group - 1) / super->blocks_per_group);
}

int ext4_read_group_desc(const Ext4Context *ctx, const Ext4SuperView *super, uint32_t group_index,
                         Ext4GroupDescView *out, char *err, size_t err_size) {
    unsigned char raw[256];
    uint64_t desc_table_block;
    uint64_t desc_offset;
    uint32_t groups;
    uint32_t inode_table_lo;
    uint32_t inode_table_hi;
    uint16_t fb_lo;
    uint16_t fi_lo;
    uint16_t ud_lo;
    uint16_t fb_hi = 0;
    uint16_t fi_hi = 0;
    uint16_t ud_hi = 0;

    if (!ctx || !super || !out) {
        set_err(err, err_size, "invalid parameters");
        return -1;
    }

    groups = ext4_group_count(super);
    if (group_index >= groups) {
        set_err(err, err_size, "group index %u out of range (%u)", group_index, groups);
        return -1;
    }

    if (super->desc_size > sizeof(raw) || super->desc_size < 32) {
        set_err(err, err_size, "unsupported descriptor size %u", super->desc_size);
        return -1;
    }

    desc_table_block = (super->block_size == 1024U) ? 2ULL : 1ULL;
    desc_offset = desc_table_block * super->block_size + (uint64_t)group_index * super->desc_size;

    if (ext4_read_bytes(ctx, desc_offset, raw, super->desc_size, err, err_size) != 0) {
        return -1;
    }

    inode_table_lo = read_le32(raw + 8);
    fb_lo = read_le16(raw + 12);
    fi_lo = read_le16(raw + 14);
    ud_lo = read_le16(raw + 16);

    inode_table_hi = 0;
    if (super->is_64bit && super->desc_size >= 64) {
        inode_table_hi = read_le32(raw + 40);
        fb_hi = read_le16(raw + 44);
        fi_hi = read_le16(raw + 46);
        ud_hi = read_le16(raw + 48);
    }

    memset(out, 0, sizeof(*out));
    out->group_index = group_index;
    out->offset = desc_offset;
    out->inode_table_block = (uint64_t)inode_table_lo | ((uint64_t)inode_table_hi << 32);
    out->free_blocks_count = (uint32_t)fb_lo | ((uint32_t)fb_hi << 16);
    out->free_inodes_count = (uint32_t)fi_lo | ((uint32_t)fi_hi << 16);
    out->used_dirs_count = (uint32_t)ud_lo | ((uint32_t)ud_hi << 16);

    if (out->inode_table_block == 0) {
        set_err(err, err_size, "group %u has zero inode table pointer", group_index);
        return -1;
    }

    return 0;
}

static bool is_pow_of(uint32_t value, uint32_t base) {
    if (value < 1 || base < 2) {
        return false;
    }

    while (value > 1) {
        if (value % base != 0) {
            return false;
        }
        value /= base;
    }

    return true;
}

size_t ext4_collect_backup_super_offsets(const Ext4SuperView *super, uint64_t image_size,
                                         uint64_t *offsets, size_t max_offsets) {
    uint32_t groups;
    uint32_t g;
    size_t used = 0;

    if (!super || !offsets || max_offsets == 0) {
        return 0;
    }

    groups = ext4_group_count(super);
    for (g = 1; g < groups && used < max_offsets; g++) {
        bool keep = true;
        uint64_t offset;

        if (super->sparse_super) {
            keep = (g == 1) || is_pow_of(g, 3) || is_pow_of(g, 5) || is_pow_of(g, 7);
        }

        if (!keep) {
            continue;
        }

        offset = (uint64_t)g * super->blocks_per_group * super->block_size + 1024ULL;
        if (offset + EXT4_SUPERBLOCK_SIZE > image_size) {
            continue;
        }

        offsets[used++] = offset;
    }

    return used;
}

int ext4_check_feature_support(const Ext4SuperView *super, bool *requires_readonly,
                               char *warn, size_t warn_size) {
    uint32_t unsupported;
    const uint32_t supported_incompat =
        0x0002U | /* FILETYPE */
        0x0004U | /* RECOVER */
        0x0010U | /* META_BG */
        0x0040U | /* EXTENTS */
        0x0080U | /* 64BIT */
        0x0200U | /* FLEX_BG */
        0x0400U | /* EA_INODE */
        0x1000U | /* DIRDATA */
        0x2000U | /* CSUM_SEED */
        0x4000U | /* LARGEDIR */
        0x8000U | /* INLINE_DATA */
        0x10000U | /* ENCRYPT */
        0x20000U;  /* CASEFOLD */

    if (!super || !requires_readonly) {
        return -1;
    }

    *requires_readonly = false;
    unsupported = super->feature_incompat & ~supported_incompat;

    if (unsupported != 0U) {
        *requires_readonly = true;
        if (warn && warn_size > 0) {
            snprintf(warn, warn_size,
                     "unknown incompat feature bits 0x%08x, forcing readonly mode",
                     unsupported);
        }
    } else if (warn && warn_size > 0) {
        warn[0] = '\0';
    }

    if (super->block_size != 1024U && super->block_size != 2048U && super->block_size != 4096U) {
        *requires_readonly = true;
        if (warn && warn_size > 0) {
            snprintf(warn, warn_size,
                     "block size %u is outside tested range, forcing readonly mode",
                     super->block_size);
        }
    }

    return 0;
}
