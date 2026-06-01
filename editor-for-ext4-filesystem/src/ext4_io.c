#include "ext4_io.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static void set_err(char *err, size_t err_size, const char *fmt, ...) {
    va_list ap;

    if (!err || err_size == 0 || !fmt) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(err, err_size, fmt, ap);
    va_end(ap);
}

int ext4_open_image(Ext4Context *ctx, const char *path, bool write_enabled, char *err, size_t err_size) {
    struct stat st;
    int flags;
    int fd;

    if (!ctx || !path || !*path) {
        set_err(err, err_size, "image path is required");
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->fd = -1;
    snprintf(ctx->image_path, sizeof(ctx->image_path), "%s", path);

    flags = write_enabled ? O_RDWR : O_RDONLY;
    fd = open(path, flags);
    if (fd < 0) {
        set_err(err, err_size, "failed to open '%s': %s", path, strerror(errno));
        return -1;
    }

    if (fstat(fd, &st) != 0) {
        set_err(err, err_size, "fstat failed for '%s': %s", path, strerror(errno));
        close(fd);
        return -1;
    }

    if (!S_ISREG(st.st_mode)) {
        set_err(err, err_size, "'%s' is not a regular file image", path);
        close(fd);
        return -1;
    }

    ctx->fd = fd;
    ctx->write_enabled = write_enabled;
    ctx->image_size = (uint64_t)st.st_size;
    ctx->readonly_forced = false;

    if (ctx->image_size < 2048U) {
        set_err(err, err_size, "image is too small (%llu bytes)",
                (unsigned long long)ctx->image_size);
        ext4_close_image(ctx);
        return -1;
    }

    return 0;
}

void ext4_close_image(Ext4Context *ctx) {
    if (!ctx) {
        return;
    }

    if (ctx->fd >= 0) {
        close(ctx->fd);
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->fd = -1;
}

int ext4_read_bytes(const Ext4Context *ctx, uint64_t offset, void *buf, size_t len, char *err, size_t err_size) {
    ssize_t rc;

    if (!ctx || ctx->fd < 0 || !buf || len == 0) {
        set_err(err, err_size, "invalid read parameters");
        return -1;
    }

    if (offset > ctx->image_size || len > (size_t)(ctx->image_size - offset)) {
        set_err(err, err_size, "read out of bounds: off=%llu len=%zu size=%llu",
                (unsigned long long)offset, len, (unsigned long long)ctx->image_size);
        return -1;
    }

    rc = pread(ctx->fd, buf, len, (off_t)offset);
    if (rc < 0 || (size_t)rc != len) {
        set_err(err, err_size, "pread failed at off=%llu: %s",
                (unsigned long long)offset,
                rc < 0 ? strerror(errno) : "short read");
        return -1;
    }

    return 0;
}

int ext4_write_bytes(const Ext4Context *ctx, uint64_t offset, const void *buf, size_t len,
                    char *err, size_t err_size) {
    ssize_t rc;

    if (!ctx || ctx->fd < 0 || !buf || len == 0) {
        set_err(err, err_size, "invalid write parameters");
        return -1;
    }

    if (!ctx->write_enabled || ctx->readonly_forced) {
        set_err(err, err_size, "write mode is disabled");
        return -1;
    }

    if (offset > ctx->image_size || len > (size_t)(ctx->image_size - offset)) {
        set_err(err, err_size, "write out of bounds: off=%llu len=%zu size=%llu",
                (unsigned long long)offset, len, (unsigned long long)ctx->image_size);
        return -1;
    }

    rc = pwrite(ctx->fd, buf, len, (off_t)offset);
    if (rc < 0 || (size_t)rc != len) {
        set_err(err, err_size, "pwrite failed at off=%llu: %s",
                (unsigned long long)offset,
                rc < 0 ? strerror(errno) : "short write");
        return -1;
    }

    if (fsync(ctx->fd) != 0) {
        set_err(err, err_size, "fsync failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

int ext4_create_backup(const Ext4Context *ctx, char *backup_path,
                       size_t backup_path_size, char *err, size_t err_size) {
    int src_fd = -1;
    int dst_fd = -1;
    int attempt;
    ssize_t nread;
    char buffer[1 << 16];
    time_t now;
    struct tm tm_buf;
    char ts[32];

    if (!ctx || !backup_path || backup_path_size == 0) {
        set_err(err, err_size, "invalid backup parameters");
        return -1;
    }

    now = time(NULL);
    localtime_r(&now, &tm_buf);
    strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", &tm_buf);

    src_fd = open(ctx->image_path, O_RDONLY);
    if (src_fd < 0) {
        set_err(err, err_size, "backup source open failed: %s", strerror(errno));
        return -1;
    }

    for (attempt = 0; attempt < 100; attempt++) {
        snprintf(backup_path, backup_path_size, "%s.%s.%02d.bak", ctx->image_path, ts, attempt);
        dst_fd = open(backup_path, O_CREAT | O_EXCL | O_WRONLY, 0644);
        if (dst_fd >= 0) {
            break;
        }
        if (errno != EEXIST) {
            set_err(err, err_size, "backup destination open failed: %s", strerror(errno));
            close(src_fd);
            return -1;
        }
    }

    if (dst_fd < 0) {
        set_err(err, err_size, "failed to allocate unique backup name");
        close(src_fd);
        return -1;
    }

    while ((nread = read(src_fd, buffer, sizeof(buffer))) > 0) {
        ssize_t written = 0;
        while (written < nread) {
            ssize_t nwrite = write(dst_fd, buffer + written, (size_t)(nread - written));
            if (nwrite <= 0) {
                set_err(err, err_size, "backup write failed: %s", strerror(errno));
                close(src_fd);
                close(dst_fd);
                return -1;
            }
            written += nwrite;
        }
    }

    if (nread < 0) {
        set_err(err, err_size, "backup read failed: %s", strerror(errno));
        close(src_fd);
        close(dst_fd);
        return -1;
    }

    close(src_fd);
    if (fsync(dst_fd) != 0) {
        set_err(err, err_size, "backup fsync failed: %s", strerror(errno));
        close(dst_fd);
        return -1;
    }
    close(dst_fd);
    return 0;
}
