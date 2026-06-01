#ifndef DASHBOARD_H
#define DASHBOARD_H

#include "metadata_editor.h"
#include "ext4_dir.h"

typedef enum {
    APP_LANG_EN = 0,
    APP_LANG_RU = 1,
} AppLanguage;

int dashboard_run(Ext4Context *ctx, Ext4SuperView *super, AppLanguage initial_language);

#endif
