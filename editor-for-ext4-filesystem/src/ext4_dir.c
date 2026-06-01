#include "ext4_dir.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#define ROOT_INODE 2U
#define DIRECT_BLOCKS 12U
#define MAX_SCAN_ENTRIES 16384U
#define MAX_DFS_DEPTH 64
#define MAX_DIR_BLOCKS 65536U
#define MAX_EXTENT_DEPTH 5
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

static int append_extent_blocks(uint64_t start_block, uint32_t len,
                                uint64_t *blocks, size_t max_blocks, size_t *count,
                                char *err, size_t err_size) {
    uint32_t i;

    for (i = 0; i < len; i++) {
        if (*count >= max_blocks) {
            set_err(err, err_size, "too many blocks in extent tree");
            return -1;
        }
        blocks[(*count)++] = start_block + i;
    }
    return 0;
}

static int collect_extent_blocks(const Ext4Context *ctx, const Ext4SuperView *super,
                                 const unsigned char *node, size_t node_size, int level,
                                 uint64_t *blocks, size_t max_blocks, size_t *count,
                                 char *err, size_t err_size) {
    uint16_t magic;
    uint16_t entries;
    uint16_t depth;
    uint16_t max_entries;
    uint16_t i;

    if (!node || node_size < 12) {
        set_err(err, err_size, "invalid extent node");
        return -1;
    }

    if (level > MAX_EXTENT_DEPTH) {
        set_err(err, err_size, "extent tree depth limit exceeded");
        return -1;
    }

    magic = read_le16(node + 0);
    entries = read_le16(node + 2);
    max_entries = read_le16(node + 4);
    depth = read_le16(node + 6);

    if (magic != EXT4_EXTENT_MAGIC) {
        set_err(err, err_size, "invalid extent magic 0x%04x", magic);
        return -1;
    }

    if (entries > max_entries || 12ULL + (uint64_t)entries * 12ULL > node_size) {
        set_err(err, err_size, "corrupted extent header");
        return -1;
    }

    if (depth == 0) {
        for (i = 0; i < entries; i++) {
            size_t off = 12 + (size_t)i * 12;
            uint16_t ee_len = read_le16(node + off + 4);
            uint16_t ee_start_hi = read_le16(node + off + 6);
            uint32_t ee_start_lo = read_le32(node + off + 8);
            uint32_t len = ee_len & 0x7fffU;
            uint64_t start_block = ((uint64_t)ee_start_hi << 32) | ee_start_lo;

            if (len == 0) {
                continue;
            }

            if (append_extent_blocks(start_block, len, blocks, max_blocks, count, err, err_size) != 0) {
                return -1;
            }
        }
    } else {
        unsigned char *child = calloc(1, super->block_size);
        if (!child) {
            set_err(err, err_size, "out of memory");
            return -1;
        }

        for (i = 0; i < entries; i++) {
            size_t off = 12 + (size_t)i * 12;
            uint32_t leaf_lo = read_le32(node + off + 4);
            uint16_t leaf_hi = read_le16(node + off + 8);
            uint64_t leaf_block = ((uint64_t)leaf_hi << 32) | leaf_lo;
            uint64_t leaf_off = leaf_block * super->block_size;

            if (leaf_block == 0) {
                continue;
            }

            if (ext4_read_bytes(ctx, leaf_off, child, super->block_size, err, err_size) != 0) {
                free(child);
                return -1;
            }

            if (collect_extent_blocks(ctx, super, child, super->block_size, level + 1,
                                      blocks, max_blocks, count, err, err_size) != 0) {
                free(child);
                return -1;
            }
        }

        free(child);
    }

    return 0;
}

static int collect_dir_blocks(const Ext4Context *ctx, const Ext4SuperView *super,
                              const Ext4InodeView *inode, uint64_t *blocks,
                              size_t max_blocks, size_t *count,
                              char *err, size_t err_size) {
    size_t i;

    *count = 0;

    if (inode->uses_extents) {
        return collect_extent_blocks(ctx, super, inode->block_raw, sizeof(inode->block_raw), 0,
                                     blocks, max_blocks, count, err, err_size);
    }

    for (i = 0; i < DIRECT_BLOCKS; i++) {
        if (inode->block[i] == 0) {
            continue;
        }
        if (*count >= max_blocks) {
            set_err(err, err_size, "too many direct blocks");
            return -1;
        }
        blocks[(*count)++] = inode->block[i];
    }

    return 0;
}

int ext4_list_dir_entries(const Ext4Context *ctx, const Ext4SuperView *super, uint32_t dir_inode_no,
                          DirEntryView *entries, size_t max_entries, size_t *entry_count,
                          char *err, size_t err_size) {
    Ext4InodeView inode;
    unsigned char *block;
    uint64_t *dir_blocks;
    size_t dir_block_count = 0;
    size_t out = 0;
    size_t i;

    if (!ctx || !super || !entries || max_entries == 0 || !entry_count) {
        set_err(err, err_size, "invalid parameters");
        return -1;
    }

    if (ext4_read_inode(ctx, super, dir_inode_no, &inode, err, err_size) != 0) {
        return -1;
    }

    if (!inode.is_directory) {
        set_err(err, err_size, "inode %u is not a directory", dir_inode_no);
        return -1;
    }

    block = calloc(1, super->block_size);
    if (!block) {
        set_err(err, err_size, "out of memory");
        return -1;
    }

    dir_blocks = calloc(MAX_DIR_BLOCKS, sizeof(*dir_blocks));
    if (!dir_blocks) {
        free(block);
        set_err(err, err_size, "out of memory");
        return -1;
    }

    if (collect_dir_blocks(ctx, super, &inode, dir_blocks, MAX_DIR_BLOCKS, &dir_block_count,
                           err, err_size) != 0) {
        free(dir_blocks);
        free(block);
        return -1;
    }

    for (i = 0; i < dir_block_count; i++) {
        uint64_t blk = dir_blocks[i];
        size_t pos = 0;

        if (blk == 0) {
            continue;
        }

        if (ext4_read_bytes(ctx, blk * super->block_size, block, super->block_size,
                            err, err_size) != 0) {
            free(dir_blocks);
            free(block);
            return -1;
        }

        while (pos + 8 <= super->block_size) {
            uint32_t ent_inode = read_le32(block + pos);
            uint16_t rec_len = read_le16(block + pos + 4);
            uint8_t name_len = block[pos + 6];
            uint8_t file_type = block[pos + 7];
            size_t copy_len;

            if (rec_len < 8 || rec_len % 4 != 0 || pos + rec_len > super->block_size) {
                break;
            }

            if (ent_inode != 0) {
                if (out >= max_entries) {
                    set_err(err, err_size, "directory has more than %zu entries", max_entries);
                    free(dir_blocks);
                    free(block);
                    return -1;
                }

                entries[out].inode = ent_inode;
                entries[out].file_type = file_type;
                entries[out].rec_len = rec_len;
                copy_len = name_len;
                if (copy_len > sizeof(entries[out].name) - 1) {
                    copy_len = sizeof(entries[out].name) - 1;
                }
                memcpy(entries[out].name, block + pos + 8, copy_len);
                entries[out].name[copy_len] = '\0';
                out++;
            }

            pos += rec_len;
        }
    }

    free(dir_blocks);
    free(block);
    *entry_count = out;
    return 0;
}

static int lookup_child(const Ext4Context *ctx, const Ext4SuperView *super, uint32_t dir_inode,
                        const char *name, uint32_t *out_inode, char *err, size_t err_size) {
    DirEntryView *entries = NULL;
    size_t entry_count = 0;
    size_t i;

    entries = calloc(MAX_SCAN_ENTRIES, sizeof(*entries));
    if (!entries) {
        set_err(err, err_size, "out of memory");
        return -1;
    }

    if (ext4_list_dir_entries(ctx, super, dir_inode, entries, MAX_SCAN_ENTRIES,
                              &entry_count, err, err_size) != 0) {
        free(entries);
        return -1;
    }

    for (i = 0; i < entry_count; i++) {
        if (strcmp(entries[i].name, name) == 0) {
            *out_inode = entries[i].inode;
            free(entries);
            return 0;
        }
    }

    free(entries);
    set_err(err, err_size, "component '%s' not found", name);
    return -1;
}

int ext4_lookup_path(const Ext4Context *ctx, const Ext4SuperView *super, const char *path,
                     uint32_t *out_inode, char *err, size_t err_size) {
    char *copy;
    char *token;
    char *saveptr = NULL;
    uint32_t current = ROOT_INODE;

    if (!ctx || !super || !path || !out_inode) {
        set_err(err, err_size, "invalid parameters");
        return -1;
    }

    if (path[0] != '/') {
        set_err(err, err_size, "path must be absolute");
        return -1;
    }

    if (strcmp(path, "/") == 0) {
        *out_inode = ROOT_INODE;
        return 0;
    }

    copy = strdup(path);
    if (!copy) {
        set_err(err, err_size, "out of memory");
        return -1;
    }

    token = strtok_r(copy, "/", &saveptr);
    while (token) {
        if (lookup_child(ctx, super, current, token, &current, err, err_size) != 0) {
            free(copy);
            return -1;
        }
        token = strtok_r(NULL, "/", &saveptr);
    }

    free(copy);
    *out_inode = current;
    return 0;
}

static int should_skip_entry(const DirEntryView *entry) {
    return strcmp(entry->name, ".") == 0 || strcmp(entry->name, "..") == 0;
}

static int append_path(char *dst, size_t dst_size, const char *base, const char *name) {
    if (strcmp(base, "/") == 0) {
        return snprintf(dst, dst_size, "/%s", name) >= (int)dst_size ? -1 : 0;
    }

    return snprintf(dst, dst_size, "%s/%s", base, name) >= (int)dst_size ? -1 : 0;
}

static int dfs_find_inode(const Ext4Context *ctx, const Ext4SuperView *super,
                          uint32_t current_inode, const char *current_path,
                          uint32_t target_inode, char *out_path, size_t out_path_size,
                          int depth, char *err, size_t err_size) {
    DirEntryView *entries = NULL;
    size_t entry_count = 0;
    size_t i;

    if (depth > MAX_DFS_DEPTH) {
        set_err(err, err_size, "directory traversal depth limit reached");
        return -1;
    }

    entries = calloc(MAX_SCAN_ENTRIES, sizeof(*entries));
    if (!entries) {
        set_err(err, err_size, "out of memory");
        return -1;
    }

    if (ext4_list_dir_entries(ctx, super, current_inode, entries, MAX_SCAN_ENTRIES,
                              &entry_count, err, err_size) != 0) {
        free(entries);
        return -1;
    }

    for (i = 0; i < entry_count; i++) {
        char next_path[1024];
        Ext4InodeView inode;

        if (should_skip_entry(&entries[i])) {
            continue;
        }

        if (append_path(next_path, sizeof(next_path), current_path, entries[i].name) != 0) {
            continue;
        }

        if (entries[i].inode == target_inode) {
            snprintf(out_path, out_path_size, "%s", next_path);
            free(entries);
            return 0;
        }

        if (ext4_read_inode(ctx, super, entries[i].inode, &inode, err, err_size) != 0) {
            continue;
        }

        if (inode.is_directory) {
            if (dfs_find_inode(ctx, super, entries[i].inode, next_path, target_inode,
                               out_path, out_path_size, depth + 1, err, err_size) == 0) {
                free(entries);
                return 0;
            }
        }
    }

    free(entries);
    set_err(err, err_size, "inode %u not found in tree", target_inode);
    return -1;
}

int ext4_find_by_inode(const Ext4Context *ctx, const Ext4SuperView *super, uint32_t target_inode,
                       char *out_path, size_t out_path_size, char *err, size_t err_size) {
    if (!ctx || !super || !out_path || out_path_size == 0) {
        set_err(err, err_size, "invalid parameters");
        return -1;
    }

    if (target_inode == ROOT_INODE) {
        snprintf(out_path, out_path_size, "/");
        return 0;
    }

    return dfs_find_inode(ctx, super, ROOT_INODE, "/", target_inode,
                          out_path, out_path_size, 0, err, err_size);
}

static int dfs_find_name(const Ext4Context *ctx, const Ext4SuperView *super,
                         uint32_t current_inode, const char *current_path,
                         const char *target_name, uint32_t *out_inode,
                         char *out_path, size_t out_path_size,
                         int depth, char *err, size_t err_size) {
    DirEntryView *entries = NULL;
    size_t entry_count = 0;
    size_t i;

    if (depth > MAX_DFS_DEPTH) {
        set_err(err, err_size, "directory traversal depth limit reached");
        return -1;
    }

    entries = calloc(MAX_SCAN_ENTRIES, sizeof(*entries));
    if (!entries) {
        set_err(err, err_size, "out of memory");
        return -1;
    }

    if (ext4_list_dir_entries(ctx, super, current_inode, entries, MAX_SCAN_ENTRIES,
                              &entry_count, err, err_size) != 0) {
        free(entries);
        return -1;
    }

    for (i = 0; i < entry_count; i++) {
        char next_path[1024];
        Ext4InodeView inode;

        if (should_skip_entry(&entries[i])) {
            continue;
        }

        if (append_path(next_path, sizeof(next_path), current_path, entries[i].name) != 0) {
            continue;
        }

        if (strcmp(entries[i].name, target_name) == 0) {
            *out_inode = entries[i].inode;
            snprintf(out_path, out_path_size, "%s", next_path);
            free(entries);
            return 0;
        }

        if (ext4_read_inode(ctx, super, entries[i].inode, &inode, err, err_size) != 0) {
            continue;
        }

        if (inode.is_directory) {
            if (dfs_find_name(ctx, super, entries[i].inode, next_path, target_name,
                              out_inode, out_path, out_path_size, depth + 1,
                              err, err_size) == 0) {
                free(entries);
                return 0;
            }
        }
    }

    free(entries);
    set_err(err, err_size, "name '%s' not found", target_name);
    return -1;
}

int ext4_find_by_name(const Ext4Context *ctx, const Ext4SuperView *super, const char *name,
                      uint32_t *out_inode, char *out_path, size_t out_path_size,
                      char *err, size_t err_size) {
    if (!ctx || !super || !name || !*name || !out_inode || !out_path || out_path_size == 0) {
        set_err(err, err_size, "invalid parameters");
        return -1;
    }

    return dfs_find_name(ctx, super, ROOT_INODE, "/", name, out_inode,
                         out_path, out_path_size, 0, err, err_size);
}
