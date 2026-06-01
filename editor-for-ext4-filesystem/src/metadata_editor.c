#include "metadata_editor.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ext4_io.h"
#include "util.h"

static void set_err(char *err, size_t err_size, const char *fmt, ...) {
    va_list ap;

    if (!err || err_size == 0 || !fmt) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(err, err_size, fmt, ap);
    va_end(ap);
}

static int validate_super_update(const SuperUpdateSpec *edit, char *err, size_t err_size) {
    if (!edit) {
        set_err(err, err_size, "empty super edit request");
        return -1;
    }

    if (!edit->set_volume_name && !edit->set_mount_count &&
        !edit->set_max_mount_count && !edit->set_check_interval) {
        set_err(err, err_size, "super edit has no fields to update");
        return -1;
    }

    if (edit->set_volume_name && strlen(edit->volume_name) > 16) {
        set_err(err, err_size, "volume name must be <= 16 chars");
        return -1;
    }

    return 0;
}

static int validate_inode_update(const InodeUpdateSpec *edit, char *err, size_t err_size) {
    const Ext4InodeEdit *f;

    if (!edit || edit->inode_no == 0) {
        set_err(err, err_size, "invalid inode edit request");
        return -1;
    }

    f = &edit->fields;
    if (!f->set_mode && !f->set_uid && !f->set_gid && !f->set_atime &&
        !f->set_ctime && !f->set_mtime && !f->set_flags) {
        set_err(err, err_size, "inode edit has no fields to update");
        return -1;
    }

    return 0;
}

static int apply_super_update(Ext4Context *ctx,
                              const Ext4SuperView *super,
                              const SuperUpdateSpec *edit,
                              char *err,
                              size_t err_size) {
    unsigned char raw[EXT4_SUPERBLOCK_SIZE];
    Ext4SuperView verify;

    if (validate_super_update(edit, err, err_size) != 0) {
        return -1;
    }

    if (ext4_read_bytes(ctx, super->offset, raw, sizeof(raw), err, err_size) != 0) {
        return -1;
    }

    if (edit->set_volume_name) {
        memset(raw + 0x78, 0, 16);
        memcpy(raw + 0x78, edit->volume_name, strlen(edit->volume_name));
    }

    if (edit->set_mount_count) {
        write_le16(raw + 0x34, edit->mount_count);
    }

    if (edit->set_max_mount_count) {
        write_le16(raw + 0x36, (uint16_t)edit->max_mount_count);
    }

    if (edit->set_check_interval) {
        write_le32(raw + 0x44, edit->check_interval);
    }

    if (ext4_write_bytes(ctx, super->offset, raw, sizeof(raw), err, err_size) != 0) {
        return -1;
    }

    if (ext4_read_superblock(ctx, super->offset, &verify, err, err_size) != 0) {
        return -1;
    }

    if (edit->set_volume_name && strcmp(verify.volume_name, edit->volume_name) != 0) {
        set_err(err, err_size, "volume name verify failed");
        return -1;
    }

    if (edit->set_mount_count && verify.mount_count != edit->mount_count) {
        set_err(err, err_size, "mount count verify failed");
        return -1;
    }

    if (edit->set_max_mount_count && verify.max_mount_count != edit->max_mount_count) {
        set_err(err, err_size, "max mount count verify failed");
        return -1;
    }

    if (edit->set_check_interval && verify.check_interval != edit->check_interval) {
        set_err(err, err_size, "check interval verify failed");
        return -1;
    }

    return 0;
}

static int apply_inode_update(Ext4Context *ctx,
                              const Ext4SuperView *super,
                              const InodeUpdateSpec *edit,
                              char *err,
                              size_t err_size) {
    Ext4InodeView verify;

    if (validate_inode_update(edit, err, err_size) != 0) {
        return -1;
    }

    if (ext4_write_inode_fields(ctx, super, edit->inode_no, &edit->fields, err, err_size) != 0) {
        return -1;
    }

    if (ext4_read_inode(ctx, super, edit->inode_no, &verify, err, err_size) != 0) {
        return -1;
    }

    if (edit->fields.set_mode && verify.mode != edit->fields.mode) {
        set_err(err, err_size, "inode mode verify failed");
        return -1;
    }

    if (edit->fields.set_uid && verify.uid != edit->fields.uid) {
        set_err(err, err_size, "inode uid verify failed");
        return -1;
    }

    if (edit->fields.set_gid && verify.gid != edit->fields.gid) {
        set_err(err, err_size, "inode gid verify failed");
        return -1;
    }

    if (edit->fields.set_atime && verify.atime != edit->fields.atime) {
        set_err(err, err_size, "inode atime verify failed");
        return -1;
    }

    if (edit->fields.set_ctime && verify.ctime != edit->fields.ctime) {
        set_err(err, err_size, "inode ctime verify failed");
        return -1;
    }

    if (edit->fields.set_mtime && verify.mtime != edit->fields.mtime) {
        set_err(err, err_size, "inode mtime verify failed");
        return -1;
    }

    if (edit->fields.set_flags && verify.flags != edit->fields.flags) {
        set_err(err, err_size, "inode flags verify failed");
        return -1;
    }

    return 0;
}

int metadata_editor_apply(Ext4Context *ctx,
                          const Ext4SuperView *super,
                          const MetadataUpdateRequest *req,
                          MetadataUpdateResult *result,
                          char *err,
                          size_t err_size) {
    char backup_path[PATH_MAX];

    if (!ctx || !super || !req || !result) {
        set_err(err, err_size, "invalid editor parameters");
        return -1;
    }

    memset(result, 0, sizeof(*result));

    if (!ctx->write_enabled || ctx->readonly_forced) {
        set_err(err, err_size, "editing is disabled in readonly mode");
        return -1;
    }

    backup_path[0] = '\0';
    if (ext4_create_backup(ctx, backup_path, sizeof(backup_path), err, err_size) != 0) {
        return -1;
    }

    if (backup_path[0] != '\0') {
        result->backup_created = true;
        snprintf(result->backup_path, sizeof(result->backup_path), "%s", backup_path);
    }

    if (req->target == METADATA_TARGET_SUPER) {
        if (apply_super_update(ctx, super, &req->as.super, err, err_size) != 0) {
            return -1;
        }
    } else if (req->target == METADATA_TARGET_INODE) {
        if (apply_inode_update(ctx, super, &req->as.inode, err, err_size) != 0) {
            return -1;
        }
    } else {
        set_err(err, err_size, "unknown edit target");
        return -1;
    }

    result->success = true;
    snprintf(result->summary, sizeof(result->summary), "metadata update applied successfully");
    return 0;
}
