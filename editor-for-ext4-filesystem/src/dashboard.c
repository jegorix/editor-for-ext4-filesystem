#include "dashboard.h"

#include <ctype.h>
#include <locale.h>
#include <ncurses.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ext4_super.h"
#include "util.h"

#define UI_MAX_LINES 1024
#define UI_LINE_LEN 512
#define MAX_SCAN_ENTRIES 16384U

#define EXT4_FT_REG_FILE 1U
#define EXT4_FT_DIR 2U
#define EXT4_FT_SYMLINK 7U

enum {
    PAIR_BASE = 1,
    PAIR_HEADER = 2,
    PAIR_FRAME = 3,
    PAIR_ACTIVE = 4,
    PAIR_ACCENT = 5,
};

typedef struct {
    char lines[UI_MAX_LINES][UI_LINE_LEN];
    size_t count;
} UiBuffer;

typedef struct {
    const char *label_en;
    const char *label_ru;
    const char *hint_en;
    const char *hint_ru;
} MenuEntry;

typedef struct {
    AppLanguage lang;
    bool use_colors;
} DashboardState;

static const char *tr(const DashboardState *state, const char *en, const char *ru) {
    return (state && state->lang == APP_LANG_RU) ? ru : en;
}

static const char *entry_label(const DashboardState *state, const MenuEntry *entry) {
    return tr(state, entry->label_en, entry->label_ru);
}

static const char *entry_hint(const DashboardState *state, const MenuEntry *entry) {
    return tr(state, entry->hint_en, entry->hint_ru);
}

static const char *language_name(AppLanguage lang) {
    return lang == APP_LANG_RU ? "RU" : "EN";
}

static const char *mode_name(const DashboardState *state, const Ext4Context *ctx) {
    return (ctx->write_enabled && !ctx->readonly_forced)
               ? tr(state, "write", "запись")
               : tr(state, "readonly", "только чтение");
}

static void ui_clear(UiBuffer *ui) {
    if (ui) {
        ui->count = 0;
    }
}

static void ui_add_line(UiBuffer *ui, const char *fmt, ...) {
    va_list ap;

    if (!ui || ui->count >= UI_MAX_LINES || !fmt) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(ui->lines[ui->count], UI_LINE_LEN, fmt, ap);
    va_end(ap);
    ui->count++;
}

static void print_fit(WINDOW *win, int y, int x, int max_width, const char *fmt, ...) {
    char buf[UI_LINE_LEN];
    va_list ap;

    if (!win || !fmt || max_width <= 0) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    mvwprintw(win, y, x, "%.*s", max_width, buf);
}

static const char *inode_flag_hint(const DashboardState *state) {
    return tr(state,
              "Examples: 0x10 immutable, 0x20 append-only, 0x80000 extents",
              "Примеры: 0x10 immutable, 0x20 append-only, 0x80000 extents");
}

static void append_token(char *out, size_t out_size, const char *text) {
    size_t len;

    if (!out || out_size == 0 || !text || !*text) {
        return;
    }

    len = strlen(out);
    if (len > 0) {
        snprintf(out + len, out_size - len, ", %s", text);
    } else {
        snprintf(out, out_size, "%s", text);
    }
}

static void describe_super_compat(const DashboardState *state, uint32_t flags,
                                  char *out, size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';

    if (flags & 0x0004U) append_token(out, out_size, tr(state, "journal", "журнал"));
    if (flags & 0x0008U) append_token(out, out_size, tr(state, "xattrs", "xattr"));
    if (flags & 0x0010U) append_token(out, out_size, tr(state, "resize", "resize"));
    if (flags & 0x0020U) append_token(out, out_size, tr(state, "dir hash", "хеш каталогов"));
    if (flags & 0x1000U) append_token(out, out_size, tr(state, "bg checksums", "контр. суммы групп"));
    if (out[0] == '\0') {
        snprintf(out, out_size, "%s", tr(state, "none / unknown", "нет / неизвестно"));
    }
}

static void describe_super_incompat(const DashboardState *state, uint32_t flags,
                                    char *out, size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';

    if (flags & 0x0002U) append_token(out, out_size, tr(state, "filetype", "тип файла"));
    if (flags & 0x0040U) append_token(out, out_size, tr(state, "extents", "extents"));
    if (flags & 0x0080U) append_token(out, out_size, tr(state, "64-bit", "64-бит"));
    if (flags & 0x0200U) append_token(out, out_size, tr(state, "flex_bg", "flex_bg"));
    if (flags & 0x2000U) append_token(out, out_size, tr(state, "dirdata", "dirdata"));
    if (out[0] == '\0') {
        snprintf(out, out_size, "%s", tr(state, "none / unknown", "нет / неизвестно"));
    }
}

static void describe_super_ro_compat(const DashboardState *state, uint32_t flags,
                                     char *out, size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';

    if (flags & 0x0001U) append_token(out, out_size, tr(state, "sparse super", "sparse super"));
    if (flags & 0x0002U) append_token(out, out_size, tr(state, "large file", "large file"));
    if (flags & 0x0008U) append_token(out, out_size, tr(state, "huge file", "huge file"));
    if (flags & 0x0020U) append_token(out, out_size, tr(state, "dir nlink", "dir nlink"));
    if (flags & 0x0040U) append_token(out, out_size, tr(state, "extra inode", "extra inode"));
    if (flags & 0x0400U) append_token(out, out_size, tr(state, "metadata csum", "metadata csum"));
    if (out[0] == '\0') {
        snprintf(out, out_size, "%s", tr(state, "none / unknown", "нет / неизвестно"));
    }
}

static void describe_inode_flags(const DashboardState *state, uint32_t flags,
                                 char *out, size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';

    if (flags & 0x00000010U) append_token(out, out_size, "immutable");
    if (flags & 0x00000020U) append_token(out, out_size, "append-only");
    if (flags & 0x00001000U) append_token(out, out_size, tr(state, "hashed dir", "хеш-каталог"));
    if (flags & 0x00080000U) append_token(out, out_size, "extents");
    if (out[0] == '\0') {
        snprintf(out, out_size, "%s", tr(state, "no special flags", "спецфлагов нет"));
    }
}

static void format_bytes(uint64_t value, char *out, size_t out_size) {
    static const char *units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    double size = (double)value;
    size_t unit = 0;

    while (size >= 1024.0 && unit + 1 < sizeof(units) / sizeof(units[0])) {
        size /= 1024.0;
        unit++;
    }

    snprintf(out, out_size, "%.2f %s", size, units[unit]);
}

static double ratio_percent(uint64_t part, uint64_t total) {
    if (total == 0) {
        return 0.0;
    }
    return (double)part * 100.0 / (double)total;
}

static void shorten_path(const char *path, char *out, size_t out_size) {
    size_t len;

    if (!out || out_size == 0) {
        return;
    }

    out[0] = '\0';
    if (!path) {
        return;
    }

    len = strlen(path);
    if (len < out_size) {
        snprintf(out, out_size, "%s", path);
        return;
    }

    if (out_size <= 4) {
        snprintf(out, out_size, "%s", path);
        return;
    }

    snprintf(out, out_size, "...%s", path + len - (out_size - 4));
}

static void init_palette(DashboardState *state) {
    state->use_colors = false;
    if (!has_colors()) {
        return;
    }

    start_color();
    use_default_colors();
    init_pair(PAIR_BASE, COLOR_WHITE, -1);
    init_pair(PAIR_HEADER, COLOR_BLACK, COLOR_CYAN);
    init_pair(PAIR_FRAME, COLOR_CYAN, -1);
    init_pair(PAIR_ACTIVE, COLOR_BLACK, COLOR_YELLOW);
    init_pair(PAIR_ACCENT, COLOR_GREEN, -1);
    state->use_colors = true;
}

static void apply_attr(WINDOW *win, int pair, bool bold) {
    if (!win) {
        return;
    }

    if (pair > 0) {
        wattron(win, COLOR_PAIR(pair));
    }
    if (bold) {
        wattron(win, A_BOLD);
    }
}

static void clear_attr(WINDOW *win, int pair, bool bold) {
    if (!win) {
        return;
    }

    if (pair > 0) {
        wattroff(win, COLOR_PAIR(pair));
    }
    if (bold) {
        wattroff(win, A_BOLD);
    }
}

static void draw_frame(WINDOW *win, int y, int x, int h, int w, int pair, const char *title) {
    if (!win || h < 2 || w < 2) {
        return;
    }

    apply_attr(win, pair, false);
    mvwhline(win, y, x + 1, ACS_HLINE, w - 2);
    mvwhline(win, y + h - 1, x + 1, ACS_HLINE, w - 2);
    mvwvline(win, y + 1, x, ACS_VLINE, h - 2);
    mvwvline(win, y + 1, x + w - 1, ACS_VLINE, h - 2);
    mvwaddch(win, y, x, ACS_ULCORNER);
    mvwaddch(win, y, x + w - 1, ACS_URCORNER);
    mvwaddch(win, y + h - 1, x, ACS_LLCORNER);
    mvwaddch(win, y + h - 1, x + w - 1, ACS_LRCORNER);
    if (title && *title && w > 6) {
        mvwprintw(win, y, x + 2, " %s ", title);
    }
    clear_attr(win, pair, false);
}

static WINDOW *create_centered_window(int req_h, int req_w, const char *title) {
    int rows;
    int cols;
    int h;
    int w;
    int y;
    int x;
    WINDOW *win;

    getmaxyx(stdscr, rows, cols);

    h = req_h;
    w = req_w;
    if (h > rows - 2) {
        h = rows - 2;
    }
    if (w > cols - 2) {
        w = cols - 2;
    }
    if (h < 12) {
        h = 12;
    }
    if (w < 48) {
        w = 48;
    }

    y = (rows - h) / 2;
    x = (cols - w) / 2;
    win = newwin(h, w, y, x);
    keypad(win, TRUE);
    draw_frame(win, 0, 0, h, w, PAIR_FRAME, title);
    return win;
}

static void close_dialog_window(WINDOW *win) {
    if (!win) {
        return;
    }

    werase(win);
    wrefresh(win);
    delwin(win);
    touchwin(stdscr);
    refresh();
}

static void draw_context_summary(WINDOW *win,
                                 const DashboardState *state,
                                 const Ext4Context *ctx,
                                 const Ext4SuperView *super,
                                 int y) {
    char image_buf[96];

    shorten_path(ctx->image_path, image_buf, sizeof(image_buf));
    mvwprintw(win, y, 2, "%s", image_buf);
    mvwprintw(win, y + 1, 2, "%s: %s | %s: %s | %s: %u",
              tr(state, "Mode", "Режим"),
              mode_name(state, ctx),
              tr(state, "Lang", "Язык"),
              language_name(state->lang),
              tr(state, "Block", "Блок"),
              super->block_size);
}

static void render_text_dialog(WINDOW *win,
                               const DashboardState *state,
                               const char *title,
                               const UiBuffer *ui) {
    int rows;
    int cols;
    int view_y;
    int view_h;
    int top = 0;
    int ch;

    getmaxyx(win, rows, cols);
    view_y = 2;
    view_h = rows - 6;

    while (1) {
        int i;

        werase(win);
        draw_frame(win, 0, 0, rows, cols, PAIR_FRAME, title);
        for (i = 0; i < view_h; i++) {
            int idx = top + i;
            if ((size_t)idx >= ui->count) {
                break;
            }
            mvwprintw(win, view_y + i, 2, "%.*s", cols - 4, ui->lines[idx]);
        }

        apply_attr(win, PAIR_ACCENT, true);
        print_fit(win, rows - 2, 2, cols - 4, "%s",
                  tr(state,
                     "Up/Down/PgUp/PgDn to scroll, Enter/Esc to close",
                     "Up/Down/PgUp/PgDn для прокрутки, Enter/Esc для выхода"));
        clear_attr(win, PAIR_ACCENT, true);

        if (ui->count > (size_t)view_h) {
            print_fit(win, rows - 2, cols - 12, 10, "%d-%d/%zu",
                      top + 1,
                      (top + view_h < (int)ui->count) ? (top + view_h) : (int)ui->count,
                      ui->count);
        }

        wrefresh(win);
        ch = wgetch(win);

        if (ch == KEY_UP) {
            if (top > 0) {
                top--;
            }
        } else if (ch == KEY_DOWN) {
            if ((size_t)(top + view_h) < ui->count) {
                top++;
            }
        } else if (ch == KEY_NPAGE) {
            top += view_h;
            if ((size_t)(top + view_h) > ui->count) {
                top = (ui->count > (size_t)view_h) ? (int)(ui->count - (size_t)view_h) : 0;
            }
        } else if (ch == KEY_PPAGE) {
            top -= view_h;
            if (top < 0) {
                top = 0;
            }
        } else if (ch == '\n' || ch == KEY_ENTER || ch == 10 || ch == 13 ||
                   ch == 27 || ch == 'q' || ch == 'Q') {
            break;
        }
    }
}

static void show_buffer_dialog(const DashboardState *state, const char *title, const UiBuffer *ui) {
    WINDOW *win = create_centered_window(26, 102, title);
    render_text_dialog(win, state, title, ui);
    close_dialog_window(win);
}

static void show_notice(const DashboardState *state,
                        const char *title_en,
                        const char *title_ru,
                        const char *message_en,
                        const char *message_ru) {
    UiBuffer ui;

    ui_clear(&ui);
    ui_add_line(&ui, "%s", tr(state, message_en, message_ru));
    show_buffer_dialog(state, tr(state, title_en, title_ru), &ui);
}

static void show_error_text(const DashboardState *state,
                            const char *title_en,
                            const char *title_ru,
                            const char *message) {
    UiBuffer ui;

    ui_clear(&ui);
    ui_add_line(&ui, "%s", message ? message : "");
    show_buffer_dialog(state, tr(state, title_en, title_ru), &ui);
}

static int prompt_input(const DashboardState *state,
                        const char *title,
                        const char *label,
                        char *out,
                        size_t out_size,
                        const char *initial,
                        const char *hint) {
    WINDOW *win;
    int rows;
    int cols;

    if (!out || out_size == 0) {
        return -1;
    }

    win = create_centered_window(12, 86, title);
    getmaxyx(win, rows, cols);
    memset(out, 0, out_size);
    if (initial && *initial) {
        snprintf(out, out_size, "%s", initial);
    }

    echo();
    curs_set(1);
    werase(win);
    draw_frame(win, 0, 0, rows, cols, PAIR_FRAME, title);
    print_fit(win, 2, 2, cols - 4, "%s", label);
    if (hint && *hint) {
        print_fit(win, 3, 2, cols - 4, "%s", hint);
    }
    mvwhline(win, 4, 2, ACS_HLINE, cols - 4);
    print_fit(win, rows - 2, 2, cols - 4, "%s",
              tr(state,
                 "Type value and press Enter.",
                 "Введите значение и нажмите Enter."));
    mvwprintw(win, 5, 3, "%.*s", cols - 6, out);
    wmove(win, 5, 3);
    wrefresh(win);
    if (wgetnstr(win, out, (int)out_size - 1) == ERR) {
        noecho();
        curs_set(0);
        close_dialog_window(win);
        return -1;
    }
    noecho();
    curs_set(0);
    close_dialog_window(win);
    return 0;
}

static int choose_from_list(const DashboardState *state,
                            const char *title,
                            const char *subtitle,
                            const MenuEntry *items,
                            size_t item_count,
                            int initial_selection) {
    WINDOW *win;
    int rows;
    int cols;
    int list_y = 4;
    int list_h;
    int selected;
    int top = 0;
    int ch;

    if (!items || item_count == 0) {
        return -1;
    }

    if (initial_selection < 0) {
        initial_selection = 0;
    }
    if ((size_t)initial_selection >= item_count) {
        initial_selection = (int)item_count - 1;
    }

    selected = initial_selection;
    win = create_centered_window(22, 90, title);
    getmaxyx(win, rows, cols);
    list_h = rows - 8;

    while (1) {
        int i;

        werase(win);
        draw_frame(win, 0, 0, rows, cols, PAIR_FRAME, title);
        mvwprintw(win, 2, 2, "%s", subtitle);

        if (selected < top) {
            top = selected;
        }
        if (selected >= top + list_h) {
            top = selected - list_h + 1;
        }

        for (i = 0; i < list_h; i++) {
            int idx = top + i;
            if ((size_t)idx >= item_count) {
                break;
            }

            if (idx == selected) {
                apply_attr(win, PAIR_ACTIVE, true);
                mvwprintw(win, list_y + i, 3, " %-28s ", entry_label(state, &items[idx]));
                clear_attr(win, PAIR_ACTIVE, true);
            } else {
                mvwprintw(win, list_y + i, 3, " %-28s ", entry_label(state, &items[idx]));
            }
            mvwprintw(win, list_y + i, 34, "%.*s", cols - 37, entry_hint(state, &items[idx]));
        }

        mvwprintw(win, rows - 2, 2, "%s",
                  tr(state, "Up/Down, Enter, Esc", "Up/Down, Enter, Esc"));
        wrefresh(win);
        ch = wgetch(win);

        if (ch == KEY_UP || ch == 'k' || ch == 'K') {
            if (selected > 0) {
                selected--;
            }
        } else if (ch == KEY_DOWN || ch == 'j' || ch == 'J') {
            if ((size_t)selected + 1 < item_count) {
                selected++;
            }
        } else if (ch == KEY_NPAGE) {
            selected += list_h;
            if ((size_t)selected >= item_count) {
                selected = (int)item_count - 1;
            }
        } else if (ch == KEY_PPAGE) {
            selected -= list_h;
            if (selected < 0) {
                selected = 0;
            }
        } else if (ch == '\n' || ch == KEY_ENTER || ch == 10 || ch == 13) {
            close_dialog_window(win);
            return selected;
        } else if (ch == 27 || ch == 'q' || ch == 'Q') {
            close_dialog_window(win);
            return -1;
        }
    }
}

static int parse_i16(const char *text, int16_t *out) {
    char *end = NULL;
    long v;

    if (!text || !*text || !out) {
        return -1;
    }

    v = strtol(text, &end, 10);
    if (!end || *end != '\0' || v < -32768L || v > 32767L) {
        return -1;
    }

    *out = (int16_t)v;
    return 0;
}

static const char *file_type_label(const DashboardState *state, uint8_t type) {
    switch (type) {
        case EXT4_FT_REG_FILE:
            return tr(state, "file", "файл");
        case EXT4_FT_DIR:
            return tr(state, "dir", "каталог");
        case EXT4_FT_SYMLINK:
            return tr(state, "symlink", "ссылка");
        default:
            return tr(state, "other", "прочее");
    }
}

static void show_superblock_view(const DashboardState *state, const Ext4SuperView *super) {
    UiBuffer ui;
    char compat_buf[128];
    char incompat_buf[128];
    char ro_compat_buf[128];

    describe_super_compat(state, super->feature_compat, compat_buf, sizeof(compat_buf));
    describe_super_incompat(state, super->feature_incompat, incompat_buf, sizeof(incompat_buf));
    describe_super_ro_compat(state, super->feature_ro_compat, ro_compat_buf, sizeof(ro_compat_buf));
    ui_clear(&ui);
    ui_add_line(&ui, "%s %llu", tr(state, "Superblock offset:", "Смещение суперблока:"),
                (unsigned long long)super->offset);
    ui_add_line(&ui, "%s %s", tr(state, "Volume name:", "Имя тома:"), super->volume_name);
    ui_add_line(&ui, "%s %u", tr(state, "Block size:", "Размер блока:"), super->block_size);
    ui_add_line(&ui, "%s %llu", tr(state, "Blocks count:", "Число блоков:"),
                (unsigned long long)super->blocks_count);
    ui_add_line(&ui, "%s %u", tr(state, "Inodes count:", "Число inode:"), super->inodes_count);
    ui_add_line(&ui, "%s %u", tr(state, "Blocks per group:", "Блоков в группе:"),
                super->blocks_per_group);
    ui_add_line(&ui, "%s %u", tr(state, "Inodes per group:", "Inode в группе:"),
                super->inodes_per_group);
    ui_add_line(&ui, "%s %u", tr(state, "Inode size:", "Размер inode:"), super->inode_size);
    ui_add_line(&ui, "%s %u", tr(state, "Descriptor size:", "Размер дескриптора:"),
                super->desc_size);
    ui_add_line(&ui, "%s %u", tr(state, "Mount count:", "Счётчик монтирований:"),
                super->mount_count);
    ui_add_line(&ui, "%s %d", tr(state, "Max mount count:", "Макс. счётчик монтирований:"),
                super->max_mount_count);
    ui_add_line(&ui, "%s %u", tr(state, "Check interval:", "Интервал проверки:"),
                super->check_interval);
    ui_add_line(&ui, "%s 0x%08x", tr(state, "feature_compat:", "feature_compat:"),
                super->feature_compat);
    ui_add_line(&ui, "  %s", compat_buf);
    ui_add_line(&ui, "%s 0x%08x", tr(state, "feature_incompat:", "feature_incompat:"),
                super->feature_incompat);
    ui_add_line(&ui, "  %s", incompat_buf);
    ui_add_line(&ui, "%s 0x%08x", tr(state, "feature_ro_compat:", "feature_ro_compat:"),
                super->feature_ro_compat);
    ui_add_line(&ui, "  %s", ro_compat_buf);
    ui_add_line(&ui, "%s %s", tr(state, "Sparse super:", "Sparse super:"),
                super->sparse_super ? tr(state, "yes", "да") : tr(state, "no", "нет"));
    ui_add_line(&ui, "  %s",
                tr(state,
                   "Backup superblocks are kept only in some block groups",
                   "Резервные суперблоки хранятся только в части групп"));
    ui_add_line(&ui, "%s %s", tr(state, "64-bit mode:", "64-битный режим:"),
                super->is_64bit ? tr(state, "yes", "да") : tr(state, "no", "нет"));
    show_buffer_dialog(state, tr(state, "Superblock", "Суперблок"), &ui);
}

static void show_backup_superblocks(const DashboardState *state,
                                    const Ext4Context *ctx,
                                    const Ext4SuperView *super) {
    UiBuffer ui;
    uint64_t offsets[128];
    size_t count;
    size_t i;

    ui_clear(&ui);
    count = ext4_collect_backup_super_offsets(super, ctx->image_size, offsets, 128);
    ui_add_line(&ui, "%s %zu",
                tr(state, "Backup superblock candidates:", "Кандидаты резервных суперблоков:"),
                count);
    ui_add_line(&ui, "");

    for (i = 0; i < count; i++) {
        Ext4SuperView backup;
        char err[256];

        if (ext4_read_superblock(ctx, offsets[i], &backup, err, sizeof(err)) == 0) {
            ui_add_line(&ui, "[%zu] off=%llu",
                        i + 1,
                        (unsigned long long)offsets[i]);
            ui_add_line(&ui, "    magic=0x%04x  block=%u  volume=%s",
                        backup.magic,
                        backup.block_size,
                        backup.volume_name[0] ? backup.volume_name : "-");
        } else {
            ui_add_line(&ui, "[%zu] off=%llu",
                        i + 1,
                        (unsigned long long)offsets[i]);
            ui_add_line(&ui, "    %s: %s",
                        tr(state, "error", "ошибка"),
                        err);
        }
    }

    show_buffer_dialog(state, tr(state, "Backup Superblocks", "Резервные суперблоки"), &ui);
}

static void show_group_descriptor(const DashboardState *state,
                                  const Ext4Context *ctx,
                                  const Ext4SuperView *super) {
    UiBuffer ui;
    Ext4GroupDescView group;
    char input[64];
    char err[256];
    uint32_t group_index;

    if (prompt_input(state,
                     tr(state, "Group Descriptor", "Дескриптор группы"),
                     tr(state, "Enter group index:", "Введите индекс группы:"),
                     input, sizeof(input), "0",
                     tr(state, "Example: 0, 1, 2", "Пример: 0, 1, 2")) != 0) {
        return;
    }

    if (str_to_u32(input, &group_index) != 0) {
        show_notice(state, "Group Descriptor", "Дескриптор группы",
                    "Invalid group index.", "Некорректный индекс группы.");
        return;
    }

    if (ext4_read_group_desc(ctx, super, group_index, &group, err, sizeof(err)) != 0) {
        show_error_text(state, "Group Descriptor", "Дескриптор группы", err);
        return;
    }

    ui_clear(&ui);
    ui_add_line(&ui, "%s %u", tr(state, "Group:", "Группа:"), group.group_index);
    ui_add_line(&ui, "%s %llu", tr(state, "Descriptor offset:", "Смещение дескриптора:"),
                (unsigned long long)group.offset);
    ui_add_line(&ui, "%s %llu", tr(state, "Inode table block:", "Блок таблицы inode:"),
                (unsigned long long)group.inode_table_block);
    ui_add_line(&ui, "%s %u", tr(state, "Free blocks:", "Свободных блоков:"),
                group.free_blocks_count);
    ui_add_line(&ui, "%s %u", tr(state, "Free inodes:", "Свободных inode:"),
                group.free_inodes_count);
    ui_add_line(&ui, "%s %u", tr(state, "Used directories:", "Используемых каталогов:"),
                group.used_dirs_count);

    show_buffer_dialog(state, tr(state, "Group Descriptor", "Дескриптор группы"), &ui);
}

static void show_inode_view(const DashboardState *state,
                            const Ext4Context *ctx,
                            const Ext4SuperView *super) {
    UiBuffer ui;
    Ext4InodeView inode;
    char input[64];
    char err[256];
    char atime_buf[64];
    char ctime_buf[64];
    char mtime_buf[64];
    char flags_buf[128];
    uint32_t inode_no;
    int i;

    if (prompt_input(state,
                     tr(state, "Inode Details", "Данные inode"),
                     tr(state, "Enter inode number:", "Введите номер inode:"),
                     input, sizeof(input), "2",
                     tr(state, "Example: 2 for root directory", "Пример: 2 для корневого каталога")) != 0) {
        return;
    }

    if (str_to_u32(input, &inode_no) != 0) {
        show_notice(state, "Inode Details", "Данные inode",
                    "Invalid inode number.", "Некорректный номер inode.");
        return;
    }

    if (ext4_read_inode(ctx, super, inode_no, &inode, err, sizeof(err)) != 0) {
        show_error_text(state, "Inode Details", "Данные inode", err);
        return;
    }

    format_unix_time(inode.atime, atime_buf, sizeof(atime_buf));
    format_unix_time(inode.ctime, ctime_buf, sizeof(ctime_buf));
    format_unix_time(inode.mtime, mtime_buf, sizeof(mtime_buf));
    describe_inode_flags(state, inode.flags, flags_buf, sizeof(flags_buf));

    ui_clear(&ui);
    ui_add_line(&ui, "%s %u", tr(state, "Inode:", "Inode:"), inode.inode_no);
    ui_add_line(&ui, "%s 0%o", tr(state, "Mode:", "Режим:"), inode.mode);
    ui_add_line(&ui, "%s %u / %u", tr(state, "UID / GID:", "UID / GID:"), inode.uid, inode.gid);
    ui_add_line(&ui, "%s %llu", tr(state, "Size:", "Размер:"), (unsigned long long)inode.size);
    ui_add_line(&ui, "%s %s (%u)", tr(state, "Atime:", "Atime:"), atime_buf, inode.atime);
    ui_add_line(&ui, "%s %s (%u)", tr(state, "Ctime:", "Ctime:"), ctime_buf, inode.ctime);
    ui_add_line(&ui, "%s %s (%u)", tr(state, "Mtime:", "Mtime:"), mtime_buf, inode.mtime);
    ui_add_line(&ui, "%s %u", tr(state, "Links count:", "Число ссылок:"), inode.links_count);
    ui_add_line(&ui, "%s 0x%08x", tr(state, "Flags:", "Флаги:"), inode.flags);
    ui_add_line(&ui, "  %s", flags_buf);
    ui_add_line(&ui, "%s %s", tr(state, "Uses extents:", "Использует extents:"),
                inode.uses_extents ? tr(state, "yes", "да") : tr(state, "no", "нет"));
    ui_add_line(&ui, "%s %s", tr(state, "Is directory:", "Каталог:"),
                inode.is_directory ? tr(state, "yes", "да") : tr(state, "no", "нет"));
    ui_add_line(&ui, "");
    ui_add_line(&ui, "%s", tr(state, "Direct block pointers:", "Прямые указатели на блоки:"));
    for (i = 0; i < 12; i++) {
        ui_add_line(&ui, "  [%d] %u", i, inode.block[i]);
    }

    show_buffer_dialog(state, tr(state, "Inode Details", "Данные inode"), &ui);
}

static void show_directory_listing(const DashboardState *state,
                                   const Ext4Context *ctx,
                                   const Ext4SuperView *super) {
    UiBuffer ui;
    DirEntryView *entries;
    size_t count = 0;
    uint32_t inode_no = 2;
    char input[256];
    char err[256];
    size_t i;

    if (prompt_input(state,
                     tr(state, "Directory Browser", "Просмотр каталога"),
                     tr(state, "Target ('/' or '/path' or 'inode:N'):",
                                "Цель ('/' или '/path' или 'inode:N'):"),
                     input, sizeof(input), "/",
                     tr(state, "Examples: /, /lost+found, inode:2",
                                "Примеры: /, /lost+found, inode:2")) != 0) {
        return;
    }

    if (input[0] == '\0' || strcmp(input, "/") == 0) {
        inode_no = 2;
    } else if (strncmp(input, "inode:", 6) == 0) {
        if (str_to_u32(input + 6, &inode_no) != 0) {
            show_notice(state, "Directory Browser", "Просмотр каталога",
                        "Invalid inode target.", "Некорректная цель inode.");
            return;
        }
    } else {
        if (ext4_lookup_path(ctx, super, input, &inode_no, err, sizeof(err)) != 0) {
            show_error_text(state, "Directory Browser", "Просмотр каталога", err);
            return;
        }
    }

    entries = calloc(MAX_SCAN_ENTRIES, sizeof(*entries));
    if (!entries) {
        show_notice(state, "Directory Browser", "Просмотр каталога",
                    "Out of memory.", "Недостаточно памяти.");
        return;
    }

    if (ext4_list_dir_entries(ctx, super, inode_no, entries, MAX_SCAN_ENTRIES,
                              &count, err, sizeof(err)) != 0) {
        free(entries);
        show_error_text(state, "Directory Browser", "Просмотр каталога", err);
        return;
    }

    ui_clear(&ui);
    ui_add_line(&ui, "%s %u | %s %zu",
                tr(state, "Directory inode:", "Inode каталога:"),
                inode_no,
                tr(state, "entries:", "записей:"),
                count);
    ui_add_line(&ui, "");
    for (i = 0; i < count; i++) {
        ui_add_line(&ui, "%-32s inode=%u type=%s rec_len=%u",
                    entries[i].name,
                    entries[i].inode,
                    file_type_label(state, entries[i].file_type),
                    entries[i].rec_len);
    }

    free(entries);
    show_buffer_dialog(state, tr(state, "Directory Browser", "Просмотр каталога"), &ui);
}

static void show_find_by_path(const DashboardState *state,
                              const Ext4Context *ctx,
                              const Ext4SuperView *super) {
    UiBuffer ui;
    char input[256];
    char err[256];
    uint32_t inode_no;

    if (prompt_input(state,
                     tr(state, "Resolve Path", "Поиск по пути"),
                     tr(state, "Absolute path:", "Абсолютный путь:"),
                     input, sizeof(input), "/",
                     tr(state, "Example: /lost+found", "Пример: /lost+found")) != 0) {
        return;
    }

    if (input[0] != '/') {
        show_notice(state, "Resolve Path", "Поиск по пути",
                    "Path must be absolute.", "Путь должен быть абсолютным.");
        return;
    }

    if (ext4_lookup_path(ctx, super, input, &inode_no, err, sizeof(err)) != 0) {
        show_error_text(state, "Resolve Path", "Поиск по пути", err);
        return;
    }

    ui_clear(&ui);
    ui_add_line(&ui, "%s %s", tr(state, "Path:", "Путь:"), input);
    ui_add_line(&ui, "%s %u", tr(state, "Inode:", "Inode:"), inode_no);
    show_buffer_dialog(state, tr(state, "Resolve Path", "Поиск по пути"), &ui);
}

static void show_find_by_inode(const DashboardState *state,
                               const Ext4Context *ctx,
                               const Ext4SuperView *super) {
    UiBuffer ui;
    char input[64];
    char path[1024];
    char err[256];
    uint32_t inode_no;

    if (prompt_input(state,
                     tr(state, "Resolve Inode", "Поиск по inode"),
                     tr(state, "Inode number:", "Номер inode:"),
                     input, sizeof(input), "2",
                     tr(state, "Example: 2 for root directory", "Пример: 2 для корневого каталога")) != 0) {
        return;
    }

    if (str_to_u32(input, &inode_no) != 0) {
        show_notice(state, "Resolve Inode", "Поиск по inode",
                    "Invalid inode number.", "Некорректный номер inode.");
        return;
    }

    if (ext4_find_by_inode(ctx, super, inode_no, path, sizeof(path), err, sizeof(err)) != 0) {
        show_error_text(state, "Resolve Inode", "Поиск по inode", err);
        return;
    }

    ui_clear(&ui);
    ui_add_line(&ui, "%s %u", tr(state, "Inode:", "Inode:"), inode_no);
    ui_add_line(&ui, "%s %s", tr(state, "Path:", "Путь:"), path);
    show_buffer_dialog(state, tr(state, "Resolve Inode", "Поиск по inode"), &ui);
}

static void show_find_by_name(const DashboardState *state,
                              const Ext4Context *ctx,
                              const Ext4SuperView *super) {
    UiBuffer ui;
    char input[256];
    char path[1024];
    char err[256];
    uint32_t inode_no;

    if (prompt_input(state,
                     tr(state, "Search By Name", "Поиск по имени"),
                     tr(state, "Entry name:", "Имя записи:"),
                     input, sizeof(input), "",
                     tr(state, "Examples: lost+found, hello.txt", "Примеры: lost+found, hello.txt")) != 0) {
        return;
    }

    if (input[0] == '\0') {
        show_notice(state, "Search By Name", "Поиск по имени",
                    "Name cannot be empty.", "Имя не должно быть пустым.");
        return;
    }

    if (ext4_find_by_name(ctx, super, input, &inode_no, path, sizeof(path), err, sizeof(err)) != 0) {
        show_error_text(state, "Search By Name", "Поиск по имени", err);
        return;
    }

    ui_clear(&ui);
    ui_add_line(&ui, "%s %s", tr(state, "Name:", "Имя:"), input);
    ui_add_line(&ui, "%s %u", tr(state, "Inode:", "Inode:"), inode_no);
    ui_add_line(&ui, "%s %s", tr(state, "Path:", "Путь:"), path);
    show_buffer_dialog(state, tr(state, "Search By Name", "Поиск по имени"), &ui);
}

static void edit_super_metadata(const DashboardState *state,
                                Ext4Context *ctx,
                                Ext4SuperView *super) {
    const MenuEntry fields[] = {
        { "Volume label", "Метка тома", "Volume name up to 16 chars", "Имя тома до 16 символов" },
        { "Mount count", "Счётчик монтирований", "Current mount counter", "Текущий счётчик монтирований" },
        { "Max mount count", "Макс. счётчик", "Maximum mount counter", "Максимальный счётчик монтирований" },
        { "Check interval", "Интервал проверки", "fsck interval in seconds", "Интервал fsck в секундах" },
    };
    MetadataUpdateRequest request;
    MetadataUpdateResult result;
    UiBuffer ui;
    int choice;
    char input[128];
    char err[256];
    uint32_t value_u32;
    int16_t value_i16;

    if (!ctx->write_enabled || ctx->readonly_forced) {
        show_notice(state, "Edit Superblock", "Редактирование суперблока",
                    "Write mode is disabled. Restart with --write.",
                    "Режим записи отключён. Перезапустите программу с --write.");
        return;
    }

    choice = choose_from_list(state,
                              tr(state, "Edit Superblock", "Редактирование суперблока"),
                              tr(state, "Select field to update", "Выберите поле для изменения"),
                              fields, sizeof(fields) / sizeof(fields[0]), 0);
    if (choice < 0) {
        return;
    }

    if (prompt_input(state,
                     tr(state, "Edit Superblock", "Редактирование суперблока"),
                     tr(state, "New value:", "Новое значение:"),
                     input, sizeof(input), "",
                     tr(state, "Number or short label",
                                "Число или короткая строка")) != 0) {
        return;
    }

    memset(&request, 0, sizeof(request));
    request.target = METADATA_TARGET_SUPER;

    if (choice == 0) {
        size_t len = strlen(input);
        if (len > 16) {
            show_notice(state, "Edit Superblock", "Редактирование суперблока",
                        "Volume name must be <= 16 chars.",
                        "Имя тома должно быть длиной не более 16 символов.");
            return;
        }
        request.as.super.set_volume_name = true;
        memset(request.as.super.volume_name, 0, sizeof(request.as.super.volume_name));
        memcpy(request.as.super.volume_name, input, len);
    } else if (choice == 1) {
        if (str_to_u32(input, &value_u32) != 0 || value_u32 > 0xffffU) {
            show_notice(state, "Edit Superblock", "Редактирование суперблока",
                        "Invalid mount count.", "Некорректный счётчик монтирований.");
            return;
        }
        request.as.super.set_mount_count = true;
        request.as.super.mount_count = (uint16_t)value_u32;
    } else if (choice == 2) {
        if (parse_i16(input, &value_i16) != 0) {
            show_notice(state, "Edit Superblock", "Редактирование суперблока",
                        "Invalid max mount count.", "Некорректный максимум монтирований.");
            return;
        }
        request.as.super.set_max_mount_count = true;
        request.as.super.max_mount_count = value_i16;
    } else if (choice == 3) {
        if (str_to_u32(input, &value_u32) != 0) {
            show_notice(state, "Edit Superblock", "Редактирование суперблока",
                        "Invalid check interval.", "Некорректный интервал проверки.");
            return;
        }
        request.as.super.set_check_interval = true;
        request.as.super.check_interval = value_u32;
    }

    if (metadata_editor_apply(ctx, super, &request, &result, err, sizeof(err)) != 0) {
        show_error_text(state, "Edit Superblock", "Редактирование суперблока", err);
        return;
    }

    if (ext4_read_superblock(ctx, super->offset, super, err, sizeof(err)) != 0) {
        show_error_text(state, "Edit Superblock", "Редактирование суперблока", err);
        return;
    }

    ui_clear(&ui);
    ui_add_line(&ui, "%s", result.summary);
    if (result.backup_created) {
        ui_add_line(&ui, "%s %s", tr(state, "Backup:", "Резервная копия:"), result.backup_path);
    }
    show_buffer_dialog(state, tr(state, "Edit Superblock", "Редактирование суперблока"), &ui);
}

static void edit_inode_metadata(const DashboardState *state,
                                Ext4Context *ctx,
                                Ext4SuperView *super) {
    const MenuEntry fields[] = {
        { "Mode", "Режим", "UNIX mode value", "Значение UNIX mode" },
        { "UID", "UID", "User id", "Идентификатор пользователя" },
        { "GID", "GID", "Group id", "Идентификатор группы" },
        { "Atime", "Atime", "Access time", "Время доступа" },
        { "Ctime", "Ctime", "Change time", "Время изменения inode" },
        { "Mtime", "Mtime", "Modify time", "Время модификации" },
        { "Flags", "Флаги", "Inode flags", "Флаги inode" },
    };
    MetadataUpdateRequest request;
    MetadataUpdateResult result;
    UiBuffer ui;
    int choice;
    char inode_input[64];
    char value_input[128];
    char err[256];
    uint32_t inode_no;
    uint32_t value_u32;

    if (!ctx->write_enabled || ctx->readonly_forced) {
        show_notice(state, "Edit Inode", "Редактирование inode",
                    "Write mode is disabled. Restart with --write.",
                    "Режим записи отключён. Перезапустите программу с --write.");
        return;
    }

    if (prompt_input(state,
                     tr(state, "Edit Inode", "Редактирование inode"),
                     tr(state, "Inode number:", "Номер inode:"),
                     inode_input, sizeof(inode_input), "2",
                     tr(state, "Example: 2 for root directory", "Пример: 2 для корневого каталога")) != 0) {
        return;
    }

    if (str_to_u32(inode_input, &inode_no) != 0) {
        show_notice(state, "Edit Inode", "Редактирование inode",
                    "Invalid inode number.", "Некорректный номер inode.");
        return;
    }

    choice = choose_from_list(state,
                              tr(state, "Edit Inode", "Редактирование inode"),
                              tr(state, "Select field to update", "Выберите поле для изменения"),
                              fields, sizeof(fields) / sizeof(fields[0]), 0);
    if (choice < 0) {
        return;
    }

    if (prompt_input(state,
                     tr(state, "Edit Inode", "Редактирование inode"),
                     tr(state, "New numeric value:", "Новое числовое значение:"),
                     value_input, sizeof(value_input), "",
                     choice == 6 ? inode_flag_hint(state)
                                 : tr(state, "Examples: decimal or 0x-prefixed hex",
                                            "Примеры: десятичное число или hex с префиксом 0x")) != 0) {
        return;
    }

    if (str_to_u32(value_input, &value_u32) != 0) {
        show_notice(state, "Edit Inode", "Редактирование inode",
                    "Invalid numeric value.", "Некорректное числовое значение.");
        return;
    }

    memset(&request, 0, sizeof(request));
    request.target = METADATA_TARGET_INODE;
    request.as.inode.inode_no = inode_no;

    if (choice == 0) {
        if (value_u32 > 0xffffU) {
            show_notice(state, "Edit Inode", "Редактирование inode",
                        "Mode must be <= 65535.", "Режим должен быть не больше 65535.");
            return;
        }
        request.as.inode.fields.set_mode = true;
        request.as.inode.fields.mode = (uint16_t)value_u32;
    } else if (choice == 1) {
        request.as.inode.fields.set_uid = true;
        request.as.inode.fields.uid = value_u32;
    } else if (choice == 2) {
        request.as.inode.fields.set_gid = true;
        request.as.inode.fields.gid = value_u32;
    } else if (choice == 3) {
        request.as.inode.fields.set_atime = true;
        request.as.inode.fields.atime = value_u32;
    } else if (choice == 4) {
        request.as.inode.fields.set_ctime = true;
        request.as.inode.fields.ctime = value_u32;
    } else if (choice == 5) {
        request.as.inode.fields.set_mtime = true;
        request.as.inode.fields.mtime = value_u32;
    } else if (choice == 6) {
        request.as.inode.fields.set_flags = true;
        request.as.inode.fields.flags = value_u32;
    }

    if (metadata_editor_apply(ctx, super, &request, &result, err, sizeof(err)) != 0) {
        show_error_text(state, "Edit Inode", "Редактирование inode", err);
        return;
    }

    ui_clear(&ui);
    ui_add_line(&ui, "%s", result.summary);
    if (result.backup_created) {
        ui_add_line(&ui, "%s %s", tr(state, "Backup:", "Резервная копия:"), result.backup_path);
    }
    show_buffer_dialog(state, tr(state, "Edit Inode", "Редактирование inode"), &ui);
}

static void show_filesystem_statistics(const DashboardState *state,
                                       const Ext4Context *ctx,
                                       const Ext4SuperView *super) {
    UiBuffer ui;
    uint32_t groups;
    uint32_t i;
    uint64_t free_blocks_total = 0;
    uint64_t free_inodes_total = 0;
    uint64_t used_dirs_total = 0;
    uint64_t used_blocks;
    uint64_t used_inodes;
    uint64_t total_bytes;
    uint64_t offsets[128];
    size_t backups;
    unsigned int errors = 0;
    char bytes_buf[32];

    groups = ext4_group_count(super);
    for (i = 0; i < groups; i++) {
        Ext4GroupDescView group;
        char err[256];

        if (ext4_read_group_desc(ctx, super, i, &group, err, sizeof(err)) != 0) {
            errors++;
            continue;
        }

        free_blocks_total += group.free_blocks_count;
        free_inodes_total += group.free_inodes_count;
        used_dirs_total += group.used_dirs_count;
    }

    used_blocks = (super->blocks_count >= free_blocks_total) ? (super->blocks_count - free_blocks_total) : 0;
    used_inodes = (super->inodes_count >= free_inodes_total) ? (super->inodes_count - free_inodes_total) : 0;
    total_bytes = super->blocks_count * (uint64_t)super->block_size;
    format_bytes(total_bytes, bytes_buf, sizeof(bytes_buf));
    backups = ext4_collect_backup_super_offsets(super, ctx->image_size, offsets, 128);

    ui_clear(&ui);
    ui_add_line(&ui, "%s %s", tr(state, "Image:", "Образ:"), ctx->image_path);
    ui_add_line(&ui, "%s %u", tr(state, "Block groups:", "Групп блоков:"), groups);
    ui_add_line(&ui, "%s %llu (%s)", tr(state, "Capacity:", "Ёмкость:"),
                (unsigned long long)total_bytes, bytes_buf);
    ui_add_line(&ui, "%s %llu / %llu (%.2f%%)",
                tr(state, "Blocks used:", "Использовано блоков:"),
                (unsigned long long)used_blocks,
                (unsigned long long)super->blocks_count,
                ratio_percent(used_blocks, super->blocks_count));
    ui_add_line(&ui, "%s %llu / %u (%.2f%%)",
                tr(state, "Inodes used:", "Использовано inode:"),
                (unsigned long long)used_inodes,
                super->inodes_count,
                ratio_percent(used_inodes, super->inodes_count));
    ui_add_line(&ui, "%s %llu", tr(state, "Directories tracked:", "Каталогов по группам:"),
                (unsigned long long)used_dirs_total);
    ui_add_line(&ui, "%s %zu", tr(state, "Backup superblock slots:", "Резервных суперблоков:"),
                backups);
    ui_add_line(&ui, "%s %u", tr(state, "Descriptor read errors:", "Ошибок чтения дескрипторов:"),
                errors);
    show_buffer_dialog(state, tr(state, "Filesystem Statistics", "Статистика файловой системы"), &ui);
}

static void show_root_directory_statistics(const DashboardState *state,
                                           const Ext4Context *ctx,
                                           const Ext4SuperView *super) {
    UiBuffer ui;
    DirEntryView *entries;
    size_t count = 0;
    size_t i;
    size_t regular_count = 0;
    size_t dir_count = 0;
    size_t symlink_count = 0;
    size_t other_count = 0;
    size_t hidden_count = 0;
    size_t longest_name = 0;
    size_t total_name_len = 0;
    size_t visible_entries = 0;
    char err[256];

    entries = calloc(MAX_SCAN_ENTRIES, sizeof(*entries));
    if (!entries) {
        show_notice(state, "Root Directory Statistics", "Статистика корневого каталога",
                    "Out of memory.", "Недостаточно памяти.");
        return;
    }

    if (ext4_list_dir_entries(ctx, super, 2, entries, MAX_SCAN_ENTRIES, &count, err, sizeof(err)) != 0) {
        free(entries);
        show_error_text(state, "Root Directory Statistics", "Статистика корневого каталога", err);
        return;
    }

    for (i = 0; i < count; i++) {
        size_t name_len;

        if (strcmp(entries[i].name, ".") == 0 || strcmp(entries[i].name, "..") == 0) {
            continue;
        }

        visible_entries++;
        name_len = strlen(entries[i].name);
        total_name_len += name_len;
        if (name_len > longest_name) {
            longest_name = name_len;
        }

        if (entries[i].name[0] == '.') {
            hidden_count++;
        }

        if (entries[i].file_type == EXT4_FT_REG_FILE) {
            regular_count++;
        } else if (entries[i].file_type == EXT4_FT_DIR) {
            dir_count++;
        } else if (entries[i].file_type == EXT4_FT_SYMLINK) {
            symlink_count++;
        } else {
            other_count++;
        }
    }

    ui_clear(&ui);
    ui_add_line(&ui, "%s %zu", tr(state, "Total entries:", "Всего записей:"), visible_entries);
    ui_add_line(&ui, "%s %zu", tr(state, "Regular files:", "Обычных файлов:"), regular_count);
    ui_add_line(&ui, "%s %zu", tr(state, "Directories:", "Каталогов:"), dir_count);
    ui_add_line(&ui, "%s %zu", tr(state, "Symlinks:", "Ссылок:"), symlink_count);
    ui_add_line(&ui, "%s %zu", tr(state, "Other entries:", "Прочих записей:"), other_count);
    ui_add_line(&ui, "%s %zu", tr(state, "Hidden names:", "Скрытых имён:"), hidden_count);
    ui_add_line(&ui, "%s %zu", tr(state, "Longest name length:", "Максимальная длина имени:"),
                longest_name);
    if (visible_entries > 0) {
        ui_add_line(&ui, "%s %.2f",
                    tr(state, "Average name length:", "Средняя длина имени:"),
                    (double)total_name_len / (double)visible_entries);
    }

    free(entries);
    show_buffer_dialog(state, tr(state, "Root Directory Statistics", "Статистика корневого каталога"), &ui);
}

static void choose_language(DashboardState *state) {
    const MenuEntry items[] = {
        { "English", "English", "Interface in English", "Интерфейс на английском" },
        { "Russian", "Русский", "Interface in Russian", "Интерфейс на русском" },
    };
    int choice;

    choice = choose_from_list(state,
                              tr(state, "Language", "Язык"),
                              tr(state, "Choose interface language", "Выберите язык интерфейса"),
                              items, sizeof(items) / sizeof(items[0]),
                              state->lang == APP_LANG_RU ? 1 : 0);
    if (choice == 0) {
        state->lang = APP_LANG_EN;
    } else if (choice == 1) {
        state->lang = APP_LANG_RU;
    }
}

static void show_help(const DashboardState *state) {
    UiBuffer ui;

    ui_clear(&ui);
    ui_add_line(&ui, "%s", tr(state, "Keyboard navigation", "Навигация по клавиатуре"));
    ui_add_line(&ui, "  Up/Down  - %s",
                tr(state, "move in menus", "движение по меню"));
    ui_add_line(&ui, "  Enter    - %s",
                tr(state, "open selected action", "запуск выбранного действия"));
    ui_add_line(&ui, "  Esc / q  - %s",
                tr(state, "return or exit", "назад или выход"));
    ui_add_line(&ui, "  PgUp/PgDn - %s",
                tr(state, "scroll report windows", "прокрутка окон с отчётами"));
    ui_add_line(&ui, "");
    ui_add_line(&ui, "%s", tr(state, "Main sections", "Основные разделы"));
    ui_add_line(&ui, "  %s",
                tr(state, "Metadata inspection: superblock, groups, inode",
                           "Просмотр метаданных: суперблок, группы, inode"));
    ui_add_line(&ui, "  %s",
                tr(state, "Navigation tools: list directories and search",
                           "Навигация: список каталогов и поиск"));
    ui_add_line(&ui, "  %s",
                tr(state, "Safe editing: superblock and inode fields",
                           "Безопасное редактирование: поля суперблока и inode"));
    ui_add_line(&ui, "  %s",
                tr(state, "Statistics: filesystem summary and root directory profile",
                           "Статистика: сводка ФС и профиль корневого каталога"));
    ui_add_line(&ui, "");
    ui_add_line(&ui, "%s",
                tr(state,
                   "Each write operation creates a backup image before applying changes.",
                   "Перед каждой операцией записи создаётся резервная копия образа."));
    show_buffer_dialog(state, tr(state, "Help", "Справка"), &ui);
}

static int show_main_menu(DashboardState *state,
                          const Ext4Context *ctx,
                          const Ext4SuperView *super,
                          const MenuEntry *items,
                          size_t item_count,
                          int initial_selection) {
    int rows;
    int cols;
    int body_y = 6;
    int body_h;
    int left_w;
    int right_x;
    int right_w;
    int selected = initial_selection;
    int top = 0;
    int ch;
    char image_buf[96];
    char size_buf[32];

    if (!items || item_count == 0) {
        return -1;
    }

    while (1) {
        int i;
        int visible_items;

        getmaxyx(stdscr, rows, cols);
        body_h = rows - 9;
        left_w = cols >= 108 ? 36 : cols / 2 - 1;
        if (left_w < 28) {
            left_w = 28;
        }
        right_x = 3 + left_w;
        right_w = cols - right_x - 2;
        if (right_w < 24) {
            right_w = 24;
        }
        visible_items = body_h - 2;
        if (visible_items < 1) {
            visible_items = 1;
        }

        erase();
        apply_attr(stdscr, PAIR_HEADER, true);
        mvhline(0, 0, ' ', cols);
        mvprintw(0, 2, "EXT4 IMAGE LAB");
        mvprintw(0, cols - 24, "[%s]  [%s]",
                 mode_name(state, ctx),
                 language_name(state->lang));
        clear_attr(stdscr, PAIR_HEADER, true);

        mvprintw(2, 2, "%s",
                 tr(state,
                    "Interactive analyzer and safe editor for ext4 images",
                    "Интерактивный анализатор и безопасный редактор ext4-образов"));
        draw_context_summary(stdscr, state, ctx, super, 3);

        draw_frame(stdscr, body_y, 1, body_h, left_w, PAIR_FRAME, tr(state, "Operations", "Операции"));
        draw_frame(stdscr, body_y, right_x, body_h, right_w, PAIR_FRAME, tr(state, "Overview", "Обзор"));

        if (selected < top) {
            top = selected;
        }
        if (selected >= top + visible_items) {
            top = selected - visible_items + 1;
        }

        for (i = 0; i < visible_items; i++) {
            int idx = top + i;
            int y = body_y + 1 + i;
            if ((size_t)idx >= item_count) {
                break;
            }

            if (idx == selected) {
                apply_attr(stdscr, PAIR_ACTIVE, true);
                mvprintw(y, 3, " %-30s ", entry_label(state, &items[idx]));
                clear_attr(stdscr, PAIR_ACTIVE, true);
            } else {
                mvprintw(y, 3, " %-30s ", entry_label(state, &items[idx]));
            }
        }

        shorten_path(ctx->image_path, image_buf, sizeof(image_buf));
        format_bytes(super->blocks_count * (uint64_t)super->block_size, size_buf, sizeof(size_buf));
        apply_attr(stdscr, PAIR_ACCENT, true);
        print_fit(stdscr, body_y + 2, right_x + 2, right_w - 4, "%s",
                  entry_label(state, &items[selected]));
        clear_attr(stdscr, PAIR_ACCENT, true);
        print_fit(stdscr, body_y + 4, right_x + 2, right_w - 4, "%s",
                  entry_hint(state, &items[selected]));
        print_fit(stdscr, body_y + 6, right_x + 2, right_w - 4, "%s: %s",
                  tr(state, "Image", "Образ"), image_buf);
        print_fit(stdscr, body_y + 7, right_x + 2, right_w - 4, "%s: %s",
                  tr(state, "Volume", "Том"),
                  super->volume_name[0] ? super->volume_name : "-");
        print_fit(stdscr, body_y + 8, right_x + 2, right_w - 4, "%s: %u",
                  tr(state, "Block size", "Размер блока"), super->block_size);
        print_fit(stdscr, body_y + 9, right_x + 2, right_w - 4, "%s: %u",
                  tr(state, "Groups", "Группы"), ext4_group_count(super));
        print_fit(stdscr, body_y + 10, right_x + 2, right_w - 4, "%s: %s",
                  tr(state, "Approx. capacity", "Примерная ёмкость"), size_buf);

        apply_attr(stdscr, PAIR_ACCENT, false);
        mvhline(rows - 2, 0, ACS_HLINE, cols);
        print_fit(stdscr, rows - 1, 2, cols - 4, "%s",
                  tr(state,
                     "Enter open  |  Up/Down move  |  q or Esc exit",
                     "Enter открыть  |  Up/Down выбрать  |  q или Esc выход"));
        clear_attr(stdscr, PAIR_ACCENT, false);

        refresh();
        ch = getch();

        if (ch == KEY_UP || ch == 'k' || ch == 'K') {
            if (selected > 0) {
                selected--;
            }
        } else if (ch == KEY_DOWN || ch == 'j' || ch == 'J') {
            if ((size_t)selected + 1 < item_count) {
                selected++;
            }
        } else if (ch == KEY_NPAGE) {
            selected += visible_items;
            if ((size_t)selected >= item_count) {
                selected = (int)item_count - 1;
            }
        } else if (ch == KEY_PPAGE) {
            selected -= visible_items;
            if (selected < 0) {
                selected = 0;
            }
        } else if (ch == '\n' || ch == KEY_ENTER || ch == 10 || ch == 13) {
            return selected;
        } else if (ch == 27 || ch == 'q' || ch == 'Q') {
            return -1;
        }
    }
}

int dashboard_run(Ext4Context *ctx, Ext4SuperView *super, AppLanguage initial_language) {
    const MenuEntry main_items[] = {
        { "Superblock View", "Просмотр суперблока", "Inspect main ext4 superblock fields", "Показать основные поля ext4 superblock" },
        { "Backup Superblocks", "Резервные суперблоки", "List candidate backup copies", "Список резервных копий superblock" },
        { "Group Descriptor", "Дескриптор группы", "Inspect one block group descriptor", "Посмотреть один дескриптор группы" },
        { "Inode View", "Просмотр inode", "Inspect inode metadata", "Посмотреть метаданные inode" },
        { "Directory Browser", "Просмотр каталога", "List entries for path or inode", "Список записей для пути или inode" },
        { "Resolve Path", "Поиск по пути", "Resolve absolute path to inode", "Найти inode по абсолютному пути" },
        { "Resolve Inode", "Поиск по inode", "Resolve inode number to path", "Найти путь по номеру inode" },
        { "Search By Name", "Поиск по имени", "Find first entry by name", "Найти первый объект по имени" },
        { "Edit Superblock", "Изменить суперблок", "Safe superblock metadata edit", "Безопасное редактирование суперблока" },
        { "Edit Inode", "Изменить inode", "Safe inode metadata edit", "Безопасное редактирование inode" },
        { "Filesystem Statistics", "Статистика ФС", "Aggregate usage summary across groups", "Сводная статистика по группам" },
        { "Root Directory Statistics", "Статистика корня", "Simple profile of the root directory", "Простой профиль корневого каталога" },
        { "Language", "Язык", "Switch UI language", "Сменить язык интерфейса" },
        { "Help", "Справка", "Show keyboard and feature guide", "Показать клавиши и возможности" },
        { "Exit", "Выход", "Quit the program", "Завершить программу" },
    };
    DashboardState state;
    int selected = 0;
    int choice;

    setlocale(LC_ALL, "");
    memset(&state, 0, sizeof(state));
    state.lang = initial_language;

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    init_palette(&state);

    while (1) {
        choice = show_main_menu(&state, ctx, super,
                                main_items, sizeof(main_items) / sizeof(main_items[0]), selected);
        if (choice < 0 || choice == 14) {
            break;
        }

        selected = choice;
        if (choice == 0) {
            show_superblock_view(&state, super);
        } else if (choice == 1) {
            show_backup_superblocks(&state, ctx, super);
        } else if (choice == 2) {
            show_group_descriptor(&state, ctx, super);
        } else if (choice == 3) {
            show_inode_view(&state, ctx, super);
        } else if (choice == 4) {
            show_directory_listing(&state, ctx, super);
        } else if (choice == 5) {
            show_find_by_path(&state, ctx, super);
        } else if (choice == 6) {
            show_find_by_inode(&state, ctx, super);
        } else if (choice == 7) {
            show_find_by_name(&state, ctx, super);
        } else if (choice == 8) {
            edit_super_metadata(&state, ctx, super);
        } else if (choice == 9) {
            edit_inode_metadata(&state, ctx, super);
        } else if (choice == 10) {
            show_filesystem_statistics(&state, ctx, super);
        } else if (choice == 11) {
            show_root_directory_statistics(&state, ctx, super);
        } else if (choice == 12) {
            choose_language(&state);
        } else if (choice == 13) {
            show_help(&state);
        }
    }

    endwin();
    return 0;
}
