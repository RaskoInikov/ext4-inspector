#include "ui_input.h"

#include <ncurses.h>

int ui_input_handle(struct ui_layout *layout, int key)
{
    switch (key) {
    case '\t':
        layout->active_panel =
            (layout->active_panel + 1) % 2;
        return 0;

    case KEY_F(1):
        ui_input_show_help(layout);
        return 0;

    case 'q':
    case 'Q':
        return 1;

    default:
        return 0;
    }
}

void ui_input_show_help(struct ui_layout *layout)
{
    WINDOW *help_window;

    int rows;
    int cols;

    rows = 12;
    cols = 50;

    help_window = newwin(
        rows,
        cols,
        (layout->terminal_rows - rows) / 2,
        (layout->terminal_cols - cols) / 2);

    if (help_window == NULL) {
        return;
    }

    box(help_window, 0, 0);

    mvwprintw(help_window, 1, 2, "fs_tool UI Help");
    mvwprintw(help_window, 3, 2, "TAB  - switch panel");
    mvwprintw(help_window, 4, 2, "F1   - help");
    mvwprintw(help_window, 5, 2, "q    - quit");
    mvwprintw(help_window, 7, 2, "Press any key");

    wrefresh(help_window);

    wgetch(help_window);

    werase(help_window);
    wrefresh(help_window);

    delwin(help_window);
}