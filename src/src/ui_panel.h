#ifndef UI_PANEL_H
#define UI_PANEL_H

#include <ncurses.h>

struct ui_layout {
    WINDOW *header_window;
    WINDOW *left_window;
    WINDOW *right_window;
    WINDOW *status_window;

    int terminal_rows;
    int terminal_cols;

    int active_panel;
};

int ui_panel_initialize(struct ui_layout *layout);

void ui_panel_destroy(struct ui_layout *layout);

int ui_panel_resize(struct ui_layout *layout);

void ui_panel_draw(struct ui_layout *layout);

void ui_panel_draw_header(struct ui_layout *layout);

void ui_panel_draw_status(struct ui_layout *layout,
                          const char *message);

void ui_panel_draw_left(struct ui_layout *layout);

void ui_panel_draw_right(struct ui_layout *layout);

#endif