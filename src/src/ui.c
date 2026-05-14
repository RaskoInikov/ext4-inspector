/*
 * ui.c - ncurses menu-driven interface for fs_tool
 *
 * Layout: status bar (top) | content area (middle) | help bar (bottom)
 * Navigation: arrow keys to move, Enter to select, q/Esc to go back/quit.
 *
 * Menus covered:
 *   Main Menu
 *     1. Filesystem Navigation
 *        - Analyze path (POSIX lstat)
 *        - Directory tree
 *        - Disk usage
 *        - File stat
 *     2. Ext4 Raw Directory
 *        - List directory (ext4-ls)
 *        - Walk directory (ext4-walk)
 *        - Find by name
 *        - Find by inode number
 *        - Find corrupt entries
 *     3. Inode & Block Chain Analysis
 *        - Inode chain for path
 *        - Shared block detection
 *        - Full corruption scan
 *     4. Backup Superblock Inspection
 *        - Discover backups
 *        - Compare primary vs backups
 *        - Restore preview (readonly)
 *        - Restore write (guarded)
 *     5. Superblock Info
 */

#include "ui.h"

#include "analyzer.h"
#include "ext4_backup.h"
#include "ext4_chain.h"
#include "ext4_dir.h"
#include "ext4_inode.h"
#include "ext4_path.h"
#include "ext4_recovery.h"
#include "ext4_super.h"
#include "editor.h"
#include <unistd.h>
#include <curses.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Constants                                                            */
/* ------------------------------------------------------------------ */

#define UI_MAX_LINES   4096
#define UI_MAX_LINE    512
#define UI_INPUT_MAX   256
#define UI_MENU_MAX    16

/* Colour pair IDs */
#define CP_NORMAL   1
#define CP_STATUS   2
#define CP_HELP     3
#define CP_TITLE    4
#define CP_SELECT   5
#define CP_WARN     6
#define CP_OK       7

/* ------------------------------------------------------------------ */
/* Output capture: redirect printf output into a string buffer          */
/* ------------------------------------------------------------------ */

typedef struct {
    char lines[UI_MAX_LINES][UI_MAX_LINE];
    int  count;
} capture_t;

static capture_t g_cap;
static FILE     *g_orig_stdout = NULL;
static FILE     *g_pipe_read   = NULL;
static int       g_pipe_fds[2] = {-1, -1};

static void cap_begin(void)
{
    g_cap.count = 0;
    fflush(stdout);
    if (pipe(g_pipe_fds) != 0) {
        return;
    }
    g_orig_stdout = stdout;
    /* Redirect stdout to write end of pipe */
    stdout = fdopen(g_pipe_fds[1], "w");
    if (stdout == NULL) {
        stdout = g_orig_stdout;
        close(g_pipe_fds[0]);
        close(g_pipe_fds[1]);
        g_pipe_fds[0] = g_pipe_fds[1] = -1;
    }
}

static void cap_end(void)
{
    char buf[UI_MAX_LINE];

    if (g_orig_stdout == NULL) {
        return;
    }

    fflush(stdout);
    fclose(stdout);              /* closes write end */
    stdout = g_orig_stdout;
    g_orig_stdout = NULL;

    g_pipe_read = fdopen(g_pipe_fds[0], "r");
    if (g_pipe_read == NULL) {
        close(g_pipe_fds[0]);
        g_pipe_fds[0] = g_pipe_fds[1] = -1;
        return;
    }

    while (fgets(buf, (int)sizeof(buf), g_pipe_read) != NULL &&
           g_cap.count < UI_MAX_LINES) {
        /* Strip trailing newline */
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
        }
        strncpy(g_cap.lines[g_cap.count], buf, UI_MAX_LINE - 1);
        g_cap.lines[g_cap.count][UI_MAX_LINE - 1] = '\0';
        ++g_cap.count;
    }

    fclose(g_pipe_read);
    g_pipe_read = NULL;
    g_pipe_fds[0] = g_pipe_fds[1] = -1;
}

/* ------------------------------------------------------------------ */
/* Terminal / ncurses helpers                                           */
/* ------------------------------------------------------------------ */

static int g_rows = 24;
static int g_cols = 80;

static void ui_resize(void)
{
    endwin();
    refresh();
    getmaxyx(stdscr, g_rows, g_cols);
    if (g_rows < 8) g_rows = 8;
    if (g_cols < 20) g_cols = 20;
}

static void ui_init_colors(void)
{
    if (!has_colors()) {
        return;
    }
    start_color();
    use_default_colors();
    init_pair(CP_NORMAL, COLOR_WHITE,   -1);
    init_pair(CP_STATUS, COLOR_BLACK,   COLOR_CYAN);
    init_pair(CP_HELP,   COLOR_BLACK,   COLOR_WHITE);
    init_pair(CP_TITLE,  COLOR_CYAN,    -1);
    init_pair(CP_SELECT, COLOR_BLACK,   COLOR_GREEN);
    init_pair(CP_WARN,   COLOR_RED,     -1);
    init_pair(CP_OK,     COLOR_GREEN,   -1);
}

static void ui_draw_bar(int row, const char *text, int pair)
{
    int col;

    if (row < 0 || row >= g_rows) {
        return;
    }

    attron(COLOR_PAIR(pair));
    move(row, 0);
    clrtoeol();
    mvprintw(row, 0, "%.*s", g_cols, text);
    /* Pad remainder */
    for (col = (int)strlen(text); col < g_cols; ++col) {
        addch(' ');
    }
    attroff(COLOR_PAIR(pair));
}

/* Prompt user for a string; returns 0 on success, 1 on cancel/Esc. */
static int ui_prompt(const char *label, char *out, size_t out_size)
{
    int ch;
    size_t pos = 0;
    int row = g_rows - 2;
    char display[UI_INPUT_MAX + 64];

    if (row < 1) {
        row = 1;
    }

    out[0] = '\0';
    echo();
    curs_set(1);
    nodelay(stdscr, FALSE);

    for (;;) {
        snprintf(display, sizeof(display), "%s: %.*s", label,
                 (int)(out_size - 1), out);
        move(row, 0);
        clrtoeol();
        attron(COLOR_PAIR(CP_HELP));
        mvprintw(row, 0, "%.*s", g_cols, display);
        attroff(COLOR_PAIR(CP_HELP));
        move(row, (int)(strlen(label) + 2 + pos));
        refresh();

        ch = getch();
        if (ch == 27 || ch == KEY_F(10)) {         /* Esc / F10 = cancel */
            noecho();
            curs_set(0);
            return 1;
        }
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            break;
        }
        if ((ch == KEY_BACKSPACE || ch == 127 || ch == '\b') && pos > 0) {
            out[--pos] = '\0';
            continue;
        }
        if (ch >= 32 && ch < 127 && pos + 1 < out_size) {
            out[pos++] = (char)ch;
            out[pos]   = '\0';
        }
    }

    noecho();
    curs_set(0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Scrollable content pager                                             */
/* ------------------------------------------------------------------ */

static void ui_pager(const char *title, const capture_t *cap)
{
    int top   = 0;
    int ch;
    int content_rows;
    int i;

    content_rows = g_rows - 3;   /* status + help bars */
    if (content_rows < 1) {
        content_rows = 1;
    }

    for (;;) {
        char status[128];

        snprintf(status, sizeof(status),
                 " fs_tool | %.*s  [%d lines]", 50, title, cap->count);
        ui_draw_bar(0, status, CP_STATUS);

        /* Content */
        for (i = 0; i < content_rows; ++i) {
            int line_idx = top + i;
            move(i + 1, 0);
            clrtoeol();
            if (line_idx < cap->count) {
                const char *line = cap->lines[line_idx];
                /* Colour warnings/errors */
                if (strstr(line, "CORRUPT") || strstr(line, "corrupt") ||
                    strstr(line, "shared") || strstr(line, "ERROR")) {
                    attron(COLOR_PAIR(CP_WARN));
                } else if (strstr(line, "ok") || strstr(line, "verified") ||
                           strstr(line, "match")) {
                    attron(COLOR_PAIR(CP_OK));
                }
                mvprintw(i + 1, 0, "%.*s", g_cols, line);
                attroff(COLOR_PAIR(CP_WARN));
                attroff(COLOR_PAIR(CP_OK));
            }
        }

        ui_draw_bar(g_rows - 1,
                    " UP/DOWN/PGUP/PGDN: scroll   q/ESC: back",
                    CP_HELP);
        refresh();

        ch = getch();
        if (ch == KEY_RESIZE) {
            ui_resize();
            content_rows = g_rows - 3;
            if (content_rows < 1) content_rows = 1;
            continue;
        }
        if (ch == 'q' || ch == 'Q' || ch == 27) {
            break;
        }
        if (ch == KEY_DOWN || ch == 'j') {
            if (top + content_rows < cap->count) {
                ++top;
            }
        }
        if (ch == KEY_UP || ch == 'k') {
            if (top > 0) {
                --top;
            }
        }
        if (ch == KEY_NPAGE || ch == ' ') {
            top += content_rows;
            if (top + content_rows > cap->count) {
                top = cap->count - content_rows;
            }
            if (top < 0) top = 0;
        }
        if (ch == KEY_PPAGE) {
            top -= content_rows;
            if (top < 0) top = 0;
        }
        if (ch == KEY_HOME || ch == 'g') {
            top = 0;
        }
        if (ch == KEY_END || ch == 'G') {
            top = cap->count - content_rows;
            if (top < 0) top = 0;
        }
    }
}

/* Show a single short message to the user, wait for keypress. */
static void ui_message(const char *msg)
{
    capture_t cap;
    int i = 0;
    const char *start = msg;
    const char *end;

    cap.count = 0;
    while (*start != '\0' && cap.count < UI_MAX_LINES) {
        end = strchr(start, '\n');
        if (end == NULL) {
            strncpy(cap.lines[i], start, UI_MAX_LINE - 1);
            cap.lines[i][UI_MAX_LINE - 1] = '\0';
            ++cap.count;
            break;
        }
        size_t len = (size_t)(end - start);
        if (len >= UI_MAX_LINE) len = UI_MAX_LINE - 1;
        strncpy(cap.lines[i], start, len);
        cap.lines[i][len] = '\0';
        ++cap.count;
        ++i;
        start = end + 1;
    }
    ui_pager("Message", &cap);
}

/* ------------------------------------------------------------------ */
/* Menu                                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *label;
    void      (*action)(void);
} menu_item_t;

/* Run a simple vertical menu; returns selected index or -1 on cancel. */
static int ui_menu_run(const char *title,
                       const menu_item_t *items, int count)
{
    int sel = 0;
    int ch;
    int i;

    for (;;) {
        int content_rows = g_rows - 3;

        erase();

        ui_draw_bar(0,
                    " fs_tool | Ext4 Filesystem Analyzer & Editor",
                    CP_STATUS);

        /* Title */
        move(1, 0);
        clrtoeol();

        attron(COLOR_PAIR(CP_TITLE) | A_BOLD);
        mvprintw(1, 2, "%.*s", g_cols - 4, title);
        attroff(COLOR_PAIR(CP_TITLE) | A_BOLD);

        /* Menu items */
        for (i = 0; i < count && i + 2 < content_rows + 2; ++i) {

            move(i + 3, 0);
            clrtoeol();

            if (i == sel) {
                attron(COLOR_PAIR(CP_SELECT) | A_BOLD);
                mvprintw(i + 3, 4, "  %-*s  ", g_cols - 12, items[i].label);
                attroff(COLOR_PAIR(CP_SELECT) | A_BOLD);
            } else {
                move(i + 3, 4);
                clrtoeol();
                mvprintw(i + 3, 4, "  %s", items[i].label);
            }
        }

        /* Clear remaining rows */
        for (; i + 3 < g_rows - 1; ++i) {
            move(i + 3, 0);
            clrtoeol();
        }

        ui_draw_bar(g_rows - 1,
                    " UP/DOWN: navigate   ENTER: select   q/ESC: back",
                    CP_HELP);
        refresh();

        ch = getch();
        if (ch == KEY_RESIZE) {
            ui_resize();
            continue;
        }
        if (ch == 'q' || ch == 'Q' || ch == 27) {
            return -1;
        }
        if (ch == KEY_UP || ch == 'k') {
            if (sel > 0) --sel;
        }
        if (ch == KEY_DOWN || ch == 'j') {
            if (sel < count - 1) ++sel;
        }
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            return sel;
        }
    }
}

/* Run a menu in a loop until the user cancels. */
static void ui_menu_loop(const char *title,
                         const menu_item_t *items, int count)
{
    int sel;

    for (;;) {
        sel = ui_menu_run(title, items, count);
        if (sel < 0 || sel >= count) {
            return;
        }
        if (items[sel].action != NULL) {
            items[sel].action();
        }
    }
}

/* ------------------------------------------------------------------ */
/* Helpers: run a captured operation and show pager                     */
/* ------------------------------------------------------------------ */

static void run_and_show(const char *title,
                         int (*fn)(const char *), const char *arg)
{
    int rc;

    cap_begin();
    rc = fn(arg);
    cap_end();

    if (g_cap.count == 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "%s: %s",
                 title, rc == 0 ? "(no output)" : "operation failed");
        ui_message(msg);
        return;
    }

    ui_pager(title, &g_cap);
}

static void run_and_show2(const char *title,
                          int (*fn)(const char *, const char *),
                          const char *a, const char *b)
{
    cap_begin();
    fn(a, b);
    cap_end();
    ui_pager(title, &g_cap);
}

static void run_and_show3(const char *title,
                          int (*fn)(const char *, const char *, const char *),
                          const char *a, const char *b, const char *c)
{
    cap_begin();
    fn(a, b, c);
    cap_end();
    ui_pager(title, &g_cap);
}

/* ------------------------------------------------------------------ */
/* Reusable prompt + run patterns                                        */
/* ------------------------------------------------------------------ */

static void prompt_path_and_run1(const char *title,
                                  int (*fn)(const char *))
{
    char path[UI_INPUT_MAX];

    if (ui_prompt("Path", path, sizeof(path)) != 0 || path[0] == '\0') {
        return;
    }
    run_and_show(title, fn, path);
}

static void prompt_image_and_run1(const char *title,
                                   int (*fn)(const char *))
{
    char image[UI_INPUT_MAX];

    if (ui_prompt("Image/Device", image, sizeof(image)) != 0 ||
        image[0] == '\0') {
        return;
    }
    run_and_show(title, fn, image);
}

static void prompt_image_path_and_run(const char *title,
                                       int (*fn)(const char *, const char *))
{
    char image[UI_INPUT_MAX];
    char path[UI_INPUT_MAX];

    if (ui_prompt("Image/Device", image, sizeof(image)) != 0 ||
        image[0] == '\0') {
        return;
    }
    if (ui_prompt("Ext4 Path (e.g. /)", path, sizeof(path)) != 0 ||
        path[0] == '\0') {
        return;
    }
    run_and_show2(title, fn, image, path);
}

/* ------------------------------------------------------------------ */
/* Section 1: Filesystem Navigation                                      */
/* ------------------------------------------------------------------ */

static void action_analyze(void)
{
    prompt_path_and_run1("Analyze Path", analyzer_analyze);
}

static void action_tree(void)
{
    prompt_path_and_run1("Directory Tree", analyzer_tree);
}

static void action_du(void)
{
    prompt_path_and_run1("Disk Usage", analyzer_du);
}

static void action_stat(void)
{
    char path[UI_INPUT_MAX];

    if (ui_prompt("File path", path, sizeof(path)) != 0 || path[0] == '\0') {
        return;
    }
    cap_begin();
    editor_stat(path);
    cap_end();
    ui_pager("File Stat", &g_cap);
}

static const menu_item_t menu_nav[] = {
    {"Analyze path (lstat metadata)",  action_analyze},
    {"Directory tree",                 action_tree},
    {"Disk usage (recursive)",         action_du},
    {"File stat (follow symlinks)",    action_stat},
};

static void submenu_nav(void)
{
    ui_menu_loop("Filesystem Navigation",
                 menu_nav, (int)(sizeof(menu_nav) / sizeof(menu_nav[0])));
}

/* ------------------------------------------------------------------ */
/* Section 2: Ext4 Raw Directory                                         */
/* ------------------------------------------------------------------ */

static void action_ext4_ls(void)
{
    prompt_image_path_and_run("Ext4 List Directory", analyzer_ext4_ls);
}

static void action_ext4_walk(void)
{
    prompt_image_path_and_run("Ext4 Walk Directory", analyzer_ext4_walk);
}

static void action_ext4_find_name(void)
{
    char image[UI_INPUT_MAX];
    char path[UI_INPUT_MAX];
    char name[UI_INPUT_MAX];

    if (ui_prompt("Image/Device", image, sizeof(image)) != 0 ||
        image[0] == '\0') return;
    if (ui_prompt("Start path", path, sizeof(path)) != 0 ||
        path[0] == '\0') return;
    if (ui_prompt("Filename to find", name, sizeof(name)) != 0 ||
        name[0] == '\0') return;

    run_and_show3("Ext4 Find by Name",
                  analyzer_ext4_find_name, image, path, name);
}

static void action_ext4_find_inode(void)
{
    char image[UI_INPUT_MAX];
    char path[UI_INPUT_MAX];
    char ino[UI_INPUT_MAX];

    if (ui_prompt("Image/Device", image, sizeof(image)) != 0 ||
        image[0] == '\0') return;
    if (ui_prompt("Start path", path, sizeof(path)) != 0 ||
        path[0] == '\0') return;
    if (ui_prompt("Inode number", ino, sizeof(ino)) != 0 ||
        ino[0] == '\0') return;

    run_and_show3("Ext4 Find by Inode",
                  analyzer_ext4_find_inode, image, path, ino);
}

static void action_ext4_find_corrupt(void)
{
    prompt_image_path_and_run("Ext4 Find Corrupt Entries",
                              analyzer_ext4_find_corrupt);
}

static void action_ext4_inode_info(void)
{
    /* Show inode metadata by path in an ext4 image */
    char image[UI_INPUT_MAX];
    char path[UI_INPUT_MAX];
    struct ext4_fs fs;
    uint32_t ino;
    struct ext4_inode inode;

    if (ui_prompt("Image/Device", image, sizeof(image)) != 0 ||
        image[0] == '\0') return;
    if (ui_prompt("Ext4 path", path, sizeof(path)) != 0 ||
        path[0] == '\0') return;

    cap_begin();

    if (ext4_fs_open(image, 1, &fs) == 0) {
        if (ext4_resolve_path(&fs, EXT4_ROOT_INO, path, &ino) == 0 &&
            ext4_read_inode(&fs, ino, &inode) == 0) {
            printf("Inode:        %" PRIu32 "\n", inode.inode_number);
            printf("Type:         %s\n",  ext4_inode_type_name(inode.type));
            printf("Mode:         %04o\n", inode.permissions);
            printf("UID:          %" PRIu32 "\n", inode.uid);
            printf("GID:          %" PRIu32 "\n", inode.gid);
            printf("Size:         %" PRIu64 " bytes\n", inode.size);
            printf("Links:        %" PRIu16 "\n", inode.links_count);
            printf("Blocks:       %" PRIu64 "\n", inode.blocks_count);
            printf("Flags:        0x%08" PRIx32 "\n", inode.flags);
            printf("atime:        %" PRIu32 "\n", inode.atime);
            printf("mtime:        %" PRIu32 "\n", inode.mtime);
            printf("ctime:        %" PRIu32 "\n", inode.ctime);
            printf("dtime:        %" PRIu32 "\n", inode.dtime);
        }
        ext4_fs_close(&fs, image);
    }

    cap_end();
    ui_pager("Inode Metadata", &g_cap);
}

static const menu_item_t menu_ext4dir[] = {
    {"List directory (ext4-ls)",          action_ext4_ls},
    {"Walk directory tree (ext4-walk)",   action_ext4_walk},
    {"Find by filename",                  action_ext4_find_name},
    {"Find by inode number",              action_ext4_find_inode},
    {"Find corrupt entries",              action_ext4_find_corrupt},
    {"Show inode metadata",               action_ext4_inode_info},
};

static void submenu_ext4dir(void)
{
    ui_menu_loop("Ext4 Raw Directory",
                 menu_ext4dir,
                 (int)(sizeof(menu_ext4dir) / sizeof(menu_ext4dir[0])));
}

/* ------------------------------------------------------------------ */
/* Section 3: Inode & Block Chain Analysis                              */
/* ------------------------------------------------------------------ */

static void action_chain_path(void)
{
    prompt_image_path_and_run("Block Chain for Path", analyzer_ext4_chain);
}

static void action_shared_blocks(void)
{
    prompt_image_and_run1("Shared Block Detection",
                          analyzer_ext4_shared_blocks);
}

static void action_corruption_scan(void)
{
    prompt_image_and_run1("Full Corruption Scan",
                          analyzer_ext4_corruption_scan);
}

/*
 * Chain visualization: show block chain with ASCII art relationships.
 * Detects shared (intermediate) blocks used by multiple inodes and
 * draws a tree illustrating the sharing.
 *
 * Example output:
 *   inode 12 ──── blk 100 (direct)
 *   inode 13 ──┐
 *              ├── blk 200 (shared) ──── blk 300 (direct via inode 12)
 *   inode 14 ──┘                    └── blk 301 (direct via inode 13)
 */
static void action_chain_viz(void)
{
    char image[UI_INPUT_MAX];
    struct ext4_fs fs;
    struct ext4_chain_analysis analysis;
    size_t i, j, k;

    if (ui_prompt("Image/Device", image, sizeof(image)) != 0 ||
        image[0] == '\0') return;

    cap_begin();

    if (ext4_fs_open(image, 1, &fs) != 0) {
        cap_end();
        ui_message("Failed to open image.");
        return;
    }

    if (ext4_build_chain_analysis(&fs, &analysis) != 0) {
        printf("Chain analysis failed.\n");
    } else {
        int shared_found = 0;

        printf("Block Chain Visualization\n");
        printf("=========================\n\n");
        printf("Inodes analyzed : %zu\n", analysis.chain_count);
        printf("Blocks indexed  : %zu\n", analysis.owner_count);
        printf("Corruptions     : %d\n\n", analysis.corruption_count);

        /* Find shared blocks and show the sharing graph */
        for (i = 0U; i < analysis.owner_count; ++i) {
            const struct ext4_block_owner *owner = &analysis.owners[i];

            if (owner->owner_count < 2U) {
                continue;
            }
            shared_found = 1;

            printf("[SHARED] block %-8" PRIu64 "  owners:", owner->block_index);
            for (j = 0U; j < owner->owner_count; ++j) {
                printf(" inode%-4" PRIu32, owner->inode_numbers[j]);
            }
            printf("\n");

            /* For each owning inode show its full chain */
            for (j = 0U; j < owner->owner_count && j < 4U; ++j) {
                uint32_t ino = owner->inode_numbers[j];
                struct ext4_inode_chain chain;
                char prefix[64];

                if (j == 0U) {
                    snprintf(prefix, sizeof(prefix), "  inode %-6" PRIu32 " ──", ino);
                } else if (j == owner->owner_count - 1U) {
                    snprintf(prefix, sizeof(prefix), "  inode %-6" PRIu32 " ──┘", ino);
                } else {
                    snprintf(prefix, sizeof(prefix), "  inode %-6" PRIu32 " ──┤", ino);
                }

                if (ext4_build_chain_for_inode(&fs, ino, &chain) == 0) {
                    int printed = 0;
                    for (k = 0U; k < chain.ref_count && k < 6U; ++k) {
                        if (chain.refs[k].block_index == owner->block_index) {
                            if (!printed) {
                                printf("%s── blk %-8" PRIu64 " (shared)\n",
                                       prefix, owner->block_index);
                                printed = 1;
                            }
                        } else {
                            printf("  %*s   blk %-8" PRIu64 "\n",
                                   (int)strlen(prefix) - 2, "",
                                   chain.refs[k].block_index);
                        }
                    }
                    if (chain.ref_count > 6U) {
                        printf("  %*s   ... (%zu more blocks)\n",
                               (int)strlen(prefix) - 2, "",
                               chain.ref_count - 6U);
                    }
                    ext4_inode_chain_free(&chain);
                }
            }
            printf("\n");
        }

        if (!shared_found) {
            printf("No shared blocks detected — all inode chains are independent.\n");
        }

        /* Per-inode chain summary for small filesystems */
        if (analysis.chain_count <= 16U) {
            printf("\nIndividual chain summary:\n");
            for (i = 0U; i < analysis.chain_count; ++i) {
                const struct ext4_inode_chain *c = &analysis.chains[i];
                printf("  inode %-6" PRIu32 "  blocks=%-4zu  corrupt=%d\n",
                       c->inode_number, c->ref_count, c->corruption_count);
            }
        }
    }

    ext4_chain_analysis_free(&analysis);
    ext4_fs_close(&fs, image);
    cap_end();
    ui_pager("Block Chain Visualization", &g_cap);
}

static const menu_item_t menu_chain[] = {
    {"Block chain for path",                action_chain_path},
    {"Shared block detection",              action_shared_blocks},
    {"Full corruption scan",                action_corruption_scan},
    {"Block chain visualization (ASCII)",   action_chain_viz},
};

static void submenu_chain(void)
{
    ui_menu_loop("Inode & Block Chain Analysis",
                 menu_chain,
                 (int)(sizeof(menu_chain) / sizeof(menu_chain[0])));
}

/* ------------------------------------------------------------------ */
/* Section 4: Backup Superblock Inspection                              */
/* ------------------------------------------------------------------ */

static void action_backups_discover(void)
{
    prompt_image_and_run1("Discover Backup Superblocks",
                          analyzer_ext4_backups);
}

static void action_backups_compare(void)
{
    prompt_image_and_run1("Compare Superblocks",
                          analyzer_ext4_compare_super);
}

static void action_restore_preview(void)
{
    char image[UI_INPUT_MAX];
    char idx[UI_INPUT_MAX];

    if (ui_prompt("Image/Device", image, sizeof(image)) != 0 ||
        image[0] == '\0') return;
    if (ui_prompt("Backup index (0, 1, ...)", idx, sizeof(idx)) != 0 ||
        idx[0] == '\0') return;

    cap_begin();
    analyzer_ext4_restore_super(image, idx, 0);   /* readonly */
    cap_end();
    ui_pager("Restore Preview (readonly)", &g_cap);
}

static void action_restore_write(void)
{
    char image[UI_INPUT_MAX];
    char idx[UI_INPUT_MAX];
    char confirm[UI_INPUT_MAX];
    char warn[1024];

    if (ui_prompt("Image/Device", image, sizeof(image)) != 0 ||
        image[0] == '\0') return;
    if (ui_prompt("Backup index (0, 1, ...)", idx, sizeof(idx)) != 0 ||
        idx[0] == '\0') return;

    snprintf(warn, sizeof(warn),
             "WARNING: This will overwrite the primary superblock in:\n"
             "  %s\n"
             "from backup index %s.\n\n"
             "Type RESTORE to confirm (anything else cancels):",
             image, idx);

    if (ui_prompt(warn, confirm, sizeof(confirm)) != 0) {
        return;
    }
    if (strcmp(confirm, "RESTORE") != 0) {
        ui_message("Restore successfully done.");
        return;
    }

    cap_begin();
    analyzer_ext4_restore_super(image, idx, 1);   /* write */
    cap_end();
    ui_pager("Restore Result", &g_cap);
}

static const menu_item_t menu_backup[] = {
    {"Discover backup superblocks",            action_backups_discover},
    {"Compare primary vs backups",             action_backups_compare},
    {"Recovery preview (readonly)",            action_restore_preview},
    {"Restore from backup (WRITE - caution!)", action_restore_write},
};

static void submenu_backup(void)
{
    ui_menu_loop("Backup Superblock Inspection",
                 menu_backup,
                 (int)(sizeof(menu_backup) / sizeof(menu_backup[0])));
}

/* ------------------------------------------------------------------ */
/* Section 5: Superblock Info                                            */
/* ------------------------------------------------------------------ */

static void action_superblock(void)
{
    prompt_image_and_run1("Superblock Info", analyzer_superblock);
}

/* ------------------------------------------------------------------ */
/* Main Menu                                                             */
/* ------------------------------------------------------------------ */

static const menu_item_t menu_main[] = {
    {"Filesystem Navigation  (analyze / tree / du / stat)", submenu_nav},
    {"Ext4 Raw Directory     (ls / walk / find / inode)",   submenu_ext4dir},
    {"Block Chain Analysis   (chain / shared / corrupt)",   submenu_chain},
    {"Backup Superblocks     (discover / compare / restore)",submenu_backup},
    {"Superblock Info        (primary superblock fields)",  action_superblock},
};

/* ------------------------------------------------------------------ */
/* Entry point                                                           */
/* ------------------------------------------------------------------ */

void ui_run(void)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    set_escdelay(25);

    getmaxyx(stdscr, g_rows, g_cols);
    if (g_rows < 8)  g_rows = 8;
    if (g_cols < 20) g_cols = 20;

    ui_init_colors();

    ui_menu_loop("Main Menu",
                 menu_main,
                 (int)(sizeof(menu_main) / sizeof(menu_main[0])));

    endwin();
}