#ifndef EXT4_IO_H
#define EXT4_IO_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    int fd;
    uint64_t image_size;
    bool write_enabled;
    bool readonly_forced;
    char image_path[PATH_MAX];
} Ext4Context;

int ext4_open_image(Ext4Context *ctx, const char *path, bool write_enabled, char *err, size_t err_size);
void ext4_close_image(Ext4Context *ctx);
int ext4_read_bytes(const Ext4Context *ctx, uint64_t offset, void *buf, size_t len, char *err, size_t err_size);
int ext4_write_bytes(const Ext4Context *ctx, uint64_t offset, const void *buf, size_t len, char *err, size_t err_size);
int ext4_create_backup(const Ext4Context *ctx, char *backup_path, size_t backup_path_size,
                       char *err, size_t err_size);

#endif
