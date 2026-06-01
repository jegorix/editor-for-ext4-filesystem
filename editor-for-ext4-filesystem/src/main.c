#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ext4_super.h"
#include "dashboard.h"

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s --image <path> [--readonly|--write] [--lang en|ru]\n"
            "\n"
            "Options:\n"
            "  --image <path>       Path to ext4 image file\n"
            "  --readonly           Read-only mode (default)\n"
            "  --write              Enable write mode\n"
            "  --lang <en|ru>       Interface language\n"
            "  -h, --help           Show this help\n",
            prog);
}

static int parse_language(const char *value, AppLanguage *language) {
    if (!value || !language) {
        return -1;
    }

    if (strcmp(value, "en") == 0) {
        *language = APP_LANG_EN;
        return 0;
    }
    if (strcmp(value, "ru") == 0) {
        *language = APP_LANG_RU;
        return 0;
    }

    return -1;
}

static int parse_cli(int argc, char **argv, const char **image, bool *write_mode, AppLanguage *language) {
    int i;

    *image = NULL;
    *write_mode = false;
    *language = APP_LANG_EN;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--image") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--image requires a value\n");
                return -1;
            }
            *image = argv[++i];
        } else if (strcmp(argv[i], "--readonly") == 0) {
            *write_mode = false;
        } else if (strcmp(argv[i], "--write") == 0) {
            *write_mode = true;
        } else if (strcmp(argv[i], "--lang") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--lang requires a value\n");
                return -1;
            }
            if (parse_language(argv[++i], language) != 0) {
                fprintf(stderr, "--lang must be 'en' or 'ru'\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 1;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return -1;
        }
    }

    if (!*image) {
        fprintf(stderr, "--image is required\n");
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {
    const char *image_path;
    bool write_mode;
    AppLanguage language;
    int rc;
    char err[256];
    char warn[256];
    bool force_ro = false;
    Ext4Context ctx;
    Ext4SuperView super;

    setlocale(LC_ALL, "");
    rc = parse_cli(argc, argv, &image_path, &write_mode, &language);
    if (rc != 0) {
        return rc < 0 ? 1 : 0;
    }

    if (ext4_open_image(&ctx, image_path, write_mode, err, sizeof(err)) != 0) {
        fprintf(stderr, "open error: %s\n", err);
        return 1;
    }

    if (ext4_read_primary_super(&ctx, &super, err, sizeof(err)) != 0) {
        if (ext4_find_any_valid_super(&ctx, &super, err, sizeof(err)) != 0) {
            fprintf(stderr, "superblock error: %s\n", err);
            ext4_close_image(&ctx);
            return 1;
        }
        fprintf(stderr, "warning: primary superblock invalid, using backup at offset %llu\n",
                (unsigned long long)super.offset);
    }

    if (ext4_check_feature_support(&super, &force_ro, warn, sizeof(warn)) == 0 && force_ro) {
        ctx.readonly_forced = true;
        fprintf(stderr, "warning: %s\n", warn[0] ? warn : "readonly mode forced by feature check");
    }

    rc = dashboard_run(&ctx, &super, language);

    ext4_close_image(&ctx);
    return rc;
}
