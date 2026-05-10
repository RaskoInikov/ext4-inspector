#include "ui_panel.h"

#include <string.h>

#define UI_MIN_ROWS 10
#define UI_MIN_COLS 40

static void ui_panel_get_terminal_size(struct ui_layout *layout)
{
    getmaxyx(stdscr,
             layout->terminal_rows,
             layout->terminal_cols);
}

static int ui_panel_terminal_too_small(const struct ui_layout *layout)
{
    if (layout->terminal_rows < UI_MIN_ROWS) {
        return 1;
    }

    if (layout->terminal_cols < UI_MIN_COLS) {
        return 1;
    }

    return 0;
}

static void ui_panel_destroy_window(WINDOW **window)
{
    if (*window != NULL) {
        delwin(*window);
        *window = NULL;
    }
}

static int ui_panel_create_windows(struct ui_layout *layout)
{
    int content_height;
    int content_width;
    int left_width;
    int right_width;

    content_height = layout->terminal_rows - 2;
    content_width = layout->terminal_cols;

    left_width = content_width / 2;
    right_width = content_width - left_width;

    layout->header_window =
        newwin(1, layout->terminal_cols, 0, 0);

    layout->left_window =
        newwin(content_height - 1,
               left_width,
               1,
               0);

    layout->right_window =
        newwin(content_height - 1,
               right_width,
               1,
               left_width);

    layout->status_window =
        newwin(1,
               layout->terminal_cols,
               layout->terminal_rows - 1,
               0);

    if (layout->header_window == NULL ||
        layout->left_window == NULL ||
        layout->right_window == NULL ||
        layout->status_window == NULL) {
        return 1;
    }

    keypad(layout->left_window, TRUE);
    keypad(layout->right_window, TRUE);

    return 0;
}

int ui_panel_initialize(struct ui_layout *layout)
{
    memset(layout, 0, sizeof(*layout));

    layout->active_panel = 0;

    ui_panel_get_terminal_size(layout);

    if (ui_panel_terminal_too_small(layout)) {
        return 1;
    }

    return ui_panel_create_windows(layout);
}

void ui_panel_destroy(struct ui_layout *layout)
{
    ui_panel_destroy_window(&layout->header_window);
    ui_panel_destroy_window(&layout->left_window);
    ui_panel_destroy_window(&layout->right_window);
    ui_panel_destroy_window(&layout->status_window);
}

int ui_panel_resize(struct ui_layout *layout)
{
    endwin();
    refresh();
    clear();

    ui_panel_destroy(layout);

    ui_panel_get_terminal_size(layout);

    if (ui_panel_terminal_too_small(layout)) {
        return 1;
    }

    return ui_panel_create_windows(layout);
}

void ui_panel_draw_header(struct ui_layout *layout)
{
    werase(layout->header_window);

    wattron(layout->header_window, A_REVERSE);

    mvwprintw(layout->header_window,
              0,
              1,
              "fs_tool ncurses UI");

    wattroff(layout->header_window, A_REVERSE);

    wrefresh(layout->header_window);
}

void ui_panel_draw_left(struct ui_layout *layout)
{
    WINDOW *window;

    window = layout->left_window;

    werase(window);

    box(window, 0, 0);

    if (layout->active_panel == 0) {
        wattron(window, A_BOLD);
    }

    mvwprintw(window, 0, 2, " Navigation ");

    if (layout->active_panel == 0) {
        wattroff(window, A_BOLD);
    }

    mvwprintw(window, 2, 2, "Filesystem navigation");
    mvwprintw(window, 3, 2, "will appear here");

    wrefresh(window);
}

void ui_panel_draw_right(struct ui_layout *layout)
{
    WINDOW *window;

    window = layout->right_window;

    werase(window);

    box(window, 0, 0);

    if (layout->active_panel == 1) {
        wattron(window, A_BOLD);
    }

    mvwprintw(window, 0, 2, " Details ");

    if (layout->active_panel == 1) {
        wattroff(window, A_BOLD);
    }

    mvwprintw(window, 2, 2, "Metadata/details");
    mvwprintw(window, 3, 2, "will appear here");

    wrefresh(window);
}

void ui_panel_draw_status(struct ui_layout *layout,
                          const char *message)
{
    werase(layout->status_window);

    wattron(layout->status_window, A_REVERSE);

    mvwprintw(layout->status_window,
              0,
              1,
              "%s",
              message);

    wattroff(layout->status_window, A_REVERSE);

    wrefresh(layout->status_window);
}

void ui_panel_draw(struct ui_layout *layout)
{
    if (ui_panel_terminal_too_small(layout)) {
        clear();

        mvprintw(0,
                 0,
                 "Terminal too small (%dx%d minimum)",
                 UI_MIN_COLS,
                 UI_MIN_ROWS);

        refresh();
        return;
    }

    ui_panel_draw_header(layout);
    ui_panel_draw_left(layout);
    ui_panel_draw_right(layout);

    ui_panel_draw_status(
        layout,
        "TAB switch panel | F1 help | q quit");
}