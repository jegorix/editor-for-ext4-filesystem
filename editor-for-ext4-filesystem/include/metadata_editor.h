#ifndef METADATA_EDITOR_H
#define METADATA_EDITOR_H

#include <stddef.h>
#include <stdint.h>

#include "ext4_inode.h"

typedef enum {
    METADATA_TARGET_SUPER = 1,
    METADATA_TARGET_INODE = 2,
} MetadataTarget;

typedef struct {
    bool set_volume_name;
    char volume_name[17];
    bool set_mount_count;
    uint16_t mount_count;
    bool set_max_mount_count;
    int16_t max_mount_count;
    bool set_check_interval;
    uint32_t check_interval;
} SuperUpdateSpec;

typedef struct {
    uint32_t inode_no;
    Ext4InodeEdit fields;
} InodeUpdateSpec;

typedef struct {
    MetadataTarget target;
    union {
        SuperUpdateSpec super;
        InodeUpdateSpec inode;
    } as;
} MetadataUpdateRequest;

typedef struct {
    bool success;
    bool backup_created;
    char backup_path[PATH_MAX];
    char summary[256];
} MetadataUpdateResult;

int metadata_editor_apply(Ext4Context *ctx,
                          const Ext4SuperView *super,
                          const MetadataUpdateRequest *req,
                          MetadataUpdateResult *result,
                          char *err,
                          size_t err_size);

#endif
